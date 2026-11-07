/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include "../../../payload/native/bindings.h"
static int native_region(void);
#undef OSOS_VOLUME_POLICY_REGION
#define OSOS_VOLUME_POLICY_REGION ((uintptr_t)native_region)
/* Native layout assertions assume 32-bit pointers; this fixture runs on the host. */
#define _Static_assert(...)
#include "../../../payload/osos.h"
#undef _Static_assert
#include "volume_warning_ui.h"
#define CFW_PATCH_H
#define PATCH_ARM

static uint32_t stored_record;
static int stored, store_sets, notifications, region = 1;
static int controller, model;

static int native_region(void)
{
    return region;
}

#include "../../../payload/store.h"

uint32_t cfw_store_get(uint32_t key, void *data, uint32_t capacity)
{
    assert(key == CFW_STORE_VOLUME_WARNING && capacity == 4);
    if (!stored)
        return 0;
    memcpy(data, &stored_record, 4);
    return 4;
}

void cfw_store_set(uint32_t key, const void *data, uint32_t size)
{
    assert(key == CFW_STORE_VOLUME_WARNING && size == 4);
    memcpy(&stored_record, data, 4);
    stored = 1;
    store_sets++;
}

static void *settings_model(void *value)
{
    assert(value == &controller);
    return &model;
}

static uintptr_t on_off(int value)
{
    return value ? 2 : 1;
}

static void notify(void *value, uint32_t type, uint32_t property)
{
    assert(value == &model && type == OSOS_NOTIFY_ALL &&
           property == CFW_VolumeWarning_Value);
    notifications++;
}

#define osos_settings_model settings_model
#define osos_settings_on_off on_off
#define osos_model_notify notify
#include "../../../payload/volume_warning.c"

static uintptr_t value(void)
{
    uintptr_t out = 0;
    assert(cfw_volume_warning_property(OSOS_PROPERTY_STRING, CFW_VolumeWarning_Value,
                                       &out));
    return out;
}

int main(void)
{
    uintptr_t out;
    assert(!cfw_volume_warning_property(OSOS_PROPERTY_COUNT, CFW_VolumeWarning_Value,
                                        &out));
    assert(!cfw_volume_warning_action(&controller, "stock", 0));
    assert(cfw_volume_warning_action(&controller, "CFW_VolumeWarning_Toggle",
                                     OSOS_ACTION_SUPPORT_QUERY));
    assert(!store_sets && !notifications);

    /* A missing record keeps Apple's warning. */
    cfw_volume_warning_load();
    assert(value() == 2 && cfw_volume_warning_active());
    region = 0;
    assert(!cfw_volume_warning_active());
    region = 1;

    assert(cfw_volume_warning_action(&controller, "CFW_VolumeWarning_Toggle", 0));
    assert(stored_record == VOLUME_WARNING_OFF_RECORD && store_sets == 1 &&
           notifications == 1);
    assert(value() == 1 && !cfw_volume_warning_active());

    cfw_volume_warning_load();
    assert(value() == 1);
    assert(cfw_volume_warning_action(&controller, "CFW_VolumeWarning_Toggle", 0));
    assert(stored_record == VOLUME_WARNING_ON_RECORD && store_sets == 2 &&
           notifications == 2 && value() == 2);

    /* Only the Off record turns the warning off. */
    stored_record = 0;
    cfw_volume_warning_load();
    assert(value() == 2);
    stored_record = VOLUME_WARNING_OFF_RECORD;
    cfw_volume_warning_load();
    assert(value() == 1);
    stored = 0;
    cfw_volume_warning_load();
    assert(value() == 2);
    return 0;
}
