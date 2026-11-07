/* SPDX-License-Identifier: GPL-3.0-only */

#include "cover_grid_view.h"
#include "cover_grid_screen_ui.h"
#include "osos.h"
#include "patch.h"
#include "screen.h"
#include "ui.h"
#include "cfw.h"
#include "dark_mode.h"

#define FIELD(type, object, offset) (*(type *)((uint8_t *)(object) + (offset)))

enum {
    GRID_VIEW_TYPE = 0x63667767, /* cfwg */
};

/* Downscaled covers for the visible rows, the peeking one included. Slots are keyed by
 * artwork ID, not by album index, because the menu may rebuild the shared album
 * list while the grid still draws its pop transition. */
enum {
    SLOT_EMPTY,
    SLOT_LOADED,
    VISIBLE_ROWS = CFW_GRID_FULL_ROWS + 1,
    VISIBLE_COVERS = CFW_GRID_COLUMNS * VISIBLE_ROWS,
    CACHE_SLOTS = VISIBLE_COVERS,
    /* Cover Flow's no-artwork cover, kept downscaled after the cover slots. */
    PLACEHOLDER_BITMAP = 0x0dad0128,
    /* Downscales per frame; covers loaded beyond it follow with the next redraw. */
    SCALES_PER_DRAW = 4,
    SOURCE_SIZE = 128,
};

struct cover_slot {
    uint32_t artwork;
    uint32_t used;
    uint32_t state;
    uint16_t pixels[CFW_GRID_COVER * CFW_GRID_COVER];
};

static struct {
    void *view;      /* The live grid view, which owns the cache and session. */
    void *reference; /* Library model on the shared music browser. */
    int selected, top;
    /* The Songs list opened from the grid has not activated yet. */
    int songs_pushed;
    /* The scrollbar: when the grid last scrolled, the alpha and width last drawn
     * and whether it shows. */
    uint32_t scrolled_ms, scrollbar_drawn;
    int scrollbar_width, scrollbar_ticking;
    /* When the view last drew; a left grid releases its covers once it stops
     * drawing. */
    uint32_t drawn_ms;
    int release_pending;
    /* Visible covers, by cell, loaded beyond a frame's downscale budget. */
    uint32_t late;
    int client_added; /* The frame client is registered. */
    uint32_t clock;
    struct cover_slot *cache;
    /* Cover Flow's native artwork session: its worker reads covers from disk, so
     * drawing never waits for one. */
    void *session;
} grid;

static char caption[256];

static void *grid_model(void)
{
    return grid.reference ? osos_model_ref_get(&grid.reference) : 0;
}

static void *grid_browser(void)
{
    void *model = grid_model();
    return model ? FIELD(void *, model, 0x40) : 0;
}

/* The shared browser's album groups: a native vector starting {begin, end}. */
static struct osos_album_group *const *grid_albums(void)
{
    void *browser = grid_browser();
    return browser ? &FIELD(struct osos_album_group *, browser, OSOS_BROWSER_ALBUMS_OFFSET)
                   : 0;
}

static int grid_count(void)
{
    struct osos_album_group *const *albums = grid_albums();
    return albums && albums[0] ? (int)(albums[1] - albums[0]) : 0;
}

static uint32_t grid_artwork(int index)
{
    if (index < 0 || index >= grid_count())
        return 0;
    return grid_albums()[0][index].artwork;
}

const char *cfw_cover_grid_view_caption(void)
{
    return caption;
}

static unsigned append(unsigned length, const char *text)
{
    while (text && *text && length + 1 < sizeof(caption))
        caption[length++] = *text++;
    caption[length] = 0;
    return length;
}

/* The selected album's name, as the native Albums list shows it. */
static void album_name(struct osos_native_string *string)
{
    struct {
        uint16_t units;
        uint16_t text[255];
    } name;

    typedef void (*name_fn)(void *, uint32_t, void *);
    name.units = 0;
    ((name_fn)OSOS_BROWSER_ALBUM_NAME)(grid_browser(), grid.selected, &name);
    osos_native_string_assign_utf16(string, name.text, name.units < 255 ? name.units : 255);
}

/* Album and artist read like the native Albums list: the album name, then its
 * subtitle, joined by a centered dot. */
static void update_caption(void)
{
    unsigned length = 0;
    caption[0] = 0;
    void *model = grid_model();
    if (model && grid.selected < grid_count()) {
        struct osos_native_string string;
        osos_native_string_init(&string);
        album_name(&string);
        length = append(length, osos_native_string_data(&string));
        osos_native_string_destroy(&string);

        osos_native_string_init(&string);
        osos_model_album_subtitle(model, grid.selected, &string);
        const char *artist = osos_native_string_data(&string);
        if (artist && *artist) {
            length = append(length, " \xc2\xb7 ");
            append(length, artist);
        }
        osos_native_string_destroy(&string);
    }
    cfw_ui_notify(CFW_CoverGridView_Caption);
}

/* The CPU has no divider. Each output pixel covers 2 or 3 source pixels per axis,
 * so the box filter divides by 4, 6 or 9; this 16.16 reciprocal rounds exactly for
 * every channel sum it can see (at most 9 * 63). */
static inline uint32_t divide_rounded(uint32_t sum, uint32_t n)
{
    static const uint16_t reciprocal[10] = {0,    0,    32768, 21846, 16384,
                                            13108, 10923, 9363, 8192, 7282};
    return (sum + n / 2) * reciprocal[n] >> 16;
}

/* RGB565 spread so that nine pixels sum without carries: blue in bits 0-10, red in
 * 11-20 and green from 21. */
static inline uint32_t spread(uint32_t pixel)
{
    return (pixel | pixel << 16) & 0x07e0f81fu;
}

/* Box filter from the 128 px source; each output pixel averages the 2 or 3 by 2 or 3
 * source pixels it covers. ARM code, for the registers the inner loop needs. */
static PATCH_ARM void downscale(uint16_t *out, const unsigned char *pixels,
                                uint32_t row_bytes)
{
    unsigned char first[CFW_GRID_COVER], span[CFW_GRID_COVER];
    for (int i = 0; i < CFW_GRID_COVER; i++) {
        first[i] = (unsigned char)(i * SOURCE_SIZE / CFW_GRID_COVER);
        span[i] = (unsigned char)(((i + 1) * SOURCE_SIZE + CFW_GRID_COVER - 1) /
                                      CFW_GRID_COVER -
                                  first[i]);
    }
    for (int y = 0; y < CFW_GRID_COVER; y++) {
        const uint16_t *row0 = (const uint16_t *)(pixels + first[y] * row_bytes);
        const uint16_t *row1 = (const uint16_t *)((const unsigned char *)row0 + row_bytes);
        const uint16_t *row2 = (const uint16_t *)((const unsigned char *)row1 + row_bytes);
        int tall = span[y] == 3;
        for (int x = 0; x < CFW_GRID_COVER; x++) {
            int c = first[x], wide = span[x] == 3;
            uint32_t sum = spread(row0[c]) + spread(row0[c + 1]) + spread(row1[c]) +
                           spread(row1[c + 1]);
            if (wide)
                sum += spread(row0[c + 2]) + spread(row1[c + 2]);
            if (tall) {
                sum += spread(row2[c]) + spread(row2[c + 1]);
                if (wide)
                    sum += spread(row2[c + 2]);
            }
            uint32_t n = (uint32_t)span[y] * span[x];
            *out++ = (uint16_t)(divide_rounded(sum >> 11 & 0x3ff, n) << 11 |
                                divide_rounded(sum >> 21, n) << 5 |
                                divide_rounded(sum & 0x7ff, n));
        }
    }
}

/* Row bytes of a 128 px RGB565 bitmap {.., u16 row bytes @4, u16 bits @6,
 * height @0x10, width @0x14} whose pixels follow at +0x1c, or 0. */
static uint32_t source_row_bytes(const unsigned char *bitmap)
{
    if (!bitmap || FIELD(uint16_t, bitmap, 6) != 16 ||
        FIELD(uint32_t, bitmap, 0x10) != SOURCE_SIZE ||
        FIELD(uint32_t, bitmap, 0x14) != SOURCE_SIZE ||
        FIELD(uint16_t, bitmap, 4) < SOURCE_SIZE * 2)
        return 0;
    return FIELD(uint16_t, bitmap, 4);
}

/* Cover Flow's placeholder for albums without a loaded cover; null when the
 * resource is unusable, which falls back to a plain tile. */
static struct cover_slot *placeholder(void)
{
    if (!grid.cache)
        return 0;
    struct cover_slot *slot = &grid.cache[CACHE_SLOTS];
    if (slot->state == SLOT_EMPTY) {
        const unsigned char *bitmap =
            (const unsigned char *)osos_ui_bitmap(PLACEHOLDER_BITMAP);
        uint32_t row_bytes = source_row_bytes(bitmap);
        if (!row_bytes)
            return 0;
        downscale(slot->pixels, bitmap + 0x1c, row_bytes);
        slot->state = SLOT_LOADED;
    }
    return slot;
}

/* The session's 128 px RGB565 pixels for an album, once its worker has loaded them. */
static const unsigned char *session_cover(int index)
{
    uint32_t found = 0;
    uintptr_t image[10];
    typedef int (*lookup_fn)(void *, void *, int, void *, int);
    if (!grid.session || index < 0)
        return 0;
    ((lookup_fn)OSOS_ARTWORK_SESSION_LOOKUP)(grid.session, image, index, &found,
                                             OSOS_ARTWORK_FORMAT_COVER);
    return (found & 0xff) ? (const unsigned char *)image[0] : 0;
}

/* Returns the album's slot, downscaling its cover while budget remains once the
 * session has it; null draws the placeholder. pending reports loaded covers
 * left for the next frame. */
static struct cover_slot *cover(int index, int *budget, int *pending)
{
    uint32_t artwork = grid_artwork(index);
    if (!grid.cache || !artwork)
        return 0;
    struct cover_slot *victim = &grid.cache[0];
    for (unsigned i = 0; i < CACHE_SLOTS; i++) {
        struct cover_slot *slot = &grid.cache[i];
        if (slot->state != SLOT_EMPTY && slot->artwork == artwork) {
            slot->used = ++grid.clock;
            return slot;
        }
        if (slot->state == SLOT_EMPTY ||
            (victim->state != SLOT_EMPTY && slot->used < victim->used))
            victim = slot;
    }
    const unsigned char *pixels = session_cover(index);
    if (!pixels)
        return 0;
    if (*budget <= 0) {
        *pending = 1;
        return 0;
    }
    (*budget)--;
    downscale(victim->pixels, pixels, SOURCE_SIZE * 2);
    victim->artwork = artwork;
    victim->used = ++grid.clock;
    victim->state = SLOT_LOADED;
    return victim;
}

/* Surface writes follow Surface_FillRect (0x08069a5c): the surface at context
 * +0x1c (+4) holds {pixels, row bytes, .., bits shift @0xc} and is either RGB565
 * or ARGB8888. Drawing is clipped to the context's clip rectangle at +0x34 and to
 * bottom (view coordinates), and bracketed by the surface's write notifications. */
struct surface_region {
    uint32_t *surface;
    unsigned shift;
    int32_t top, left; /* The image's origin in surface coordinates. */
    int32_t dirty[4];  /* The clipped target: top, left, bottom, right. */
};

/* Returns 0 for a surface it cannot write; an empty dirty rectangle is clipped away. */
static int surface_region(void *context, int32_t top, int32_t left, int32_t height,
                          int32_t width, int32_t bottom, struct surface_region *out)
{
    unsigned char *bytes = context;
    out->surface = (uint32_t *)(*(unsigned char **)(bytes + 0x1c) + 4);
    out->shift = out->surface[3] & 0xff;
    if (!out->surface[0] || (out->shift != 4 && out->shift != 5))
        return 0;
    const int32_t *clip = (const int32_t *)(bytes + 0x34);
    int32_t dy = *(int32_t *)(bytes + 0x30), dx = *(int32_t *)(bytes + 0x2c);
    out->top = top + dy;
    out->left = left + dx;
    out->dirty[0] = out->top > clip[0] ? out->top : clip[0];
    out->dirty[1] = out->left > clip[1] ? out->left : clip[1];
    out->dirty[2] = out->top + height < clip[2] ? out->top + height : clip[2];
    out->dirty[3] = out->left + width < clip[3] ? out->left + width : clip[3];
    if (out->dirty[2] > bottom + dy)
        out->dirty[2] = bottom + dy;
    return 1;
}

static unsigned char *surface_row(const struct surface_region *region, int32_t y)
{
    return (unsigned char *)(uintptr_t)region->surface[0] + y * region->surface[1];
}

typedef void (*surface_write_fn)(void *, int32_t *);

/* RGB565 to opaque ARGB8888 by byte: the high byte's entry holds red and green's
 * top three bits, the low byte's green's low three bits and blue, so the two
 * entries OR into the full expansion. The tables follow the cache's slots. */
enum { EXPAND_BYTES = 2 * 256 * sizeof(uint32_t) };

static uint32_t *expand_table(void)
{
    return (uint32_t *)(grid.cache + CACHE_SLOTS + 1);
}

static uint32_t expand_565(uint32_t p)
{
    uint32_t r = p >> 11, g = (p >> 5) & 63, b = p & 31;
    return (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
}

static void fill_expand_table(uint32_t *table)
{
    for (uint32_t v = 0; v < 256; v++) {
        table[v] = 0xff000000u | expand_565(v << 8);
        table[256 + v] = expand_565(v);
    }
}

/* Copies a downscaled cover; returns 0 for a surface it cannot write, which then
 * shows the placeholder. ARM code, for the registers the copy loops need. */
static PATCH_ARM int blit(void *context, int32_t top, int32_t left, int32_t bottom,
                          const uint16_t *pixels)
{
    struct surface_region region;
    if (!surface_region(context, top, left, CFW_GRID_COVER, CFW_GRID_COVER, bottom,
                        &region))
        return 0;
    int32_t y0 = region.dirty[0], x0 = region.dirty[1];
    int32_t y1 = region.dirty[2], x1 = region.dirty[3];
    if (y0 >= y1 || x0 >= x1)
        return 1;
    ((surface_write_fn)OSOS_SURFACE_WRITE_BEGIN)(region.surface, region.dirty);
    for (int32_t y = y0; y < y1; y++) {
        unsigned char *row = surface_row(&region, y);
        const uint16_t *in =
            pixels + (y - region.top) * CFW_GRID_COVER + (x0 - region.left);
        if (region.shift == 4) {
            uint16_t *out = (uint16_t *)row + x0;
            for (int32_t x = x0; x < x1; x++)
                *out++ = *in++;
            continue;
        }
        const uint32_t *high = expand_table(), *low = high + 256;
        uint32_t *out = (uint32_t *)row + x0;
        for (int32_t x = x0; x < x1; x++) {
            uint32_t p = *in++;
            *out++ = high[p >> 8] | low[p & 0xff];
        }
    }
    ((surface_write_fn)OSOS_SURFACE_WRITE_END)(region.surface, region.dirty);
    return 1;
}

/* An 8-bit palettized native bitmap (format 0x64): palette count at +0x1c, BGRA
 * entries with straight alpha at +0x20, then one index per pixel. */
struct indexed_bitmap {
    const unsigned char *palette, *pixels;
    uint32_t colors, width, height, stride;
};

static int indexed_bitmap(uint32_t id, struct indexed_bitmap *out)
{
    const unsigned char *bitmap = (const unsigned char *)osos_ui_bitmap(id);
    if (!bitmap || FIELD(uint16_t, bitmap, 0) != 0x64 || FIELD(uint16_t, bitmap, 6) != 8)
        return 0;
    out->stride = FIELD(uint16_t, bitmap, 4);
    out->height = FIELD(uint32_t, bitmap, 0x10);
    out->width = FIELD(uint32_t, bitmap, 0x14);
    out->colors = FIELD(uint32_t, bitmap, 0x1c);
    if (!out->width || !out->height || out->stride < out->width || !out->colors ||
        out->colors > 256)
        return 0;
    out->palette = bitmap + 0x20;
    out->pixels = out->palette + 4 * out->colors;
    return 1;
}

/* value / 255 without a divider, exact below 65535. */
static uint32_t divide_255(uint32_t value)
{
    return (value + 1 + (value >> 8)) >> 8;
}

static uint32_t mix(uint32_t over, uint32_t under, uint32_t alpha)
{
    return divide_255(over * alpha + under * (255 - alpha) + 127);
}

/* Blends rows [first, first + rows) of a palettized bitmap at alpha (0-255) of its
 * own transparency. */
static void blend(void *context, int32_t top, int32_t left, int32_t bottom,
                  const struct indexed_bitmap *bitmap, uint32_t first, uint32_t rows,
                  uint32_t alpha)
{
    struct surface_region region;
    if (!surface_region(context, top, left, (int32_t)rows, (int32_t)bitmap->width,
                        bottom, &region))
        return;
    int32_t y0 = region.dirty[0], x0 = region.dirty[1];
    int32_t y1 = region.dirty[2], x1 = region.dirty[3];
    if (y0 >= y1 || x0 >= x1)
        return;
    ((surface_write_fn)OSOS_SURFACE_WRITE_BEGIN)(region.surface, region.dirty);
    for (int32_t y = y0; y < y1; y++) {
        unsigned char *row = surface_row(&region, y);
        const unsigned char *in = bitmap->pixels +
                                  (first + (uint32_t)(y - region.top)) * bitmap->stride +
                                  (uint32_t)(x0 - region.left);
        for (int32_t x = x0; x < x1; x++, in++) {
            uint32_t index = *in < bitmap->colors ? *in : 0;
            const unsigned char *color = bitmap->palette + 4 * index;
            uint32_t a = divide_255(color[3] * alpha);
            if (!a)
                continue;
            if (region.shift == 4) {
                uint16_t *out = (uint16_t *)row + x;
                uint32_t r = *out >> 11, g = (*out >> 5) & 63, b = *out & 31;
                r = mix(color[2], r << 3 | r >> 2, a);
                g = mix(color[1], g << 2 | g >> 4, a);
                b = mix(color[0], b << 3 | b >> 2, a);
                *out = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
            } else {
                uint32_t *out = (uint32_t *)row + x;
                *out = 0xff000000u | mix(color[2], (*out >> 16) & 255, a) << 16 |
                       mix(color[1], (*out >> 8) & 255, a) << 8 |
                       mix(color[0], *out & 255, a);
            }
        }
    }
    ((surface_write_fn)OSOS_SURFACE_WRITE_END)(region.surface, region.dirty);
}

/* Graphics fills skip rectangles the frame's clip excludes, which the surface
 * write would clip away anyway. */
struct canvas {
    void *context;
    const int32_t *bounds; /* top, left, bottom, right; bottom/right are exclusive. */
};

/* Whether a view-local rectangle meets the context's clip rectangle (+0x34, surface
 * coordinates, offset from the view by +0x30 and +0x2c). */
static int in_clip(const struct canvas *canvas, int x, int y, int width, int height)
{
    const unsigned char *bytes = canvas->context;
    const int32_t *clip = (const int32_t *)(bytes + 0x34);
    int32_t top = canvas->bounds[0] + y + *(const int32_t *)(bytes + 0x30);
    int32_t left = canvas->bounds[1] + x + *(const int32_t *)(bytes + 0x2c);
    return width > 0 && height > 0 && top < clip[2] && top + height > clip[0] &&
           left < clip[3] && left + width > clip[1];
}

static void fill(const struct canvas *canvas, int x, int y, int width, int height,
                 const unsigned char *color)
{
    if (!in_clip(canvas, x, y, width, height))
        return;
    const int32_t *bounds = canvas->bounds;
    int32_t rect[] = {bounds[0] + y, bounds[1] + x, bounds[0] + y + height,
                      bounds[1] + x + width};
    osos_graphics_set_mode(canvas->context, 0);
    osos_graphics_set_color(canvas->context, color);
    osos_graphics_fill_rect_unmapped(canvas->context, rect);
}

enum {
    COLOR_BACKGROUND,
    COLOR_TILE,
    COLOR_RING,
    COLOR_COUNT,
    RING_SIZE = CFW_GRID_COVER + 2 * CFW_GRID_RING,
    /* After deactivation, the covers and the session are released once the view
     * has not drawn for this long, which outlasts the pop and push transitions. */
    RELEASE_DELAY_MS = 1000,
};

static int min_int(int a, int b)
{
    return a < b ? a : b;
}

/* Light and dark: the list background, an empty-cover tile 11% toward the text
 * color, and the selection blue. */
static const unsigned char grid_colors[2][COLOR_COUNT][4] = {
    {{255, 255, 255, 255}, {231, 231, 231, 255}, {35, 108, 197, 255}},
    {{24, 24, 24, 255}, {47, 47, 47, 255}, {0, 161, 255, 255}},
};

/* The stock list scrollbar over the right column, as list scrollbars sit over their
 * rows. Like native lists (0x081ae814), it shows when the grid scrolls, holds for
 * 1.5 s after the last scroll, then fades out over 0.5 s. Its art is the theme's. */
enum {
    SCROLLBAR_TOP = 0x0dad0122,
    SCROLLBAR_CENTER = 0x0dad0123,
    SCROLLBAR_BOTTOM = 0x0dad0124,
    SCROLLBAR_HOLD_MS = 1500,
    SCROLLBAR_FADE_MS = 500,
};

static const uintptr_t *const grid_client;

/* The UI loop's clock, read as the loop reads it (0x08288a94). */
static uint32_t milliseconds(void)
{
    typedef void (*read_fn)(uint32_t *);
    typedef uint32_t (*milliseconds_fn)(const uint32_t *);
    uint32_t microseconds;
    ((read_fn)OSOS_UI_CLOCK_READ)(&microseconds);
    return ((milliseconds_fn)OSOS_UI_CLOCK_MILLISECONDS)(&microseconds);
}

/* Redraws a view-local rectangle. Views keep their frame at +0x80 in window
 * coordinates, and the grid's local rectangle starts at 0, 0. The invalidation goes
 * through the root view, as osos_ui_invalidate does: the native call ignores the
 * grid view itself, whose flags fail its visibility test (0x082990d8). */
static void invalidate_area(int x, int y, int width, int height)
{
    typedef unsigned char *(*root_fn)(void);
    typedef void (*invalidate_fn)(void *, const int32_t *);
    unsigned char *view = grid.view, *root = ((root_fn)OSOS_UI_ROOT_VIEW)();
    if (!view || !root)
        return;
    const int32_t *frame = (const int32_t *)(view + 0x80);
    int32_t rect[] = {frame[0] + y, frame[1] + x, frame[0] + y + height,
                      frame[1] + x + width};
    ((invalidate_fn)OSOS_VIEW_INVALIDATE_RECT)(root, rect);
}

/* Redraws one visible cover with its selection ring. */
static void invalidate_cover(int index)
{
    int row = index / CFW_GRID_COLUMNS - grid.top;
    if (index < 0 || row < 0 || row >= VISIBLE_ROWS)
        return;
    int x = CFW_GRID_MARGIN + index % CFW_GRID_COLUMNS * CFW_GRID_PITCH;
    int y = CFW_GRID_MARGIN + row * CFW_GRID_PITCH;
    invalidate_area(x - CFW_GRID_RING, y - CFW_GRID_RING, RING_SIZE, RING_SIZE);
}

static uint32_t scrollbar_alpha(void)
{
    if (!grid.scrollbar_ticking)
        return 0;
    uint32_t elapsed = milliseconds() - grid.scrolled_ms;
    if (elapsed < SCROLLBAR_HOLD_MS)
        return 255;
    if (elapsed >= SCROLLBAR_HOLD_MS + SCROLLBAR_FADE_MS)
        return 0;
    return 255 - (elapsed - SCROLLBAR_HOLD_MS) * 255 / SCROLLBAR_FADE_MS;
}

/* Native thumb geometry (0x081ae0c8): a track from 1 px below the top to 2 px above
 * the bottom of the covers' area, a thumb as long as the visible fraction (at least
 * minimum) placed by the scrolled fraction, and art reaching 1 px above and 2 px
 * below it. Returns 0 when the grid does not scroll. */
static int scrollbar_span(int count, int top, int height, int minimum, int32_t *y0,
                          int32_t *y1)
{
    int rows = (count + CFW_GRID_COLUMNS - 1) / CFW_GRID_COLUMNS;
    if (rows <= CFW_GRID_FULL_ROWS)
        return 0;
    int track = height - 3;
    int scroll = (rows - CFW_GRID_FULL_ROWS) * CFW_GRID_PITCH;
    int length = track * height / (scroll + height);
    if (length < minimum)
        length = minimum;
    if (length > track)
        length = track;
    int position = (track - length) * top * CFW_GRID_PITCH / scroll;
    *y0 = position;
    *y1 = position + length + 3;
    return 1;
}

/* One native frame client serves the scrollbar's fade, late covers and the
 * deferred release; it is registered while any of them needs it. Removal inside the clients' pass is
 * deferred natively (0x082892e0), so the client may remove itself. */
static void update_client(void)
{
    typedef void (*client_fn)(const void *);
    int wanted = grid.scrollbar_ticking || grid.release_pending || grid.late;
    if (wanted == grid.client_added)
        return;
    if (wanted)
        ((client_fn)OSOS_FRAME_CLIENT_ADD)(&grid_client);
    else
        ((client_fn)OSOS_FRAME_CLIENT_REMOVE)(&grid_client);
    grid.client_added = wanted;
}

static void scrollbar_stop(void)
{
    grid.scrollbar_ticking = 0;
    update_client();
}

/* Caps and a tiled center, flush with the right edge. */
static void draw_scrollbar(void *context, const int32_t *bounds, int height, int count)
{
    uint32_t alpha = scrollbar_alpha();
    grid.scrollbar_drawn = alpha;
    if (!alpha) {
        scrollbar_stop();
        return;
    }
    struct indexed_bitmap cap_top, center, cap_bottom;
    int32_t y0, y1;
    if (!indexed_bitmap(SCROLLBAR_TOP, &cap_top) ||
        !indexed_bitmap(SCROLLBAR_CENTER, &center) ||
        !indexed_bitmap(SCROLLBAR_BOTTOM, &cap_bottom) || center.width != cap_top.width ||
        cap_bottom.width != cap_top.width ||
        !scrollbar_span(count, grid.top, height,
                        (int)(cap_top.height + cap_bottom.height) - 3, &y0, &y1))
        return;
    grid.scrollbar_width = (int)cap_top.width;
    int32_t left = bounds[3] - (int32_t)cap_top.width, bottom = bounds[0] + height;
    int32_t end = y1 - (int32_t)cap_bottom.height;
    blend(context, bounds[0] + y0, left, bottom, &cap_top, 0, cap_top.height, alpha);
    for (int32_t y = y0 + (int32_t)cap_top.height; y < end; y += (int32_t)center.height)
        blend(context, bounds[0] + y, left, bottom, &center, 0,
              (uint32_t)min_int((int)center.height, end - y), alpha);
    blend(context, bounds[0] + end, left, bottom, &cap_bottom, 0, cap_bottom.height,
          alpha);
}

static void allocate_cache(void)
{
    if (grid.cache)
        return;
    unsigned bytes = (CACHE_SLOTS + 1) * sizeof(*grid.cache);
    grid.cache = osos_import_alloc(bytes + EXPAND_BYTES);
    if (!grid.cache)
        return;
    cfw_zero(grid.cache, bytes);
    fill_expand_table(expand_table());
}

/* Background in the margins and gaps around the covers and in the strip under the
 * caption bar; the covers paint the rest. */
static void draw_gaps(const struct canvas *canvas, int width, int height, int full,
                      const unsigned char *color)
{
    fill(canvas, 0, 0, width, CFW_GRID_MARGIN, color);
    for (int column = 0; column <= CFW_GRID_COLUMNS; column++) {
        int x = column ? CFW_GRID_MARGIN + column * CFW_GRID_PITCH - CFW_GRID_GAP : 0;
        int gap = !column ? CFW_GRID_MARGIN
                          : column == CFW_GRID_COLUMNS ? width - x : CFW_GRID_GAP;
        fill(canvas, x, CFW_GRID_MARGIN, gap, height - CFW_GRID_MARGIN, color);
    }
    for (int row = 0; row < VISIBLE_ROWS; row++) {
        int y = CFW_GRID_MARGIN + row * CFW_GRID_PITCH + CFW_GRID_COVER;
        if (y < height)
            fill(canvas, 0, y, width, min_int(CFW_GRID_GAP, height - y), color);
    }
    fill(canvas, 0, height, width, full - height, color);
}

/* The selection ring, drawn in the gap around the cover and cut at the caption bar. */
static void draw_ring(const struct canvas *canvas, int x, int y, int below,
                      const unsigned char *color)
{
    int side = min_int(below, CFW_GRID_COVER);
    fill(canvas, x - CFW_GRID_RING, y - CFW_GRID_RING, RING_SIZE, CFW_GRID_RING, color);
    fill(canvas, x - CFW_GRID_RING, y, CFW_GRID_RING, side, color);
    fill(canvas, x + CFW_GRID_COVER, y, CFW_GRID_RING, side, color);
    if (below > CFW_GRID_COVER)
        fill(canvas, x - CFW_GRID_RING, y + CFW_GRID_COVER, RING_SIZE,
             min_int(below - CFW_GRID_COVER, CFW_GRID_RING), color);
}

static PATCH_ARM void grid_draw(void *view, void *draw_argument, const int32_t *clip)
{
    (void)draw_argument;
    (void)clip;
    uint32_t context[0x44 / 4];
    int32_t bounds[4];
    typedef void (*view_context_fn)(void *, void *);
    typedef void (*rect_fn)(void *, int32_t *);
    ((void *(*)(void *))OSOS_GRAPHICS_CONSTRUCT)(context);
    ((view_context_fn)OSOS_VIEW_BIND_GRAPHICS)(view, context);
    ((rect_fn)OSOS_VIEW_LOCAL_RECT)(view, bounds);
    struct canvas canvas = {context, bounds};
    const unsigned char(*colors)[4] = grid_colors[cfw_dark_mode_enabled() ? 1 : 0];
    const unsigned char *background = colors[COLOR_BACKGROUND];
    int width = bounds[3] - bounds[1], full = bounds[2] - bounds[0];
    /* The caption bar draws over the strip below the covers. */
    int height = full - CFW_GRID_CAPTION;
    grid.drawn_ms = milliseconds();
    /* The native UI builds the view at boot, long before it is shown, so the cache
     * is allocated on the first draw and again after a release. */
    allocate_cache();
    draw_gaps(&canvas, width, height, full, background);

    int count = grid_count();
    int budget = SCALES_PER_DRAW;
    unsigned late = 0;
    for (int row = 0; row < VISIBLE_ROWS; row++) {
        int y = CFW_GRID_MARGIN + row * CFW_GRID_PITCH;
        int below = height - y;
        if (below <= 0)
            break;
        for (int column = 0; column < CFW_GRID_COLUMNS; column++) {
            int index = (grid.top + row) * CFW_GRID_COLUMNS + column;
            int x = CFW_GRID_MARGIN + column * CFW_GRID_PITCH;
            if (!in_clip(&canvas, x - CFW_GRID_RING, y - CFW_GRID_RING, RING_SIZE,
                         min_int(below + CFW_GRID_RING, RING_SIZE)))
                continue;
            int side = min_int(below, CFW_GRID_COVER);
            if (index >= count) {
                fill(&canvas, x, y, CFW_GRID_COVER, side, background);
                continue;
            }
            int pending = 0;
            struct cover_slot *slot = cover(index, &budget, &pending);
            if (pending)
                late |= 1u << (row * CFW_GRID_COLUMNS + column);
            if (!slot)
                slot = placeholder();
            if (index == grid.selected)
                draw_ring(&canvas, x, y, below, colors[COLOR_RING]);
            if (!slot || !blit(context, bounds[0] + y, bounds[1] + x, bounds[0] + height,
                               slot->pixels))
                fill(&canvas, x, y, CFW_GRID_COVER, side, colors[COLOR_TILE]);
        }
    }
    draw_scrollbar(context, bounds, height, count);
    /* Covers loaded beyond the budget follow in the next frame; invalidations made
     * while the frame renders are lost, so the frame client makes them. */
    grid.late |= late;
    update_client();
}

static void release_session(void)
{
    if (grid.session) {
        typedef void *(*destroy_fn)(void *);
        ((destroy_fn)OSOS_ARTWORK_SESSION_DESTROY)(grid.session);
        osos_import_free(grid.session);
    }
    grid.session = 0;
}

/* Frees the downscaled covers and the session with its 128 px covers; drawing and
 * activation allocate them again. */
static void release_covers(void)
{
    release_session();
    if (grid.cache)
        osos_import_free(grid.cache);
    grid.cache = 0;
    grid.clock = 0;
    grid.late = 0;
    grid.release_pending = 0;
    update_client();
}

/* Called on each UI loop pass before the frame renders (0x08288a94). Redraws late
 * covers and the scrollbar while its alpha changes, and releases the covers once a
 * left grid has stopped drawing. */
static PATCH_ARM void grid_tick(void *client)
{
    (void)client;
    for (int cell = 0; grid.late; cell++, grid.late >>= 1)
        if (grid.late & 1)
            invalidate_cover(grid.top * CFW_GRID_COLUMNS + cell);
    if (grid.scrollbar_ticking && scrollbar_alpha() != grid.scrollbar_drawn) {
        unsigned char *view = grid.view;
        const int32_t *frame = view ? (const int32_t *)(view + 0x80) : 0;
        if (frame) {
            int width = frame[3] - frame[1];
            int height = frame[2] - frame[0] - CFW_GRID_CAPTION;
            invalidate_area(width - grid.scrollbar_width, 0, grid.scrollbar_width, height);
        }
    }
    if (grid.release_pending && milliseconds() - grid.drawn_ms >= RELEASE_DELAY_MS)
        release_covers();
    update_client();
}

static PATCH_ARM void grid_rendered(void *client)
{
    (void)client;
}

/* A native frame client: slot 0 runs before the frame renders, slot 1 after it. */
static const uintptr_t grid_client_vtable[2] = {(uintptr_t)grid_tick,
                                                (uintptr_t)grid_rendered};
static const uintptr_t *const grid_client = grid_client_vtable;

static void scrollbar_show(void)
{
    grid.scrolled_ms = milliseconds();
    grid.scrollbar_ticking = 1;
    update_client();
}

/* The session reports each cover its worker finishes through slot 2 of this
 * listener's vtable, on the UI thread; the other slots are never called. */
static PATCH_ARM void cover_loaded(void *listener, void *item, int index)
{
    (void)listener;
    (void)item;
    if (grid.view)
        invalidate_cover(index);
}

static const uintptr_t listener_vtable[3] = {[2] = (uintptr_t)cover_loaded};
static const uintptr_t *const listener = listener_vtable;

/* The session's window reaches only a few albums behind its center but well
 * past the visible rows ahead of it, so it centers on the first visible album.
 * Like Cover Flow, it is placed once on activation and then moved by the
 * scrolled distance, which tells the worker the direction to load. */
static void center_session(int absolute, int value)
{
    typedef int (*center_fn)(void *, int, int);
    if (grid.session)
        ((center_fn)OSOS_ARTWORK_SESSION_CENTER)(grid.session, absolute, value);
}

/* Cover Flow's activation: listen, choose the 128 px format, follow the current
 * music query, center, then start the worker. */
static void start_session(void)
{
    typedef int (*session_fn)(void *);
    typedef int (*attach_fn)(void *, const void *);
    typedef int (*format_fn)(void *, int);
    if (!grid.session) {
        typedef void *(*construct_fn)(void *, const char *);
        void *session = osos_import_alloc(OSOS_ARTWORK_SESSION_BYTES);
        if (session)
            grid.session = ((construct_fn)OSOS_ARTWORK_SESSION_CONSTRUCT)(session, "CoverGrid");
    }
    if (!grid.session)
        return;
    ((attach_fn)OSOS_ARTWORK_SESSION_ATTACH)(grid.session, &listener);
    ((format_fn)OSOS_ARTWORK_SESSION_SET_FORMAT)(grid.session, OSOS_ARTWORK_FORMAT_COVER);
    ((session_fn)OSOS_ARTWORK_SESSION_BIND_QUERY)(grid.session);
    center_session(1, grid.top * CFW_GRID_COLUMNS);
    ((session_fn)OSOS_ARTWORK_SESSION_START)(grid.session);
}

static void stop_session(void)
{
    typedef int (*session_fn)(void *);
    if (grid.session)
        ((session_fn)OSOS_ARTWORK_SESSION_STOP)(grid.session);
}

static void release_model(void)
{
    if (grid.reference)
        osos_model_ref_release(&grid.reference);
    grid.reference = 0;
}

/* The view outlives its controller through the pop transition and until the grid
 * opens again, so the model is released with the view. A grid reopened meanwhile
 * owns the state instead. */
static PATCH_ARM void *grid_destroy(void *view)
{
    if (view == grid.view) {
        scrollbar_stop();
        release_covers();
        grid.view = 0;
        release_model();
    }
    typedef void *(*destroy_fn)(void *);
    return ((destroy_fn)((const uintptr_t *)OSOS_VIEW_VTABLE)[CFW_VIEW_DESTROY_SLOT / 4])(
        view);
}

CFW_VIEW_VTABLE_DESTROY(grid_vtable, grid_destroy, grid_draw);

static PATCH_ARM void *grid_create(void *owner, void *parent, void *event_target,
                                   const void *descriptor)
{
    (void)owner;
    void *view = cfw_view_create(parent, event_target, descriptor, grid_vtable);
    grid.view = view;
    grid.release_pending = 0;
    update_client();
    return view;
}

static struct cfw_wheel grid_wheel;

/* Like the native Albums list: select the album on the shared browser, title the
 * song list with its name, then show it. */
static void open_album(void *controller)
{
    void *browser = grid_browser();
    if (!browser || grid.selected >= grid_count())
        return;
    typedef int (*select_fn)(void *, uint32_t);
    if (((select_fn)OSOS_BROWSER_SELECT_ALBUM)(browser, grid.selected))
        return;
    typedef void *(*title_model_fn)(void);
    typedef void (*title_fn)(void *, struct osos_native_string *);
    struct osos_native_string title;
    osos_native_string_init(&title);
    album_name(&title);
    ((title_fn)OSOS_LIST_TITLE_SET)(((title_model_fn)OSOS_LIST_TITLE_MODEL)(), &title);
    osos_native_string_destroy(&title);
    void *screen = osos_create_screen_controller(
        osos_controller_registry(), OSOS_SONGS_SCREEN, OSOS_SONGS_PLAIN_LAYOUT, 2);
    if (!screen)
        return;
    osos_model_ref_assign((uint8_t *)screen + 0xb4, &grid.reference);
    grid.songs_pushed = 1;
    osos_controller_push(controller, screen);
}

/* After a transition, the UI restores each list's saved position for the screen
 * (0x08174878), so a Songs list reopened on the same album returns to its last row.
 * Native code clears that saved row for its Extras and Settings screens; the Songs
 * screen the grid has just pushed gets the same, once. Coming back from Now Playing
 * restores the position as usual. */
PATCH_ARM uint32_t cfw_cover_grid_restore_positions(void *ui, uint32_t screen,
                                                    uint32_t layout)
{
    typedef uint32_t *(*saved_fn)(void *, uint32_t, uint32_t, int);
    typedef uint32_t (*restore_fn)(void *, uint32_t, uint32_t);
    if (grid.songs_pushed && screen == OSOS_SONGS_SCREEN) {
        grid.songs_pushed = 0;
        uint32_t *saved = ((saved_fn)OSOS_UI_SAVED_POSITION)(ui, screen, layout, 1);
        if (saved)
            saved[OSOS_UI_SAVED_ROW_WORD] = 0;
    }
    return ((restore_fn)OSOS_UI_RESTORE_POSITIONS)(ui, screen, layout);
}

PATCH_CALL(OSOS_UI_RESTORE_POSITIONS_CALL, OSOS_UI_RESTORE_POSITIONS,
           cfw_cover_grid_restore_positions);

static PATCH_ARM int grid_event(void *controller, void *event)
{
    const uint32_t *words = event;
    if (words[1] == OSOS_EVENT_WHEEL) {
        int delta = cfw_wheel_step(&grid_wheel, event);
        int count = grid_count();
        if (!delta || !count)
            return 1;
        int next = cfw_clamp((int64_t)grid.selected + delta, 0, count - 1);
        if (next == grid.selected)
            return 1;
        int previous = grid.selected;
        grid.selected = next;
        int top = cfw_cover_grid_top_row(grid.top, grid.selected);
        if (top != grid.top) {
            center_session(0, (top - grid.top) * CFW_GRID_COLUMNS);
            grid.top = top;
            scrollbar_show();
            osos_ui_invalidate();
        } else {
            /* Only the two rings change. */
            invalidate_cover(previous);
            invalidate_cover(next);
        }
        update_caption();
        return 1;
    }
    if (words[1] == OSOS_EVENT_BUTTON_RELEASE && osos_button_ui_key(event) == OSOS_KEY_CENTER) {
        open_album(controller);
        return 1;
    }
    return osos_controller_handle_event(controller, event);
}

static PATCH_ARM uint32_t grid_activate(void *controller, uint32_t entering)
{
    cfw_wheel_reset(&grid_wheel, 1);
    grid.songs_pushed = 0;
    grid.release_pending = 0;
    update_client();
    start_session();
    return osos_controller_activate(controller, entering);
}

static PATCH_ARM uint32_t grid_deactivate(void *controller, uint32_t leaving)
{
    stop_session();
    grid.drawn_ms = milliseconds();
    grid.release_pending = 1;
    scrollbar_stop();
    return osos_controller_deactivate(controller, leaving);
}

CFW_CONTROLLER_VTABLE(grid_controller_vtable, grid_event, grid_activate, grid_deactivate);

static PATCH_ARM void *grid_controller_create(const char *name, uint32_t screen,
                                              uint32_t layout, uint32_t transition)
{
    void *controller = osos_controller_create_screen(name, screen, layout, transition);
    if (controller)
        cfw_set_vtable(controller, grid_controller_vtable);
    return controller;
}

void cfw_cover_grid_view_open(void *controller)
{
    /* Cover Flow's query: music-only album groups sorted by artist. The native
     * handler raises the show-artwork flag first. */
    typedef uint32_t (*configure_fn)(void *, uint32_t, uint32_t, uint32_t, uint32_t,
                                     uint32_t, uint32_t);
    FIELD(uint8_t, controller, 0xb0) = 1;
    if (!((configure_fn)OSOS_MUSIC_CONFIGURE_BROWSER)(controller, 1, 0, 1, 5, 2, 0))
        return;
    release_model();
    void *model = osos_model_create(0);
    if (!model)
        return;
    osos_model_ref_init(&grid.reference, model, 0);
    /* Reopen on the last album, as Cover Flow does, while it still exists. */
    if (grid.selected >= grid_count())
        grid.selected = grid.top = 0;
    grid.top = cfw_cover_grid_top_row(grid.top, grid.selected);
    void *screen = osos_create_screen_controller(osos_controller_registry(),
                                                 CFW_CoverGridView_Screen,
                                                 CFW_CoverGridView_Layout, 2);
    if (!screen) {
        release_model();
        return;
    }
    update_caption();
    osos_controller_push(controller, screen);
}

void cfw_ui_register_cover_grid(void)
{
    typedef void (*register_fn)(uint32_t, void *);
    ((register_fn)OSOS_REGISTER_VIEW_FACTORY)(GRID_VIEW_TYPE, grid_create);
    osos_ui_register_controllers(osos_controller_registry(), "CFWCoverGrid", 0,
                                 grid_controller_create);
}
