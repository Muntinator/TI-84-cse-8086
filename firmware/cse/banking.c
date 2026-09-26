/*
 * Munt386-CSE -- banked SRAM arena manager.
 *
 * The CSE exposes SRAM through banked windows (port 5 paging).  SDCC's
 * "large" model already places data/heap in the external window, so this
 * module manages one static arena inside it: no malloc, fixed-size buffers,
 * deterministic layout (see docs/CSE_MEMORY_MAP.md section 5).
 *
 * The arena hands out:
 *   - the CSE LCD buffer   (150 KiB)
 *   - the VGA framebuffer  (up to 300 KiB, mode dependent)
 *   - the RAM virtual disk (size chosen at startup from the measured budget)
 *
 * All of these live in guest-visible SDCC external RAM; on the real device
 * the linker/startup pins them into the paged windows.
 */
#include "banking.h"
#include <string.h>

static uint8_t  arena[CSE_ARENA_SIZE];
static uint32_t arena_used;
static uint32_t sram_budget;      /* measured/assumed usable SRAM */

void banking_init(uint32_t measured_sram)
{
    arena_used = 0;
    sram_budget = measured_sram;
    memset(arena, 0, sizeof(arena));
}

uint8_t *banking_alloc(uint32_t size)
{
    if ((uint32_t)arena_used + size > (uint32_t)sizeof(arena)) return NULL;
    uint8_t *p = arena + arena_used;
    arena_used += size;
    return p;
}

uint32_t banking_available(void)
{
    return (uint32_t)sizeof(arena) - arena_used;
}

uint32_t banking_budget(void)
{
    return sram_budget;
}

/* Reserve the disk after the video buffers; returns NULL when it does not
 * fit, letting startup shrink the disk instead of failing. */
uint8_t *banking_alloc_disk(uint32_t bytes)
{
    return banking_alloc(bytes);
}
