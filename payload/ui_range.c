/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui.h"
#include "osos.h"
#include "patch.h"
#include "screen.h"

static struct cfw_wheel range_wheel;

static PATCH_ARM uint32_t range_activate(void *controller, uint32_t entering)
{
    cfw_wheel_reset(&range_wheel, 0);
    return osos_controller_activate(controller, entering);
}

static PATCH_ARM int range_event(void *controller, void *event)
{
    const uint32_t *words = event;
    int delta;
    if (words[1] == OSOS_EVENT_WHEEL) {
        delta = cfw_wheel_step(&range_wheel, event);
    } else if (words[1] == OSOS_EVENT_BUTTON_PRESS || words[1] == 7 || words[1] == 8 ||
               words[1] == OSOS_EVENT_BUTTON_RELEASE) {
        uint32_t key = osos_button_ui_key(event);
        if (key != OSOS_KEY_PREVIOUS && key != OSOS_KEY_NEXT)
            return osos_controller_handle_event(controller, event);
        if (words[1] != OSOS_EVENT_BUTTON_RELEASE)
            return 1;
        delta = key == OSOS_KEY_PREVIOUS ? -1 : 1;
        osos_wheel_accumulator_reset(range_wheel.state);
    } else {
        return osos_controller_handle_event(controller, event);
    }
    uint32_t screen = *(uint32_t *)((unsigned char *)controller + 0xb4);
    for (uint32_t i = 0; i < cfw_ui_field_count; i++) {
        const struct cfw_ui_field *field = cfw_ui_fields[i];
        if (field->screen != screen || !field->numeric)
            continue;
        int64_t value = (int64_t)field->model->values[field->slot] + delta;
        if (value < 0)
            value = 0;
        if (value >= field->count)
            value = field->count - 1;
        if ((uint32_t)value != field->model->values[field->slot])
            field->model->change(field->slot, (uint32_t)value);
        break;
    }
    return 1;
}

static PATCH_ARM uint32_t range_deactivate(void *controller, uint32_t leaving)
{
    uint32_t screen = *(uint32_t *)((unsigned char *)controller + 0xb4);
    for (uint32_t i = 0; i < cfw_ui_field_count; i++)
        if (cfw_ui_fields[i]->screen == screen)
            cfw_ui_fields[i]->model->commit();

    return osos_controller_deactivate(controller, leaving);
}

CFW_CONTROLLER_VTABLE(range_vtable, range_event, range_activate, range_deactivate);

static PATCH_ARM void *range_create(const char *name, uint32_t screen, uint32_t layout,
                                    uint32_t transition)
{
    unsigned char *controller =
        osos_controller_create_screen(name, screen, layout, transition);
    cfw_set_vtable(controller, range_vtable);
    return controller;
}

void cfw_ui_register_controllers(void)
{
    void *registry = osos_controller_registry();
    osos_ui_register_controllers(registry, "CFWRange", 0, range_create);
}
