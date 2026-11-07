/* SPDX-License-Identifier: GPL-3.0-only */

#include "custom_eq.h"
#include "osos.h"
#include "cfw.h"
#include "preferences.h"
#include "patch.h"

/* Native preset index 23 / saved ID 122. Reuse Flat's preview. */
const struct {
    uint32_t name, preview;
} cfw_eq_presets[24] = {
    [23] = {CFW_EQ_PRESET_NAME, 0},
};

const uint32_t cfw_eq_preset_ids[24] = {[23] = 122};

PATCH_COPY(cfw_eq_presets, 0, OSOS_EQ_PRESETS, 23 * 8);
PATCH_COPY(cfw_eq_presets, 23 * 8 + 4, OSOS_EQ_PRESETS + 8 * 8 + 4, 4);
PATCH_COPY(cfw_eq_preset_ids, 0, OSOS_EQ_PRESET_IDS, 23 * 4);

static uint32_t settings[CFW_EQ_FIELDS];
static int initialized;
static int dirty;
static const struct osos_path settings_path = {0, "iPod_Control\\Device\\CFWEQ"};

struct eq_file {
    uint32_t magic, version;
    uint32_t values[CFW_EQ_FIELDS];
    uint32_t checksum;
};

enum { EQ_LEGACY_FIELDS = 13 };

struct legacy_eq_file {
    uint32_t magic, version;
    uint32_t values[EQ_LEGACY_FIELDS];
    uint32_t checksum;
};

static uint32_t checksum(const void *file, unsigned int size)
{
    const unsigned char *bytes = (const unsigned char *)file;
    uint32_t hash = 2166136261u;
    for (unsigned int i = 0; i < size - 4; i++)
        hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

static void defaults(void)
{
    for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
        settings[i] = cfw_eq_fields[i].default_value;
    cfw_eq_publish(settings);
    initialized = 1;
    dirty = 0;
}

void cfw_eq_load_preferences(void)
{
    struct eq_file file;
    defaults();
    if (!cfw_preferences_read(&settings_path, &file, 8) || file.magic != 0x51455743)
        return;
    if (file.version == 1 || file.version == 2) {
        struct legacy_eq_file legacy;
        if (!cfw_preferences_read(&settings_path, &legacy, sizeof(legacy)) ||
            legacy.checksum != checksum(&legacy, sizeof(legacy)))
            return;
        if (legacy.version == 1) {
            if (legacy.values[CFW_EQ_PRECUT] > 20)
                return;
            legacy.values[CFW_EQ_PRECUT] *= 5;
        }
        for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
            file.values[i] = i < EQ_LEGACY_FIELDS ? legacy.values[i] : settings[i];
    } else if ((file.version != 3 && file.version != 4) ||
               !cfw_preferences_read(&settings_path, &file, sizeof(file)) ||
               file.checksum != checksum(&file, sizeof(file))) {
        return;
    }
    /* Version 4 inserted Auto before the manual precut choices. */
    if (file.version < 4)
        file.values[CFW_EQ_PRECUT]++;
    for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
        if (file.values[i] >= cfw_eq_fields[i].count)
            return;
    if (!cfw_eq_publish(file.values))
        return;
    for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
        settings[i] = file.values[i];
}

static void save_preferences(void)
{
    struct eq_file file;
    file.magic = 0x51455743;
    file.version = 4;
    for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
        file.values[i] = settings[i];
    file.checksum = checksum(&file, sizeof(file));
    /* A failed write stays dirty and is retried by the next commit. */
    if (cfw_preferences_write(&settings_path, &file, sizeof(file)))
        dirty = 0;
}

static void change_value(uint32_t field, uint32_t value)
{
    if (!initialized)
        defaults();
    if (field >= CFW_EQ_FIELDS || value >= cfw_eq_fields[field].count ||
        settings[field] == value)
        return;
    uint32_t previous = settings[field];
    settings[field] = value;
    if (cfw_eq_publish(settings))
        dirty = 1;
    else
        settings[field] = previous;
}

static void commit_values(void)
{
    if (dirty)
        save_preferences();
}

const struct cfw_ui_model cfw_eq_model = {settings, change_value, commit_values};
