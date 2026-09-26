/*
 * Munt386-CSE -- paged guest-memory backend.
 *
 * Implements the exact mem_pread / mem_pwrite interface from include/munt386.h
 * (so src/memory.c and src/cpu.c compile unchanged) over a cache of 8 KiB
 * guest pages held in CSE SRAM.
 *
 * HONEST-BACKING RULE (the spec forbids faking RAM):
 *   - With the default CSE_MEM_CACHE_PAGES=128 the cache holds every guest
 *     page (128 x 8 KiB = 1 MiB), so the guest address space is fully backed
 *     and nothing is ever discarded.  This is the simulator/host configuration
 *     and any device configuration with enough SRAM.
 *   - A reduced-slot device configuration is legal ONLY with a backing store:
 *     dirty-page eviction must be able to swap the page image out (flash
 *     storage region, docs/CSE_MEMORY_MAP.md).  Until that path is enabled
 *     (milestone 2), a build with fewer slots than pages and a dirty victim
 *     invokes the cse_mem_panic() hook and halts instead of silently losing
 *     guest memory.
 *
 * Guest pages in the BIOS/IVT/boot areas are pinned so the structures the
 * emulated BIOS relies on never leave the working set.
 */
#include "munt386.h"
#include "cse_mem.h"
#include "banking.h"
#include <string.h>

#define GUEST_PAGE_SHIFT  13u                 /* 8 KiB */
#define GUEST_PAGE_SIZE   (1u << GUEST_PAGE_SHIFT)
#define GUEST_PAGE_MASK   (GUEST_PAGE_SIZE - 1u)
#define GUEST_PAGE_COUNT  (X86_MEM_SIZE >> GUEST_PAGE_SHIFT)  /* 128 */

#ifndef CSE_MEM_CACHE_PAGES
#define CSE_MEM_CACHE_PAGES 128u
#endif

typedef struct {
    uint8_t  *slot;          /* SRAM backing, GUEST_PAGE_SIZE bytes */
    uint16_t  guest_page;
    uint8_t   valid;
    uint8_t   dirty;
    uint8_t   pinned;
    uint8_t   age;
} cache_slot_t;

static cache_slot_t slots[CSE_MEM_CACHE_PAGES];
static uint32_t tick_count;
static int mem_ready;
static void (*panic_hook)(const char *msg);

void cse_mem_set_panic(void (*hook)(const char *msg)) { panic_hook = hook; }

static void mem_panic(const char *msg)
{
    if (panic_hook) panic_hook(msg);
    for (;;) { /* halt: never continue with lost guest memory */ }
}

/* Guest pages that must never be evicted. */
static int page_is_pinned(uint32_t guest_page)
{
    uint32_t base = guest_page << GUEST_PAGE_SHIFT;
    if (base == 0x00000u)                       return 1;  /* IVT + BDA      */
    if (base == 0x06000u)                       return 1;  /* boot sector    */
    if (base >= 0xF0000u)                       return 1;  /* BIOS ROM area  */
    return 0;
}

int cse_mem_init(void)
{
    memset(slots, 0, sizeof(slots));
    tick_count = 0;
    for (uint32_t i = 0; i < CSE_MEM_CACHE_PAGES; i++) {
        slots[i].slot = banking_alloc(GUEST_PAGE_SIZE);
        if (!slots[i].slot) { mem_ready = 0; return -1; }
        memset(slots[i].slot, 0, GUEST_PAGE_SIZE);
    }
    mem_ready = 1;
    return 0;
}

int cse_mem_ready(void) { return mem_ready; }

uint32_t cse_mem_cache_pages(void) { return CSE_MEM_CACHE_PAGES; }

/* Reset all guest memory (machine_reset semantics): every cache slot is
 * invalidated and cleared.  Called through mem_clear_all(). */
void mem_clear_all(pc_t *pc)
{
    (void)pc;
    if (!mem_ready) return;
    for (uint32_t i = 0; i < CSE_MEM_CACHE_PAGES; i++) {
        memset(slots[i].slot, 0, GUEST_PAGE_SIZE);
        slots[i].valid = 0;
        slots[i].dirty = 0;
    }
}

static cache_slot_t *find_slot(uint32_t guest_page)
{
    for (uint32_t i = 0; i < CSE_MEM_CACHE_PAGES; i++)
        if (slots[i].valid && slots[i].guest_page == guest_page)
            return &slots[i];
    return NULL;
}

static cache_slot_t *victim_slot(uint32_t guest_page)
{
    cache_slot_t *best = NULL;
    int want_pin = page_is_pinned(guest_page);

    for (uint32_t i = 0; i < CSE_MEM_CACHE_PAGES; i++) {
        cache_slot_t *s = &slots[i];
        if (!s->valid) return s;                       /* free slot          */
        if (s->pinned && !want_pin) continue;          /* never steal pinned */
        if (s->dirty) continue;                        /* no store: keep     */
        if (!best || s->age < best->age) best = s;
    }
    if (best) return best;

    /* Only dirty/pinned slots remain.  With a backing store the dirty page
     * would be swapped out here (milestone 2).  Without one, halting is the
     * only honest outcome. */
    mem_panic("cse_mem: guest working set exceeds cache without backing store");
    return &slots[0];                                  /* unreachable        */
}

static cache_slot_t *obtain(uint32_t guest_page)
{
    cache_slot_t *s = find_slot(guest_page);
    if (!s) {
        s = victim_slot(guest_page);
        if (!s->valid || s->guest_page != guest_page) {
            memset(s->slot, 0, GUEST_PAGE_SIZE);       /* first touch        */
            s->guest_page = (uint16_t)guest_page;
            s->pinned = (uint8_t)page_is_pinned(guest_page);
            s->dirty = 0;
        }
        s->valid = 1;
    }
    s->age = (uint8_t)(++tick_count & 0xFF);
    return s;
}

/* ---------------- interface implementation ---------------- */

uint8_t mem_pread8(pc_t *pc, uint32_t phys)
{
    (void)pc;
    uint32_t a = phys & X86_PHYS_MASK;
    if (!mem_ready) return 0;
    cache_slot_t *s = obtain(a >> GUEST_PAGE_SHIFT);
    return s->slot[a & GUEST_PAGE_MASK];
}

uint16_t mem_pread16(pc_t *pc, uint32_t phys)
{
    uint32_t a = phys & X86_PHYS_MASK;
    uint16_t lo = mem_pread8(pc, a);
    uint16_t hi = mem_pread8(pc, (a + 1u) & X86_PHYS_MASK);
    return (uint16_t)(lo | (hi << 8));
}

uint32_t mem_pread32(pc_t *pc, uint32_t phys)
{
    uint32_t a = phys & X86_PHYS_MASK;
    uint32_t r = 0;
    for (int i = 3; i >= 0; i--)
        r = (r << 8) | mem_pread8(pc, (a + (uint32_t)i) & X86_PHYS_MASK);
    return r;
}

void mem_pwrite8(pc_t *pc, uint32_t phys, uint8_t v)
{
    (void)pc;
    uint32_t a = phys & X86_PHYS_MASK;
    if (!mem_ready) return;
    cache_slot_t *s = obtain(a >> GUEST_PAGE_SHIFT);
    s->slot[a & GUEST_PAGE_MASK] = v;
    s->dirty = 1;
}

void mem_pwrite16(pc_t *pc, uint32_t phys, uint16_t v)
{
    uint32_t a = phys & X86_PHYS_MASK;
    mem_pwrite8(pc, a, (uint8_t)(v & 0xFF));
    mem_pwrite8(pc, (a + 1u) & X86_PHYS_MASK, (uint8_t)(v >> 8));
}

void mem_pwrite32(pc_t *pc, uint32_t phys, uint32_t v)
{
    uint32_t a = phys & X86_PHYS_MASK;
    for (int i = 0; i < 4; i++)
        mem_pwrite8(pc, (a + (uint32_t)i) & X86_PHYS_MASK,
                    (uint8_t)(v >> (8 * i)));
}
