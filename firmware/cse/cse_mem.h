/*
 * Munt386-CSE -- paged guest-memory backend interface.
 */
#ifndef CSE_MEM_H
#define CSE_MEM_H

#include <stdint.h>

int      cse_mem_init(void);
int      cse_mem_ready(void);
uint32_t cse_mem_cache_pages(void);
/* Halt hook invoked if the working set would exceed the cache without a
 * backing store (never happens with the default full 128-slot cache). */
void     cse_mem_set_panic(void (*hook)(const char *msg));
/* Invalidate and clear every guest page (machine_reset semantics). */
void     mem_clear_all(pc_t *pc);

#endif /* CSE_MEM_H */
