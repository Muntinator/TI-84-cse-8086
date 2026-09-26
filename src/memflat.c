/*
 * Munt386 -- flat guest-memory backing (host default).
 *
 * The guest address space is one contiguous allocation.  The CSE backend
 * replaces mem_backing_alloc/free with a paged implementation (see
 * firmware/cse/cse_mem.c); nothing else in the portable core changes.
 * This file is compiled only into host binaries.
 */
#include "munt386.h"
#include "platform.h"
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Flat physical accessors (host default)                              */
/* ------------------------------------------------------------------ */

uint8_t mem_pread8(pc_t *pc, uint32_t phys)
{
    return pc->mem[phys & X86_PHYS_MASK];
}

uint16_t mem_pread16(pc_t *pc, uint32_t phys)
{
    uint32_t lo = phys & X86_PHYS_MASK;
    uint32_t hi = (lo + 1) & X86_PHYS_MASK;
    return (uint16_t)pc->mem[lo] | ((uint16_t)pc->mem[hi] << 8);
}

uint32_t mem_pread32(pc_t *pc, uint32_t phys)
{
    uint32_t b0 = phys & X86_PHYS_MASK;
    uint32_t b1 = (b0 + 1) & X86_PHYS_MASK;
    uint32_t b2 = (b0 + 2) & X86_PHYS_MASK;
    uint32_t b3 = (b0 + 3) & X86_PHYS_MASK;
    return (uint32_t)pc->mem[b0]
         | ((uint32_t)pc->mem[b1] << 8)
         | ((uint32_t)pc->mem[b2] << 16)
         | ((uint32_t)pc->mem[b3] << 24);
}

void mem_pwrite8(pc_t *pc, uint32_t phys, uint8_t v)
{
    pc->mem[phys & X86_PHYS_MASK] = v;
}

void mem_pwrite16(pc_t *pc, uint32_t phys, uint16_t v)
{
    uint32_t lo = phys & X86_PHYS_MASK;
    uint32_t hi = (lo + 1) & X86_PHYS_MASK;
    pc->mem[lo] = (uint8_t)(v & 0xff);
    pc->mem[hi] = (uint8_t)(v >> 8);
}

void mem_pwrite32(pc_t *pc, uint32_t phys, uint32_t v)
{
    uint32_t b0 = phys & X86_PHYS_MASK;
    uint32_t b1 = (b0 + 1) & X86_PHYS_MASK;
    uint32_t b2 = (b0 + 2) & X86_PHYS_MASK;
    uint32_t b3 = (b0 + 3) & X86_PHYS_MASK;
    pc->mem[b0] = (uint8_t)(v);
    pc->mem[b1] = (uint8_t)(v >> 8);
    pc->mem[b2] = (uint8_t)(v >> 16);
    pc->mem[b3] = (uint8_t)(v >> 24);
}

/* ------------------------------------------------------------------ */
/* Backing allocation                                                  */
/* ------------------------------------------------------------------ */

uint8_t *mem_backing_alloc(uint32_t size)
{
    return (uint8_t *)calloc(1, size);
}

void mem_backing_free(uint8_t *mem, uint32_t size)
{
    (void)size;
    free(mem);
}
