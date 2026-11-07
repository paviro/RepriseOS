/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define CFW_OSOS_H
#define CFW_PATCH_H
#define PATCH_ARM

struct osos_path {
    uint16_t volume;
    char text[258];
};

enum {
    OSOS_ACTION_SUPPORT_QUERY = 0xdeadbeef,
    OSOS_PROPERTY_STRING = 0x53747220,
    OSOS_NOTIFY_ALL = 0x2a2a2a2a,
};

static unsigned char disk[8];
static uint32_t disk_size, writes, notified;
static int write_failure;
static void *model = &model;

static struct {
    unsigned char padding[0xb0];
    void *model;
} controller = {.model = &model};

static int osos_file_open(const struct osos_path *path, int mode, void **handle)
{
    assert(path->volume == 0);
    assert(!strcmp(path->text, "iPod_Control\\Device\\CFWCrossfeed"));
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
    assert(handle == disk && size <= sizeof(disk));
    writes++;
    if (write_failure)
        return -1;
    memcpy(disk, data, size);
    disk_size = size;
    *count = size;
    return 0;
}

static void osos_file_close(void *handle)
{
    assert(handle == disk);
}

static uintptr_t osos_settings_on_off(int enabled)
{
    return enabled ? 1 : 2;
}

static void *osos_settings_model(void *owner)
{
    return *(void **)((unsigned char *)owner + 0xb0);
}

static void osos_model_notify(void *target, uint32_t type, uint32_t property)
{
    assert(target == &model && type == OSOS_NOTIFY_ALL);
    notified = property;
}

#include "../../../payload/crossfeed.c"

/* Rockbox's crossfeed_meier_process on s3.28 samples, with fp_div(x, fs, 31)
 * coefficients. */
static void reference(const int16_t *in, int16_t *out, unsigned frames, uint32_t rate,
                      int32_t history[3])
{
    int32_t coef1 = (int32_t)(((int64_t)2128 << 31) / rate);
    int32_t coef2 = (int32_t)(((int64_t)1000 << 31) / rate);
    int32_t vcl = history[0], vcr = history[1], vdiff = history[2];
    for (unsigned i = 0; i < frames; i++) {
        int32_t lout = (int32_t)in[2 * i] * (1 << 13) + vcl;
        int32_t rout = (int32_t)in[2 * i + 1] * (1 << 13) + vcr;
        out[2 * i] = output(lout);
        out[2 * i + 1] = output(rout);
        int32_t common = (int32_t)(((int64_t)vdiff * coef2) >> 31);
        vcl -= (int32_t)(((int64_t)vcl * coef1) >> 31) + common;
        vcr -= (int32_t)(((int64_t)vcr * coef1) >> 31) - common;
        vdiff = lout - rout;
    }
    history[0] = vcl;
    history[1] = vcr;
    history[2] = vdiff;
}

enum { FRAMES = 512 };

static void signal(int16_t *samples, unsigned frames, unsigned seed, int panned)
{
    uint32_t value = seed;
    for (unsigned i = 0; i < frames; i++) {
        value = value * 1664525u + 1013904223u;
        int16_t left = (int16_t)(value >> 16);
        samples[2 * i] = left;
        samples[2 * i + 1] = panned ? 0 : left;
    }
    if (panned)
        samples[0] = 32767; /* full-scale impulse, left only */
}

static void check_processing(void)
{
    int16_t in[FRAMES * 2], buffer[FRAMES * 2], expected[FRAMES * 2];
    signal(in, FRAMES, 1, 1);

    /* Off leaves the stream untouched. */
    memcpy(buffer, in, sizeof(in));
    cfw_crossfeed_process(buffer, FRAMES, 44100);
    assert(!memcmp(buffer, in, sizeof(in)));

    enabled = 1;
    for (const uint32_t *rate = (const uint32_t[]){44100, 48000, 8000, 0}; *rate;
         rate++) {
        /* A rate change restarts the filter, matching a fresh reference. */
        int32_t history[3] = {0};
        for (unsigned block = 0; block < 3; block++) {
            signal(in, FRAMES, block + *rate, 1);
            memcpy(buffer, in, sizeof(in));
            cfw_crossfeed_process(buffer, FRAMES, *rate);
            reference(in, expected, FRAMES, *rate, history);
            assert(!memcmp(buffer, expected, sizeof(buffer)));
        }
        /* A hard-panned left channel now reaches the right one. */
        int crossed = 0;
        for (unsigned i = 0; i < FRAMES; i++)
            crossed |= buffer[2 * i + 1] != 0;
        assert(crossed);
    }

    /* Identical channels have no difference signal and pass through. */
    enabled = 0;
    cfw_crossfeed_process(buffer, 1, 44100);
    enabled = 1;
    signal(in, FRAMES, 7, 0);
    memcpy(buffer, in, sizeof(in));
    cfw_crossfeed_process(buffer, FRAMES, 44100);
    assert(!memcmp(buffer, in, sizeof(in)));
    enabled = 0;
}

static void check_setting(void)
{
    uintptr_t value = 0;
    cfw_crossfeed_load();
    assert(!enabled);
    assert(cfw_crossfeed_property(OSOS_PROPERTY_STRING, CFW_Crossfeed_Value, &value));
    assert(value == 2);
    assert(
        !cfw_crossfeed_property(OSOS_PROPERTY_STRING, CFW_Crossfeed_Value + 1, &value));
    assert(!cfw_crossfeed_action(&controller, "CFW_Other_Toggle", 0));
    assert(cfw_crossfeed_action(&controller, "CFW_Crossfeed_Toggle",
                                OSOS_ACTION_SUPPORT_QUERY));
    assert(!enabled && !writes);

    assert(cfw_crossfeed_action(&controller, "CFW_Crossfeed_Toggle", 0));
    assert(enabled && writes == 1 && notified == CFW_Crossfeed_Value);
    assert(disk_size == 4 && !memcmp(disk, "CXF1", 4));
    enabled = 0;
    cfw_crossfeed_load();
    assert(enabled);

    /* A failed save keeps the current state. */
    write_failure = 1;
    notified = 0;
    assert(cfw_crossfeed_action(&controller, "CFW_Crossfeed_Toggle", 0));
    assert(enabled && writes == 2 && !notified);
    write_failure = 0;
    assert(cfw_crossfeed_action(&controller, "CFW_Crossfeed_Toggle", 0));
    assert(!enabled && !memcmp(disk, "CXF0", 4));

    /* Unknown records read as Off. */
    memcpy(disk, "XXXX", 4);
    enabled = 1;
    cfw_crossfeed_load();
    assert(!enabled);
}

int main(void)
{
    check_processing();
    check_setting();
    return 0;
}
