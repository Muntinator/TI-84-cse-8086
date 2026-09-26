/*
 * Munt386 -- host platform backend (POSIX/libc).
 *
 * Implements include/platform.h for development machines.  The CSE backend
 * (firmware/cse/*.c) implements the identical interface on the calculator.
 * Nothing in the portable core may call these functions directly except
 * through the declarations in platform.h.
 */
#define _POSIX_C_SOURCE 200809L
#include "platform.h"
#include "munt386.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int platform_init(void) { return 0; }
void platform_shutdown(void) {}

uint64_t platform_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)(ts.tv_nsec / 1000u);
}

void platform_delay_us(uint32_t us)
{
    uint64_t end = platform_time_us() + us;
    while (platform_time_us() < end) { /* busy wait */ }
}

uint8_t platform_read_input(void)
{
    int c = getchar();
    return (c == EOF) ? 0 : (uint8_t)c;
}

int platform_poll_input(uint8_t *out)
{
    int c = getchar();
    if (c == EOF) return 0;
    *out = (uint8_t)c;
    return 1;
}

int platform_video_init(void) { return 0; }

/* The host presents frames via the PPM dump at exit; no live window. */
int platform_video_update(const uint8_t *fb, uint32_t w, uint32_t h)
{
    (void)fb; (void)w; (void)h;
    return 0;
}

void platform_video_shutdown(void) {}

/* --- virtual disk (host keeps the whole image in memory) --------------- */

static uint8_t *disk_img = NULL;
static uint32_t disk_secs = 0;
static int      disk_ro = 0;

int platform_disk_attach(uint32_t sector_count, int readonly)
{
    platform_memory_free(disk_img, (size_t)disk_secs * DISK_SECTOR_SIZE);
    disk_img = NULL;
    disk_secs = 0;
    if (sector_count == 0) return 0;
    disk_img = (uint8_t *)calloc(1, (size_t)sector_count * DISK_SECTOR_SIZE);
    if (!disk_img) return -1;
    disk_secs = sector_count;
    disk_ro = readonly;
    return 0;
}

uint32_t platform_disk_sectors(void) { return disk_secs; }

int platform_disk_read(uint8_t *buf, uint32_t lba, uint32_t count)
{
    if (!disk_img || count == 0 || lba > disk_secs || count > disk_secs - lba)
        return -1;
    memcpy(buf, disk_img + (size_t)lba * DISK_SECTOR_SIZE,
           (size_t)count * DISK_SECTOR_SIZE);
    return 0;
}

int platform_disk_write(const uint8_t *buf, uint32_t lba, uint32_t count)
{
    if (!disk_img || disk_ro || count == 0 || lba > disk_secs || count > disk_secs - lba)
        return -1;
    memcpy(disk_img + (size_t)lba * DISK_SECTOR_SIZE, buf,
           (size_t)count * DISK_SECTOR_SIZE);
    return 0;
}

void platform_debug_print(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
}

void platform_exit(int code) { exit(code); }

void *platform_memory_alloc(size_t n) { return malloc(n); }
void  platform_memory_free(void *p, size_t n) { (void)n; free(p); }
