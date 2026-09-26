/*
 * Munt386 -- portable platform helpers shared by every backend.
 *
 * pc_config mirrors the tiny386 ini semantics (mem_size / vga_mem_size /
 * display width+height), pace_* replaces the per-run static counter that used
 * to live inside pit_advance() so the emulator stays reentrant, and
 * plat_str_copy gives backends without snprintf a bounded string copy.
 */
#include "platform.h"
#include <string.h>

void pc_config_defaults(pc_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->mem_size = 0x00100000u;      /* 1 MiB guest address space */
    cfg->vga_mem_size = 0x10000;      /* 64 KiB VGA window          */
    cfg->disk_sectors = 0;
    cfg->max_steps = 50u * 1000u * 1000u;
    cfg->readonly_disk = 0;
    cfg->fill_cmos = 1;
    cfg->display_width = 320;         /* tiny386.ini [display]      */
    cfg->display_height = 240;
}

void pace_init(pace_t *p, uint32_t calls_per_tick)
{
    p->counter = 0;
    p->calls_per_tick = calls_per_tick ? calls_per_tick : 4;
}

int pace_should_tick(pace_t *p)
{
    if (++p->counter >= p->calls_per_tick) { p->counter = 0; return 1; }
    return 0;
}

void plat_str_copy(char *dst, size_t cap, const char *src)
{
    size_t i;
    if (!cap) return;
    for (i = 0; i + 1 < cap && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}
