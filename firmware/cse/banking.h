/*
 * Munt386-CSE -- banked SRAM arena interface.
 */
#ifndef BANKING_H
#define BANKING_H

#include <stdint.h>

/* Compile-time arena size.  The device build keeps the 192 KiB default (see
 * docs/CSE_MEMORY_MAP.md section 5); the host simulator overrides it via
 * -DCSE_ARENA_SIZE so the full guest page cache + framebuffers fit.  On
 * hardware the Z80 self-test measures real SRAM and must confirm the arena
 * fits before the guest runs (VERIFY item). */
#ifndef CSE_ARENA_SIZE
#define CSE_ARENA_SIZE (192u * 1024u)
#endif

void     banking_init(uint32_t measured_sram);
uint8_t *banking_alloc(uint32_t size);
uint8_t *banking_alloc_disk(uint32_t bytes);
uint32_t banking_available(void);
uint32_t banking_budget(void);

#endif /* BANKING_H */
