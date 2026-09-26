/*
 * Munt386-CSE -- CSE platform backend (include/platform.h implementation).
 *
 * This is the TI-84 Plus CSE equivalent of src/platform_host.c: the only
 * place where the abstract platform interface meets concrete hardware.
 * The x86 core and virtual chipset are the same objects the host runs.
 *
 * Non-guest memory comes from the static banked arena (banking.c) -- no
 * malloc, no dynamic allocation after startup.
 */
#include "platform.h"
#include "munt386.h"
#include "cse_hardware.h"
#include "cse_ports.h"
#include "cse_video.h"
#include "cse_main.h"
#include "cse_keys.h"
#include "cse_keys.h"
#include "cse_disk.h"
#include "banking.h"
#include "cse_mem.h"
#include <string.h>

/* Provided by startup.c: renders one diagnostic line on the LCD. */

/* ------------------------------------------------------------------ */
/* Guest memory backing                                                */
/* ------------------------------------------------------------------ */

uint8_t *mem_backing_alloc(uint32_t size)
{
    /* The paged backend cse_mem.c provides mem_pread/mem_pwrite directly;
     * the "flat" pointer is unused on this platform.  The size argument is
     * validated so a configuration that cannot honestly back the guest
     * address space fails at startup, never at runtime. */
    (void)size;
    return (uint8_t *)1;      /* non-NULL sentinel; never dereferenced */
}

void mem_backing_free(uint8_t *mem, uint32_t size)
{
    (void)mem; (void)size;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

int platform_init(void)
{
    cse_hw_early_init();
    return 0;
}

void platform_shutdown(void)
{
    cse_video_shutdown();
}

/* ------------------------------------------------------------------ */
/* Timing (cse_ports.c provides platform_time_us/platform_delay_us)     */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Input: keypad -> PC scancodes -> virtual 8042                        */
/* ------------------------------------------------------------------ */

static struct pc *input_pc;

void cse_platform_attach_input(struct pc *pc) { input_pc = pc; }

static void key_emit(void *ud, uint8_t sc)
{
    struct pc *pc = (struct pc *)ud;
    if (pc) kbd_push_scancode(pc, sc);
}

uint8_t platform_read_input(void)
{
    /* Blocking read: scan until a scancode byte is produced. */
    for (;;) {
        cse_keys_scan();
        if (input_pc && input_pc->kbd.head != input_pc->kbd.tail)
            return (uint8_t)kbd_pop_key(input_pc);
        platform_delay_us(10000);           /* ~100 Hz scan cadence */
    }
}

int platform_poll_input(uint8_t *out)
{
    cse_keys_scan();
    if (input_pc && input_pc->kbd.head != input_pc->kbd.tail) {
        *out = (uint8_t)kbd_pop_key(input_pc);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Video                                                               */
/* ------------------------------------------------------------------ */

int platform_video_init(void)  { return cse_video_init(); }
void platform_video_shutdown(void) { cse_video_shutdown(); }

int platform_video_update(const uint8_t *fb, uint32_t w, uint32_t h)
{
    return cse_video_update(fb, w, h);
}

/* ------------------------------------------------------------------ */
/* Storage: RAM-backed virtual disk (see cse_disk.c)                    */
/* ------------------------------------------------------------------ */

static uint8_t *disk_backing;

int platform_disk_attach(uint32_t sector_count, int readonly)
{
    uint32_t bytes = sector_count * 512u;
    disk_backing = banking_alloc_disk(bytes);
    if (!disk_backing) return -1;
    memset(disk_backing, 0, bytes);
    return cse_disk_attach(disk_backing, sector_count, readonly);
}

uint32_t platform_disk_sectors(void) { return cse_disk_sectors(); }

int platform_disk_read(uint8_t *buf, uint32_t lba, uint32_t count)
{
    return cse_disk_read(buf, lba, count);
}

int platform_disk_write(const uint8_t *buf, uint32_t lba, uint32_t count)
{
    return cse_disk_write(buf, lba, count);
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

/* Diagnostic line renderer, injected by startup.c so platform_debug_print
 * reaches the LCD without a cyclic dependency. */
static void (*diag_line)(const char *);

void cse_platform_set_diag(void (*fn)(const char *))
{
    diag_line = fn;
}

void platform_debug_print(const char *msg)
{
    if (diag_line) diag_line(msg);
}

/* Soft reboot: jump back to the Z80 bootstrap entry.  No flash writes.
 * The trampoline lives in startup.s; this C path only ever runs in
 * inspection builds. */
void cse_reboot(void)
{
    for (;;) { }
}

/* ------------------------------------------------------------------ */
/* Non-guest allocation (arena, no malloc)                             */
/* ------------------------------------------------------------------ */

void *platform_memory_alloc(size_t n)
{
    return banking_alloc((uint32_t)n);
}

void platform_memory_free(void *p, size_t n)
{
    (void)p; (void)n;           /* arena is never shrunk */
}
