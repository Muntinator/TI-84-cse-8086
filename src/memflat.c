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

uint8_t *mem_backing_alloc(uint32_t size)
{
    return (uint8_t *)calloc(1, size);
}

void mem_backing_free(uint8_t *mem, uint32_t size)
{
    (void)size;
    free(mem);
}
