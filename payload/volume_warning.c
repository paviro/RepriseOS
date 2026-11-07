/* SPDX-License-Identifier: GPL-3.0-only */

#include "volume_warning.h"
#include "cfw.h"
#include "osos.h"
#include "patch.h"
#include "store.h"
#include "volume_warning_ui.h"

enum {
    VOLUME_WARNING_OFF_RECORD = 0x304e5756, /* VWN0 */
    VOLUME_WARNING_ON_RECORD = 0x314e5756,  /* VWN1 */
};

/* Apple's warning stays on unless the user turns it off. */
static int warning_enabled = 1;

void cfw_volume_warning_load(void)
{
    uint32_t record = 0;
    cfw_store_get(CFW_STORE_VOLUME_WARNING, &record, sizeof(record));
    warning_enabled = record != VOLUME_WARNING_OFF_RECORD;
}

/* Replaces the region test in Apple's warning acknowledgment check and in the
 * preferences loader's startup cap. A zero result reports the warning as
 * acknowledged, so every warning path skips the 168 stop, and keeps the saved volume
 * at startup. */
PATCH_ARM int cfw_volume_warning_active(void)
{
    typedef int (*fn)(void);
    return warning_enabled && ((fn)OSOS_VOLUME_POLICY_REGION)();
}

int cfw_volume_warning_property(uint32_t type, uint32_t property, uintptr_t *out)
{
    if (type != OSOS_PROPERTY_STRING || property != CFW_VolumeWarning_Value)
        return 0;
    *out = osos_settings_on_off(warning_enabled);
    return 1;
}

int cfw_volume_warning_action(void *controller, const char *action, uint32_t argument)
{
    if (!cfw_string_equal(action, "CFW_VolumeWarning_Toggle"))
        return 0;
    if (argument == OSOS_ACTION_SUPPORT_QUERY)
        return 1;

    warning_enabled = !warning_enabled;
    uint32_t record =
        warning_enabled ? VOLUME_WARNING_ON_RECORD : VOLUME_WARNING_OFF_RECORD;
    cfw_store_set(CFW_STORE_VOLUME_WARNING, &record, sizeof(record));
    osos_model_notify(osos_settings_model(controller), OSOS_NOTIFY_ALL,
                      CFW_VolumeWarning_Value);
    return 1;
}
