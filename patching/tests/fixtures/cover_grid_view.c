/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../payload/native/bindings.h"
#include "cover_grid_screen_ui.h"

#define CFW_OSOS_H
#define CFW_PATCH_H
#define PATCH_ARM
#define PATCH_COPY(...)
#define PATCH_CALL(...)

/* Natives the grid's input and artwork paths call; drawing and screen pushes are
 * linked but not executed by this test. */
void unreachable(void)
{
    abort();
}

int session_lookup(void *session, void *image, int index, void *found, int format);
int session_center(void *session, int absolute, int value);
int session_attach(void *session, const void *listener);
int session_set_format(void *session, int format);
int session_bind_query(void *session);
int session_start(void *session);
int session_stop(void *session);
void *root_view(void);
void view_invalidate_rect(void *view, const int32_t *rect);
void *session_construct(void *session, const char *name);
void *session_destroy(void *session);
void view_init_options(const void *descriptor, void *options);
void *view_new(uint32_t bytes);
void *view_construct(void *view, void *owner, void *parent, void *target,
                     const void *options);
void *native_view_destroy(void *view);
void browser_album_name(void *browser, uint32_t index, void *out);
uint32_t *saved_position(void *ui, uint32_t screen, uint32_t layout, int create);
uint32_t restore_positions(void *ui, uint32_t screen, uint32_t layout);
void ui_clock_read(uint32_t *microseconds);
uint32_t ui_clock_milliseconds(const uint32_t *microseconds);
void frame_client_add(const void *client);
void frame_client_remove(const void *client);

static const uintptr_t native_view_vtable[2] = {0, (uintptr_t)native_view_destroy};

#undef OSOS_ARTWORK_SESSION_ATTACH
#undef OSOS_ARTWORK_SESSION_BIND_QUERY
#undef OSOS_ARTWORK_SESSION_CENTER
#undef OSOS_ARTWORK_SESSION_CONSTRUCT
#undef OSOS_ARTWORK_SESSION_DESTROY
#undef OSOS_ARTWORK_SESSION_LOOKUP
#undef OSOS_ARTWORK_SESSION_SET_FORMAT
#undef OSOS_ARTWORK_SESSION_START
#undef OSOS_ARTWORK_SESSION_STOP
#undef OSOS_BROWSER_ALBUM_NAME
#undef OSOS_BROWSER_SELECT_ALBUM
#undef OSOS_GRAPHICS_CONSTRUCT
#undef OSOS_VIEW_BIND_GRAPHICS
#undef OSOS_VIEW_LOCAL_RECT
#undef OSOS_LIST_TITLE_MODEL
#undef OSOS_LIST_TITLE_SET
#undef OSOS_MUSIC_CONFIGURE_BROWSER
#undef OSOS_SURFACE_WRITE_BEGIN
#undef OSOS_SURFACE_WRITE_END
#undef OSOS_REGISTER_VIEW_FACTORY
#undef OSOS_VIEW_INIT_OPTIONS
#undef OSOS_CPP_NEW
#undef OSOS_VIEW_CONSTRUCT
#undef OSOS_VIEW_VTABLE
#undef OSOS_UI_SAVED_POSITION
#undef OSOS_UI_RESTORE_POSITIONS
#undef OSOS_UI_CLOCK_READ
#undef OSOS_UI_CLOCK_MILLISECONDS
#undef OSOS_FRAME_CLIENT_ADD
#undef OSOS_FRAME_CLIENT_REMOVE
#undef OSOS_UI_ROOT_VIEW
#undef OSOS_VIEW_INVALIDATE_RECT
#define OSOS_ARTWORK_SESSION_ATTACH session_attach
#define OSOS_ARTWORK_SESSION_BIND_QUERY session_bind_query
#define OSOS_ARTWORK_SESSION_CENTER session_center
#define OSOS_ARTWORK_SESSION_CONSTRUCT session_construct
#define OSOS_ARTWORK_SESSION_DESTROY session_destroy
#define OSOS_ARTWORK_SESSION_LOOKUP session_lookup
#define OSOS_ARTWORK_SESSION_SET_FORMAT session_set_format
#define OSOS_ARTWORK_SESSION_START session_start
#define OSOS_ARTWORK_SESSION_STOP session_stop
#define OSOS_BROWSER_ALBUM_NAME browser_album_name
#define OSOS_BROWSER_SELECT_ALBUM unreachable
#define OSOS_GRAPHICS_CONSTRUCT unreachable
#define OSOS_VIEW_BIND_GRAPHICS unreachable
#define OSOS_VIEW_LOCAL_RECT unreachable
#define OSOS_LIST_TITLE_MODEL unreachable
#define OSOS_LIST_TITLE_SET unreachable
#define OSOS_MUSIC_CONFIGURE_BROWSER unreachable
#define OSOS_SURFACE_WRITE_BEGIN unreachable
#define OSOS_SURFACE_WRITE_END unreachable
#define OSOS_REGISTER_VIEW_FACTORY unreachable
#define OSOS_VIEW_INIT_OPTIONS view_init_options
#define OSOS_CPP_NEW view_new
#define OSOS_VIEW_CONSTRUCT view_construct
#define OSOS_VIEW_VTABLE ((uintptr_t)native_view_vtable)
#define OSOS_UI_SAVED_POSITION saved_position
#define OSOS_UI_RESTORE_POSITIONS restore_positions
#define OSOS_UI_CLOCK_READ ui_clock_read
#define OSOS_UI_CLOCK_MILLISECONDS ui_clock_milliseconds
#define OSOS_FRAME_CLIENT_ADD frame_client_add
#define OSOS_FRAME_CLIENT_REMOVE frame_client_remove
#define OSOS_UI_ROOT_VIEW root_view
#define OSOS_VIEW_INVALIDATE_RECT view_invalidate_rect

typedef void *(*osos_controller_factory)(const char *, uint32_t, uint32_t, uint32_t);

struct osos_album_group {
    uint32_t token, artist, display_artist;
    uint32_t artwork;
    uint32_t tracks, flags;
};

struct osos_native_string {
    uintptr_t *vtable;
    char *data;
};

enum { ALBUMS = 30, SOURCE_PIXELS = 128 * 128 };

struct browser {
    unsigned char bytes[OSOS_BROWSER_ALBUMS_OFFSET];
    struct osos_album_group *begin, *end;
};
struct model {
    unsigned char bytes[0x40];
    void *browser;
};
_Static_assert(offsetof(struct browser, begin) == OSOS_BROWSER_ALBUMS_OFFSET,
               "Browser album vector offset");
_Static_assert(offsetof(struct model, browser) == 0x40, "Model browser offset");

static struct osos_album_group albums[ALBUMS];
static struct browser browser;
static struct model model;
static unsigned char loaded[ALBUMS];
static uint16_t sources[2][SOURCE_PIXELS];
static uint32_t bitmap[(0x1c + SOURCE_PIXELS * 2) / 4];
static unsigned char view_storage[0xa4], session_storage[16];
static int bitmap_usable, bitmap_loads, lookups, invalidations, notifications;
static int centered_absolute, centered_value, centers, delegated, released, destroyed;
static int native_destroys, freed, restores, starts, stops;
static unsigned char root_storage[0xa4];
static int32_t invalidated[4];
static uint32_t saved_songs[4] = {1, 0, 9, 9};
static uint32_t now_ms = 1000;
static const void *frame_client;
static int frame_adds, frame_removes;

void ui_clock_read(uint32_t *microseconds)
{
    *microseconds = now_ms * 1000;
}

uint32_t ui_clock_milliseconds(const uint32_t *microseconds)
{
    return *microseconds / 1000;
}

void frame_client_add(const void *client)
{
    frame_client = client;
    frame_adds++;
}

void frame_client_remove(const void *client)
{
    assert(client == frame_client);
    frame_client = 0;
    frame_removes++;
}
static int ui;

uint32_t *saved_position(void *value, uint32_t screen, uint32_t layout, int create)
{
    assert(value == &ui && screen == OSOS_SONGS_SCREEN && layout == 7 && create == 1);
    return saved_songs;
}

uint32_t restore_positions(void *value, uint32_t screen, uint32_t layout)
{
    assert(value == &ui && layout == 7);
    restores++;
    return screen;
}

int session_lookup(void *session, void *image, int index, void *found, int format)
{
    assert(session == session_storage && format == OSOS_ARTWORK_FORMAT_COVER);
    lookups++;
    *(uint32_t *)found = loaded[index];
    ((uintptr_t *)image)[0] = (uintptr_t)sources[index % 2];
    return 0;
}

int session_attach(void *session, const void *listener)
{
    assert(session == session_storage && listener);
    return 0;
}

int session_set_format(void *session, int format)
{
    assert(session == session_storage && format == OSOS_ARTWORK_FORMAT_COVER);
    return 1;
}

int session_bind_query(void *session)
{
    assert(session == session_storage);
    return 1;
}

int session_start(void *session)
{
    assert(session == session_storage);
    starts++;
    return 1;
}

int session_stop(void *session)
{
    assert(session == session_storage);
    stops++;
    return 0;
}

void *root_view(void)
{
    return root_storage;
}

/* Cover and scrollbar redraws go through the root view in window coordinates. */
void view_invalidate_rect(void *view, const int32_t *rect)
{
    assert(view == root_storage);
    memcpy(invalidated, rect, sizeof(invalidated));
    invalidations++;
}

int session_center(void *session, int absolute, int value)
{
    assert(session == session_storage);
    centered_absolute = absolute;
    centered_value = value;
    centers++;
    return 0;
}

void *session_construct(void *session, const char *name)
{
    assert(!strcmp(name, "CoverGrid"));
    return session;
}

void *session_destroy(void *session)
{
    assert(session == session_storage);
    destroyed++;
    return session;
}

void view_init_options(const void *descriptor, void *options)
{
    (void)descriptor;
    for (unsigned i = 0; i < 0x5c / 4; i++)
        assert(!((uint32_t *)options)[i]);
}

void *view_new(uint32_t bytes)
{
    assert(bytes == sizeof(view_storage));
    return view_storage;
}

void *view_construct(void *view, void *owner, void *parent, void *target,
                     const void *options)
{
    (void)owner;
    (void)parent;
    (void)target;
    (void)options;
    return view;
}

void *native_view_destroy(void *view)
{
    native_destroys++;
    return view;
}

void browser_album_name(void *value, uint32_t index, void *out)
{
    assert(value == &browser);
    struct {
        uint16_t units;
        uint16_t text[255];
    } *name = out;
    char text[32];
    int length = snprintf(text, sizeof(text), "Album %u", index);
    for (int i = 0; i < length; i++)
        name->text[i] = (uint16_t)text[i];
    name->units = (uint16_t)length;
}

void *osos_model_ref_get(void *reference)
{
    return *(void **)reference;
}

void osos_model_ref_release(void *reference)
{
    assert(*(void **)reference == &model);
    released++;
}

void osos_model_ref_init(void **reference, void *value, int flags)
{
    (void)flags;
    *reference = value;
}

void osos_model_ref_assign(void *reference, void *source)
{
    (void)reference;
    (void)source;
    unreachable();
}

void *osos_model_create(uint32_t ownership)
{
    (void)ownership;
    unreachable();
    return 0;
}

void osos_native_string_init(struct osos_native_string *string)
{
    string->data = calloc(256, 1);
}

void osos_native_string_destroy(struct osos_native_string *string)
{
    free(string->data);
}

const char *osos_native_string_data(struct osos_native_string *string)
{
    return string->data;
}

void osos_native_string_assign_utf16(struct osos_native_string *string,
                                     const uint16_t *text, int32_t units)
{
    for (int32_t i = 0; i < units; i++)
        string->data[i] = (char)text[i];
    string->data[units] = 0;
}

void osos_model_album_subtitle(void *value, int index, struct osos_native_string *out)
{
    assert(value == &model);
    snprintf(out->data, 256, "Artist %d", index);
}

void cfw_ui_notify(uint32_t property)
{
    assert(property == CFW_CoverGridView_Caption);
    notifications++;
}

uintptr_t osos_ui_bitmap(uint32_t id)
{
    assert(id == 0x0dad0128);
    bitmap_loads++;
    unsigned char *bytes = (unsigned char *)bitmap;
    *(uint16_t *)(bytes + 4) = 256;
    *(uint16_t *)(bytes + 6) = bitmap_usable ? 16 : 32;
    *(uint32_t *)(bytes + 0x10) = 128;
    *(uint32_t *)(bytes + 0x14) = 128;
    return (uintptr_t)bitmap;
}

void *osos_import_alloc(uint32_t bytes)
{
    if (bytes == OSOS_ARTWORK_SESSION_BYTES)
        return session_storage;
    void *allocation = malloc(bytes);
    memset(allocation, 0xa5, bytes);
    return allocation;
}

void osos_import_free(void *allocation)
{
    if (allocation != session_storage)
        free(allocation);
    freed++;
}

void osos_ui_invalidate(void)
{
    invalidations++;
}

void osos_wheel_decode(void *event)
{
    assert(((uint32_t *)event)[1] == 0x15);
}

void *osos_wheel_accumulator_init(void *state, int threshold, unsigned char accelerate,
                                  uint32_t interval)
{
    assert(threshold == 5 && accelerate == 1 && interval == 350);
    return state;
}

void osos_wheel_accumulator_reset(void *state)
{
    (void)state;
}

/* The native accumulator's threshold is covered by the EQ canvas test; here each
 * motion unit is one step. */
int osos_wheel_accumulator_step(void *state, int motion, uint32_t speed)
{
    (void)state;
    (void)speed;
    return motion;
}

uint32_t osos_button_ui_key(void *event)
{
    return ((uint32_t *)event)[4] & 0xffff;
}

int osos_controller_handle_event(void *controller, void *event)
{
    (void)controller;
    (void)event;
    delegated++;
    return 77;
}

uint32_t osos_controller_activate(void *controller, uint32_t entering)
{
    (void)controller;
    return entering;
}

uint32_t osos_controller_deactivate(void *controller, uint32_t leaving)
{
    (void)controller;
    return leaving;
}

void *osos_controller_create_screen(const char *name, uint32_t screen, uint32_t layout,
                                    uint32_t transition)
{
    (void)name;
    (void)screen;
    (void)layout;
    (void)transition;
    unreachable();
    return 0;
}

void *osos_controller_registry(void)
{
    return 0;
}

void *osos_create_screen_controller(void *registry, uint32_t screen, uint32_t layout,
                                    uint32_t transition)
{
    (void)registry;
    (void)screen;
    (void)layout;
    (void)transition;
    unreachable();
    return 0;
}

void osos_controller_push(void *controller, void *screen)
{
    (void)controller;
    (void)screen;
    unreachable();
}

void osos_ui_register_controllers(void *registry, const char *name,
                                  osos_controller_factory ordinary,
                                  osos_controller_factory resource)
{
    (void)registry;
    (void)name;
    (void)ordinary;
    (void)resource;
}

void osos_graphics_set_mode(void *context, unsigned char mode)
{
    (void)context;
    (void)mode;
    unreachable();
}

void osos_graphics_set_color(void *context, const unsigned char *color)
{
    (void)context;
    (void)color;
    unreachable();
}

void osos_graphics_fill_rect_unmapped(void *context, const int32_t *rect)
{
    (void)context;
    (void)rect;
    unreachable();
}

int cfw_dark_mode_enabled(void)
{
    return 0;
}

#include "../../../payload/cover_grid_view.c"

/* The plain box filter: each output pixel averages the source pixels it covers. */
static void reference_downscale(uint16_t *out, const unsigned char *pixels,
                                uint32_t row_bytes)
{
    for (int y = 0; y < CFW_GRID_COVER; y++) {
        int y0 = y * SOURCE_SIZE / CFW_GRID_COVER;
        int y1 = ((y + 1) * SOURCE_SIZE + CFW_GRID_COVER - 1) / CFW_GRID_COVER;
        for (int x = 0; x < CFW_GRID_COVER; x++) {
            int x0 = x * SOURCE_SIZE / CFW_GRID_COVER;
            int x1 = ((x + 1) * SOURCE_SIZE + CFW_GRID_COVER - 1) / CFW_GRID_COVER;
            uint32_t r = 0, g = 0, b = 0, n = 0;
            for (int sy = y0; sy < y1; sy++) {
                const uint16_t *row = (const uint16_t *)(pixels + sy * row_bytes);
                for (int sx = x0; sx < x1; sx++) {
                    r += row[sx] >> 11;
                    g += (row[sx] >> 5) & 63;
                    b += row[sx] & 31;
                    n++;
                }
            }
            out[y * CFW_GRID_COVER + x] = (uint16_t)(((r + n / 2) / n) << 11 |
                                                     ((g + n / 2) / n) << 5 |
                                                     (b + n / 2) / n);
        }
    }
}

static void check_pixel_math(void)
{
    for (uint32_t n = 4; n <= 9; n++)
        for (uint32_t sum = 0; sum <= n * 63; sum++)
            assert(divide_rounded(sum, n) == (sum + n / 2) / n);
    for (uint32_t value = 0; value < 65535; value++)
        assert(divide_255(value) == value / 255);
    uint32_t table[512];
    fill_expand_table(table);
    for (uint32_t p = 0; p < 65536; p++)
        assert((table[p >> 8] | table[256 + (p & 0xff)]) == (0xff000000u | expand_565(p)));
}

static void check_downscale(void)
{
    static const uint16_t colors[] = {0x0000, 0xffff, 0xf800, 0x07e0, 0x001f, 0x8410};
    uint16_t source[SOURCE_PIXELS], out[CFW_GRID_COVER * CFW_GRID_COVER];
    for (unsigned c = 0; c < sizeof(colors) / sizeof(*colors); c++) {
        for (unsigned i = 0; i < SOURCE_PIXELS; i++)
            source[i] = colors[c];
        downscale(out, (const unsigned char *)source, 256);
        for (unsigned i = 0; i < CFW_GRID_COVER * CFW_GRID_COVER; i++)
            assert(out[i] == colors[c]);
    }
    /* Black left half, white right half: the edges keep their color, and the
     * column straddling the seam averages to gray. */
    for (unsigned y = 0; y < 128; y++)
        for (unsigned x = 0; x < 128; x++)
            source[y * 128 + x] = x < 64 ? 0x0000 : 0xffff;
    downscale(out, (const unsigned char *)source, 256);
    assert(out[0] == 0x0000 && out[CFW_GRID_COVER - 1] == 0xffff);
    uint16_t seam = out[38];
    assert(seam != 0x0000 && seam != 0xffff);
    assert((seam >> 11) == ((seam & 31)) && (seam >> 11) > 8 && (seam >> 11) < 24);

    /* Noise, in rows padded past the 128 pixels, matches the plain filter exactly. */
    static uint16_t padded[SOURCE_SIZE * 132];
    uint16_t expected[CFW_GRID_COVER * CFW_GRID_COVER];
    uint32_t seed = 1;
    for (int image = 0; image < 8; image++) {
        for (unsigned i = 0; i < sizeof(padded) / sizeof(*padded); i++) {
            seed = seed * 1103515245u + 12345u;
            padded[i] = (uint16_t)(seed >> 8);
        }
        downscale(out, (const unsigned char *)padded, 132 * 2);
        reference_downscale(expected, (const unsigned char *)padded, 132 * 2);
        assert(!memcmp(out, expected, sizeof(out)));
    }
}

static void check_cache(void)
{
    int budget = 1, pending = 0;
    for (unsigned i = 0; i < CACHE_SLOTS + 1; i++)
        assert(grid.cache[i].state == SLOT_EMPTY);

    /* Covers the worker has not loaded yet draw the placeholder without pending. */
    assert(!cover(0, &budget, &pending) && budget == 1 && !pending);
    loaded[0] = loaded[1] = 1;
    struct cover_slot *first = cover(0, &budget, &pending);
    assert(first && first->artwork == albums[0].artwork && budget == 0 && !pending);
    assert(first->pixels[0] == sources[0][0]);
    /* Over budget, a loaded cover waits for the next frame. */
    assert(!cover(1, &budget, &pending) && pending);
    /* Cached covers draw without budget or another lookup. */
    pending = 0;
    int before = lookups;
    assert(cover(0, &budget, &pending) == first && !pending && lookups == before);
    /* Albums without artwork never look up the session. */
    assert(!cover(5, &budget, &pending) && lookups == before);

    /* Fill every slot, touch all but album 1, then load one more: album 1's slot,
     * the least recently used, is the one replaced. */
    for (unsigned i = 0; i < ALBUMS; i++)
        loaded[i] = 1;
    budget = 100;
    int filled[CACHE_SLOTS], count = 0;
    for (int index = 0; count < CACHE_SLOTS; index++)
        if (albums[index].artwork) {
            assert(cover(index, &budget, &pending));
            filled[count++] = index;
        }
    for (int i = 0; i < CACHE_SLOTS; i++)
        if (filled[i] != 1)
            assert(cover(filled[i], &budget, &pending));
    struct cover_slot *one = 0;
    for (unsigned i = 0; i < CACHE_SLOTS; i++)
        if (grid.cache[i].artwork == albums[1].artwork)
            one = &grid.cache[i];
    assert(one);
    int next = filled[CACHE_SLOTS - 1] + 1;
    assert(cover(next, &budget, &pending) == one && one->artwork == albums[next].artwork);
    before = lookups;
    for (int i = 0; i < CACHE_SLOTS; i++)
        if (filled[i] != 1)
            assert(cover(filled[i], &budget, &pending));
    assert(lookups == before);
}

static void check_placeholder(void)
{
    /* An unusable resource falls back to the plain tile and is retried. */
    assert(!placeholder() && bitmap_loads == 1);
    bitmap_usable = 1;
    uint16_t *pixels = (uint16_t *)((unsigned char *)bitmap + 0x1c);
    for (unsigned i = 0; i < SOURCE_PIXELS; i++)
        pixels[i] = 0x8410;
    struct cover_slot *slot = placeholder();
    assert(slot && slot->pixels[0] == 0x8410 && bitmap_loads == 2);
    /* Downscaled once for the life of the cache, however many frames draw it. */
    for (int frame = 0; frame < 5; frame++)
        assert(placeholder() == slot);
    assert(bitmap_loads == 2);
}

static int wheel(int delta)
{
    uint32_t event[] = {0, OSOS_EVENT_WHEEL, 0, (uint32_t)delta, 0};
    return grid_event(0, event);
}

static void check_input(void)
{
    cfw_wheel_reset(&grid_wheel, 1);
    int before = invalidations;
    /* Moving past the second full row scrolls by one row and moves the session. */
    assert(wheel(9) == 1);
    assert(grid.selected == 9 && grid.top == 1);
    assert(centers == 1 && centered_absolute == 0 && centered_value == 4);
    assert(!strcmp(caption, "Album 9 \xc2\xb7 Artist 9") && notifications == 1);
    assert(invalidations == before + 1);
    /* Selection clamps to the library at both ends. */
    assert(wheel(-100) == 1 && grid.selected == 0 && grid.top == 0);
    assert(centered_value == -4);
    assert(wheel(1000) == 1 && grid.selected == ALBUMS - 1);
    assert(grid.top == (ALBUMS - 1) / CFW_GRID_COLUMNS - CFW_GRID_FULL_ROWS + 1);
    before = invalidations;
    int notified = notifications;
    assert(wheel(1) == 1 && invalidations == before && notifications == notified);
    /* Moving within the visible rows does not move the session and redraws only
     * the two covers with their rings, in window coordinates. */
    int centered = centers;
    before = invalidations;
    assert(wheel(-1) == 1 && centers == centered && invalidations == before + 2);
    int row = grid.selected / CFW_GRID_COLUMNS - grid.top;
    int column = grid.selected % CFW_GRID_COLUMNS;
    assert(invalidated[0] == 23 + 1 + row * CFW_GRID_PITCH);
    assert(invalidated[1] == 1 + column * CFW_GRID_PITCH);
    assert(invalidated[2] - invalidated[0] == RING_SIZE);
    assert(invalidated[3] - invalidated[1] == RING_SIZE);

    uint32_t press[] = {0, OSOS_EVENT_BUTTON_PRESS, 0, 0, OSOS_KEY_CENTER};
    assert(grid_event(0, press) == 77 && delegated == 1);
    uint32_t menu[] = {0, OSOS_EVENT_BUTTON_RELEASE, 0, 0, OSOS_KEY_MENU};
    assert(grid_event(0, menu) == 77 && delegated == 2);

    /* Finished covers redraw only while visible. */
    int first = grid.top * CFW_GRID_COLUMNS;
    before = invalidations;
    cover_loaded(0, 0, first - 1);
    cover_loaded(0, 0, first + VISIBLE_COVERS);
    assert(invalidations == before);
    cover_loaded(0, 0, first);
    cover_loaded(0, 0, first + VISIBLE_COVERS - 1);
    assert(invalidations == before + 2);
}

static void check_list_position(void)
{
    /* Other screens' transitions leave the pending Songs list alone. */
    grid.songs_pushed = 1;
    assert(cfw_cover_grid_restore_positions(&ui, 0x0dad0001, 7) == 0x0dad0001);
    assert(grid.songs_pushed && saved_songs[2] == 9 && restores == 1);
    /* The Songs screen the grid pushed restores at its first row, once; later
     * transitions, such as returning from Now Playing, keep the saved row. */
    assert(cfw_cover_grid_restore_positions(&ui, OSOS_SONGS_SCREEN, 7) ==
           OSOS_SONGS_SCREEN);
    assert(!grid.songs_pushed && saved_songs[2] == 0 && restores == 2);
    saved_songs[2] = 5;
    cfw_cover_grid_restore_positions(&ui, OSOS_SONGS_SCREEN, 7);
    assert(saved_songs[2] == 5 && restores == 3);
}

static void check_scrollbar(void)
{
    /* Native geometry: art 3 px longer than the thumb, the track inside the covers'
     * area; a two-row library does not scroll. */
    int32_t y0, y1;
    int height = 217 - CFW_GRID_CAPTION;
    assert(!scrollbar_span(8, 0, height, 16, &y0, &y1));
    assert(scrollbar_span(ALBUMS, 0, height, 16, &y0, &y1) && y0 == 0);
    int length = y1 - y0 - 3;
    assert(length >= 16 && length < height - 3);
    int last = (ALBUMS + 3) / 4 - CFW_GRID_FULL_ROWS;
    assert(scrollbar_span(ALBUMS, last, height, 16, &y0, &y1) && y1 == height);
    /* Long libraries keep the minimum thumb. */
    assert(scrollbar_span(4000, 3, height, 16, &y0, &y1) && y1 - y0 == 19);

    /* Scrolling a row shows the bar and registers the frame client once. */
    scrollbar_stop();
    int adds = frame_adds, removes = frame_removes;
    grid.selected = grid.top = 0;
    assert(!grid.scrollbar_ticking && !scrollbar_alpha());
    wheel(8);
    assert(grid.top == 1 && grid.scrollbar_ticking && frame_adds == adds + 1);
    assert(frame_client == &grid_client && scrollbar_alpha() == 255);
    wheel(4);
    assert(frame_adds == adds + 1 && grid.scrolled_ms == now_ms);
    /* Moving within the visible rows does not show it again. */
    now_ms += 1000;
    wheel(-1);
    assert(grid.scrolled_ms == now_ms - 1000);
    /* Held 1.5 s, then faded linearly over 0.5 s. */
    now_ms = grid.scrolled_ms + 1499;
    assert(scrollbar_alpha() == 255);
    now_ms = grid.scrolled_ms + 1750;
    assert(scrollbar_alpha() == 128);
    /* Each change of alpha redraws; the drawn alpha settles it. */
    int before = invalidations;
    grid.scrollbar_drawn = 255;
    grid.scrollbar_width = 7;
    grid_tick(0);
    assert(invalidations == before + 1);
    /* Only the scrollbar's column above the caption bar redraws. */
    assert(invalidated[0] == 23 && invalidated[2] == 240 - CFW_GRID_CAPTION);
    assert(invalidated[1] == 320 - 7 && invalidated[3] == 320);
    grid.scrollbar_drawn = 128;
    grid_tick(0);
    assert(invalidations == before + 1);
    now_ms = grid.scrolled_ms + 2000;
    assert(scrollbar_alpha() == 0);
    scrollbar_stop();
    assert(!grid.scrollbar_ticking && frame_removes == removes + 1 && !frame_client);
    scrollbar_stop();
    assert(frame_removes == removes + 1);
}

static void check_late_covers(void)
{
    /* Covers loaded beyond a frame's budget redraw from the frame client, before
     * the next frame; it unregisters once nothing is left. */
    grid.selected = grid.top = 0;
    int before = invalidations, removes = frame_removes;
    grid.late = 1u << 0 | 1u << 5;
    update_client();
    assert(frame_client == &grid_client);
    grid_tick(0);
    assert(invalidations == before + 2 && !grid.late);
    assert(invalidated[0] == 23 + 1 + CFW_GRID_PITCH && invalidated[1] == 1 + CFW_GRID_PITCH);
    assert(!frame_client && frame_removes == removes + 1);
}

static void check_release(void)
{
    /* Leaving stops the worker but keeps the covers while the view still draws its
     * transition. */
    grid_deactivate(0, 0);
    assert(stops == 1 && grid.release_pending && frame_client == &grid_client);
    now_ms += RELEASE_DELAY_MS - 1;
    grid_tick(0);
    assert(grid.session && grid.cache && !destroyed);
    /* Returning in time cancels the release. */
    grid_activate(0, 1);
    assert(!grid.release_pending && !frame_client && starts == 2);
    now_ms += RELEASE_DELAY_MS;
    assert(grid.session && grid.cache);

    /* A draw postpones it; once the view has not drawn for the delay, the session
     * and covers are freed and the client unregisters. */
    grid_deactivate(0, 0);
    now_ms += RELEASE_DELAY_MS - 1;
    grid.drawn_ms = now_ms;
    grid_tick(0);
    assert(grid.session && grid.cache);
    now_ms += RELEASE_DELAY_MS;
    grid_tick(0);
    assert(!grid.session && !grid.cache && !grid.release_pending && !frame_client);
    assert(destroyed == 1 && freed == 2 && !released && grid.view);

    /* Drawing and activation allocate them again. */
    allocate_cache();
    grid_activate(0, 1);
    assert(grid.cache && grid.session == session_storage && starts == 3);
    destroyed = freed = 0;
}

static void check_lifetime(void *view)
{
    /* A view from an earlier grid, still in its pop transition, leaves the live
     * grid's state alone. */
    unsigned char stale[0xa4];
    grid_destroy(stale);
    assert(grid.session && grid.cache && !destroyed && !released && native_destroys == 1);
    grid_destroy(view);
    assert(!grid.session && !grid.cache && !grid.view && !grid.reference);
    assert(destroyed == 1 && released == 1 && freed == 2 && native_destroys == 2);
}

int main(void)
{
    for (unsigned i = 0; i < ALBUMS; i++)
        albums[i].artwork = i == 5 ? 0 : 100 + i;
    for (unsigned i = 0; i < SOURCE_PIXELS; i++) {
        sources[0][i] = 0xf800;
        sources[1][i] = 0x001f;
    }
    browser.begin = albums;
    browser.end = albums + ALBUMS;
    model.browser = &browser;
    grid.reference = &model;

    check_pixel_math();
    check_downscale();
    void *view = grid_create(0, 0, 0, 0);
    /* The native UI builds the view at boot; only drawing allocates the cache. */
    assert(view == view_storage && grid.view == view && !grid.cache && !grid.session);
    assert(*(const uintptr_t **)view == grid_vtable + CFW_VTABLE_PREFIX / 4);
    allocate_cache();
    assert(grid.cache);
    /* The view's frame in window coordinates, below the title bar. */
    const int32_t frame[4] = {23, 0, 240, 320};
    memcpy(view_storage + 0x80, frame, sizeof(frame));
    /* Activation builds the session and centers it on the first visible album. */
    grid_activate(0, 1);
    assert(grid.session == session_storage && starts == 1 && centers == 1);
    assert(centered_absolute == 1 && centered_value == 0);
    centers = 0;
    check_cache();
    check_placeholder();
    check_input();
    check_list_position();
    check_scrollbar();
    check_late_covers();
    check_release();
    check_lifetime(view);

    puts("Cover Grid view: pixel math, downscale, cover cache, placeholder, input, "
         "redraws, release and lifetime passed");
    return 0;
}
