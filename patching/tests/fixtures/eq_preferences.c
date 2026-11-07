/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define CFW_OSOS_H
#define CFW_PATCH_H
#define PATCH_COPY(...)
#include "../../../payload/custom_eq.h"

struct osos_path {
    uint16_t volume;
    char text[258];
};

static unsigned char disk[128];
static uint32_t disk_size, writes;
static int write_failure, filter_failure;
const struct cfw_ui_field cfw_eq_fields[CFW_EQ_FIELDS] = {
    {.count = 102},
    {.count = 4},
    {.count = 32},
    {.count = 38},
    {.count = 49},
    {.count = 4},
    {.count = 32},
    {.count = 38},
    {.count = 49},
    {.count = 4},
    {.count = 32},
    {.count = 38},
    {.count = 49},
    {.count = 4},
    {.count = 32, .default_value = 12},
    {.count = 38, .default_value = 4},
    {.count = 49, .default_value = 24},
    {.count = 4},
    {.count = 32, .default_value = 24},
    {.count = 38, .default_value = 4},
    {.count = 49, .default_value = 24},
};

static int osos_file_open(const struct osos_path *path, int mode, void **handle)
{
    assert(!strcmp(path->text, "iPod_Control\\Device\\CFWEQ"));
    assert(mode == 1 || mode == 2);
    *handle = disk;
    return 0;
}

static int osos_file_read(void *handle, void *out, uint32_t size, uint32_t *count)
{
    assert(handle == disk);
    *count = disk_size < size ? disk_size : size;
    memcpy(out, disk, *count);
    return 0;
}

static int osos_file_write(void *handle, const void *data, uint32_t size,
                           uint32_t *count)
{
    assert(handle == disk);
    writes++;
    if (write_failure)
        return -1;
    assert(size <= sizeof(disk));
    memcpy(disk, data, size);
    disk_size = size;
    *count = size;
    return 0;
}

static void osos_file_close(void *handle)
{
    assert(handle == disk);
}

int cfw_eq_publish(const uint32_t values[CFW_EQ_FIELDS])
{
    for (unsigned i = 0; i < CFW_EQ_FIELDS; i++)
        assert(values[i] < cfw_eq_fields[i].count);
    return !filter_failure;
}

#include "../../../payload/custom_eq.c"

static void saved(uint32_t version, uint32_t precut)
{
    struct legacy_eq_file file = {.magic = 0x51455743, .version = version};
    file.values[0] = precut;
    file.values[4] = 24;
    file.checksum = checksum(&file, sizeof(file));
    memcpy(disk, &file, sizeof(file));
    disk_size = sizeof(file);
}

static void saved_v3(uint32_t precut)
{
    struct eq_file file = {.magic = 0x51455743, .version = 3};
    file.values[0] = precut;
    file.values[CFW_EQ_BAND5_GAIN] = 30;
    file.checksum = checksum(&file, sizeof(file));
    memcpy(disk, &file, sizeof(file));
    disk_size = sizeof(file);
}

int main(void)
{
    /* Older files predate Auto, so manual precut indices move up by one. */
    saved(1, 7);
    cfw_eq_load_preferences();
    assert(settings[0] == 36 && settings[4] == 24 && !writes && !dirty);
    saved(1, 21);
    cfw_eq_load_preferences();
    assert(settings[0] == 0);
    saved_v3(100);
    cfw_eq_load_preferences();
    assert(settings[0] == 101 && settings[CFW_EQ_BAND5_GAIN] == 30);
    saved(2, 37);
    cfw_eq_load_preferences();
    assert(settings[0] == 38);
    assert(settings[CFW_EQ_BAND4_TYPE] == 0 && settings[CFW_EQ_BAND5_TYPE] == 0);
    assert(settings[CFW_EQ_BAND4_GAIN] == 24 && settings[CFW_EQ_BAND5_GAIN] == 24);
    assert(settings[CFW_EQ_BAND4_FREQUENCY] == 12 &&
           settings[CFW_EQ_BAND5_FREQUENCY] == 24);
    cfw_eq_model.change(0, 39);
    assert(settings[0] == 39 && dirty && !writes);
    cfw_eq_model.change(CFW_EQ_FIELDS, 0);
    cfw_eq_model.change(0, 102);
    assert(settings[0] == 39);
    /* A failed write stays dirty so the next commit retries it. */
    write_failure = 1;
    cfw_eq_model.commit();
    assert(dirty && writes == 1);
    write_failure = 0;
    cfw_eq_model.commit();
    assert(!dirty && writes == 2);
    struct eq_file file;
    memcpy(&file, disk, sizeof(file));
    assert(file.version == 4 && file.values[0] == 39 &&
           file.checksum == checksum(&file, sizeof(file)));
    cfw_eq_model.commit();
    assert(writes == 2);
    cfw_eq_load_preferences();
    assert(settings[0] == 39);
    filter_failure = 1;
    cfw_eq_model.change(0, 40);
    assert(settings[0] == 39 && !dirty);
    filter_failure = 0;
    cfw_eq_model.change(0, CFW_EQ_PRECUT_AUTO);
    cfw_eq_model.commit();
    assert(!dirty && writes == 3);
    cfw_eq_load_preferences();
    assert(settings[0] == CFW_EQ_PRECUT_AUTO);
    cfw_eq_model.change(0, 41);
    write_failure = 1;
    cfw_eq_model.commit();
    assert(dirty);
    write_failure = 0;
    cfw_eq_model.commit();
    assert(!dirty && writes == 5);
    cfw_eq_model.change(CFW_EQ_BAND4_TYPE, 1);
    cfw_eq_model.change(CFW_EQ_BAND5_GAIN, 40);
    cfw_eq_model.commit();
    cfw_eq_load_preferences();
    assert(settings[CFW_EQ_BAND4_TYPE] == 1 && settings[CFW_EQ_BAND5_GAIN] == 40);
    disk_size--;
    cfw_eq_load_preferences();
    assert(settings[0] == 0 && settings[CFW_EQ_BAND5_GAIN] == 24);
    saved(2, 37);
    disk[20] ^= 1;
    cfw_eq_load_preferences();
    assert(settings[0] == 0);
    return 0;
}
