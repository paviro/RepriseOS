/* SPDX-License-Identifier: GPL-3.0-only */

/* Native input events, View and resource-screen controller ABI shared by payload
 * screens. Host fixtures may replace osos.h and patch.h by defining their guards. */
#ifndef CFW_SCREEN_H
#define CFW_SCREEN_H
#include <stdint.h>
#include "osos.h"
#include "patch.h"

/* Event type at word 1 and button UI keys, as the native game input bridge maps them. */
enum {
    OSOS_EVENT_BUTTON_PRESS = 6,
    OSOS_EVENT_BUTTON_RELEASE = 0x0d,
    OSOS_EVENT_WHEEL = 0x15,
    OSOS_KEY_PLAY = 0x10,
    OSOS_KEY_PREVIOUS = 0x59,
    OSOS_KEY_NEXT = 0x5a,
    OSOS_KEY_MENU = 0x6e,
    OSOS_KEY_CENTER = 0x6f,
};

enum {
    /* RTTI words before each vtable's address point. */
    CFW_VTABLE_PREFIX = 12,
    CFW_VIEW_BYTES = 0xa4,
    CFW_VIEW_OPTIONS_BYTES = 0x5c,
    CFW_VIEW_VTABLE_BYTES = 0x104,
    CFW_VIEW_DESTROY_SLOT = 4,
    CFW_VIEW_DRAW_SLOT = 0x74,
    CFW_CONTROLLER_VTABLE_BYTES = 0x128,
    CFW_CONTROLLER_EVENT_SLOT = 0x30,
    CFW_CONTROLLER_ACTIVATE_SLOT = 0xec,
    CFW_CONTROLLER_DEACTIVATE_SLOT = 0xf0,
};

#define CFW_VTABLE_WORD(slot) ((CFW_VTABLE_PREFIX + (slot)) / 4)

/* A View vtable overriding draw; the rest, ABI metadata included, is copied from the
 * native View at link time. */
#define CFW_VIEW_VTABLE(name, draw)                                                    \
    static const uintptr_t name[(CFW_VTABLE_PREFIX + CFW_VIEW_VTABLE_BYTES) / 4] = {   \
        [CFW_VTABLE_WORD(CFW_VIEW_DRAW_SLOT)] = (uintptr_t)(draw),                     \
    };                                                                                 \
    PATCH_COPY(name, 0, OSOS_VIEW_VTABLE - CFW_VTABLE_PREFIX,                          \
               CFW_VTABLE_PREFIX + CFW_VIEW_DRAW_SLOT);                                \
    PATCH_COPY(name, CFW_VTABLE_PREFIX + CFW_VIEW_DRAW_SLOT + 4,                       \
               OSOS_VIEW_VTABLE + CFW_VIEW_DRAW_SLOT + 4,                              \
               CFW_VIEW_VTABLE_BYTES - CFW_VIEW_DRAW_SLOT - 4)

/* A View vtable overriding destroy and draw. */
#define CFW_VIEW_VTABLE_DESTROY(name, destroy, draw)                                   \
    static const uintptr_t name[(CFW_VTABLE_PREFIX + CFW_VIEW_VTABLE_BYTES) / 4] = {   \
        [CFW_VTABLE_WORD(CFW_VIEW_DESTROY_SLOT)] = (uintptr_t)(destroy),               \
        [CFW_VTABLE_WORD(CFW_VIEW_DRAW_SLOT)] = (uintptr_t)(draw),                     \
    };                                                                                 \
    PATCH_COPY(name, 0, OSOS_VIEW_VTABLE - CFW_VTABLE_PREFIX,                          \
               CFW_VTABLE_PREFIX + CFW_VIEW_DESTROY_SLOT);                             \
    PATCH_COPY(name, CFW_VTABLE_PREFIX + CFW_VIEW_DESTROY_SLOT + 4,                    \
               OSOS_VIEW_VTABLE + CFW_VIEW_DESTROY_SLOT + 4,                           \
               CFW_VIEW_DRAW_SLOT - CFW_VIEW_DESTROY_SLOT - 4);                        \
    PATCH_COPY(name, CFW_VTABLE_PREFIX + CFW_VIEW_DRAW_SLOT + 4,                       \
               OSOS_VIEW_VTABLE + CFW_VIEW_DRAW_SLOT + 4,                              \
               CFW_VIEW_VTABLE_BYTES - CFW_VIEW_DRAW_SLOT - 4)

/* A resource-screen controller vtable overriding event, activate and deactivate; the
 * RTTI prefix, lifecycle and deleting destructor are copied from the native one. */
#define CFW_CONTROLLER_VTABLE(name, event, activate, deactivate)                       \
    static const uintptr_t name[(CFW_VTABLE_PREFIX + CFW_CONTROLLER_VTABLE_BYTES) /    \
                                4] = {                                                 \
        [CFW_VTABLE_WORD(CFW_CONTROLLER_EVENT_SLOT)] = (uintptr_t)(event),             \
        [CFW_VTABLE_WORD(CFW_CONTROLLER_ACTIVATE_SLOT)] = (uintptr_t)(activate),       \
        [CFW_VTABLE_WORD(CFW_CONTROLLER_DEACTIVATE_SLOT)] = (uintptr_t)(deactivate),   \
    };                                                                                 \
    PATCH_COPY(name, 0, OSOS_CONTROLLER_SCREEN_VTABLE - CFW_VTABLE_PREFIX,             \
               CFW_VTABLE_PREFIX + CFW_CONTROLLER_EVENT_SLOT);                         \
    PATCH_COPY(name, CFW_VTABLE_PREFIX + CFW_CONTROLLER_EVENT_SLOT + 4,                \
               OSOS_CONTROLLER_SCREEN_VTABLE + CFW_CONTROLLER_EVENT_SLOT + 4,          \
               CFW_CONTROLLER_ACTIVATE_SLOT - CFW_CONTROLLER_EVENT_SLOT - 4);          \
    PATCH_COPY(name, CFW_VTABLE_PREFIX + CFW_CONTROLLER_DEACTIVATE_SLOT + 4,           \
               OSOS_CONTROLLER_SCREEN_VTABLE + CFW_CONTROLLER_DEACTIVATE_SLOT + 4,     \
               CFW_CONTROLLER_VTABLE_BYTES - CFW_CONTROLLER_DEACTIVATE_SLOT - 4)

/* Points an object at a vtable declared by the macros above. */
static inline void cfw_set_vtable(void *object, const uintptr_t *vtable)
{
    *(const uintptr_t **)object = vtable + CFW_VTABLE_PREFIX / 4;
}

/* Builds a native View from its layout descriptor and gives it vtable. */
static inline void *cfw_view_create(void *parent, void *event_target,
                                    const void *descriptor, const uintptr_t *vtable)
{
    uint32_t options[CFW_VIEW_OPTIONS_BYTES / 4];
    typedef void (*options_fn)(const void *, void *);
    typedef void *(*new_fn)(uint32_t);
    typedef void *(*construct_fn)(void *, void *, void *, void *, const void *);
    for (unsigned i = 0; i < CFW_VIEW_OPTIONS_BYTES / 4; i++)
        options[i] = 0;
    ((options_fn)OSOS_VIEW_INIT_OPTIONS)(descriptor, options);
    void *view = ((new_fn)OSOS_CPP_NEW)(CFW_VIEW_BYTES);
    view = ((construct_fn)OSOS_VIEW_CONSTRUCT)(view, 0, parent, event_target, options);
    cfw_set_vtable(view, vtable);
    return view;
}

/* A native wheel accumulator with list input's steady five-count threshold. The
 * accumulator registers itself, so its storage must last for the UI lifetime. */
struct cfw_wheel {
    uint32_t state[0x34 / 4];
    int initialized;
};

static inline void cfw_wheel_reset(struct cfw_wheel *wheel, unsigned char accelerate)
{
    if (!wheel->initialized) {
        osos_wheel_accumulator_init(wheel->state, 5, accelerate, 350);
        wheel->initialized = 1;
    }
    osos_wheel_accumulator_reset(wheel->state);
}

static inline int cfw_wheel_step(struct cfw_wheel *wheel, void *event)
{
    osos_wheel_decode(event);
    return osos_wheel_accumulator_step(wheel->state, (int32_t)((uint32_t *)event)[3], 0);
}

static inline int cfw_clamp(int64_t value, int low, int high)
{
    return value < low ? low : value > high ? high : (int)value;
}
#endif
