/*
 * Munt386 -- platform abstraction layer.
 *
 * This header is the ONLY seam between the portable emulator (the src .c
 * files other than main.c) and the machine it runs on.  Two backends exist:
 *
 *   src platform_host.c     host (POSIX/libc) backend
 *   firmware/cse .c files   TI-84 Plus CSE bare-metal backend (SDCC)
 *
 * The x86 CPU core, virtual chipset, BIOS and DOS layers never include any
 * other platform header and never call host or CSE services directly.
 *
 * Simplicity rule: every function here is either trivially inlineable on the
 * host or a direct hardware write on the CSE.  No function in this interface
 * allocates, locks, or does anything the emulator core could not already do.
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <stddef.h>

/* Which backend is compiled in. */
#if defined(MUNT386_CSE)
#  define PLATFORM_NAME "ti84pcse"
#else
#  define PLATFORM_NAME "host"
#endif

/* ------------------------------------------------------------------ */
/* Guest memory backing                                                */
/* ------------------------------------------------------------------ */

/* Allocate and zero the guest address space (X86_MEM_SIZE bytes, or more on
 * hosts running protected-mode tests).  Returns NULL on failure. */
uint8_t *mem_backing_alloc(uint32_t size);
void     mem_backing_free(uint8_t *mem, uint32_t size);

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

int  platform_init(void);
void platform_shutdown(void);

/* ------------------------------------------------------------------ */
/* Timing                                                              */
/* ------------------------------------------------------------------ */

/* Microsecond monotonic timestamp; wraps rarely and diffs are wrap-safe. */
uint64_t platform_time_us(void);
/* Busy-wait at least the given number of microseconds. */
void     platform_delay_us(uint32_t us);

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

/* Block until a byte of platform input is available and return it.
 * The host backend returns bytes from stdin; the CSE backend returns
 * PC set-1 scancodes from the keypad.  Returns 0 on EOF / no more input. */
uint8_t platform_read_input(void);

/* Non-blocking poll: returns 1 and fills *out when a byte is available. */
int  platform_poll_input(uint8_t *out);

/* ------------------------------------------------------------------ */
/* Video                                                               */
/* ------------------------------------------------------------------ */

int  platform_video_init(void);
/* Present the abstract framebuffer. Returns 0 on success. */
int  platform_video_update(const uint8_t *fb, uint32_t w, uint32_t h);
void platform_video_shutdown(void);

/* ------------------------------------------------------------------ */
/* Storage                                                             */
/* ------------------------------------------------------------------ */

/* Attach a disk image of the given size.  On the host this loads a file;
 * on the CSE it prepares a RAM-backed virtual disk.  Returns 0 on success. */
int  platform_disk_read (uint8_t *buf, uint32_t lba, uint32_t count);
int  platform_disk_write(const uint8_t *buf, uint32_t lba, uint32_t count);
int  platform_disk_attach(uint32_t sector_count, int readonly);
uint32_t platform_disk_sectors(void);

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

/* Emit one diagnostic line (stdio on the host, LCD on the CSE). */
void platform_debug_print(const char *msg);

/* Exit code for the process / image when the guest terminates. */
void platform_exit(int code);

/* ------------------------------------------------------------------ */
/* Memory allocation (non-guest)                                       */
/* ------------------------------------------------------------------ */

void *platform_memory_alloc(size_t n);
void  platform_memory_free(void *p, size_t n);

/* Small string helpers shared by backends (SDCC has no snprintf). */
void plat_str_copy(char *dst, size_t cap, const char *src);

/* ------------------------------------------------------------------ */
/* Configuration (mirrors tiny386.ini semantics)                       */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t mem_size;        /* guest RAM bytes reported to the guest     */
    uint32_t vga_mem_size;    /* VGA window size                           */
    uint32_t disk_sectors;    /* sectors of the attached virtual disk      */
    uint32_t max_steps;       /* run limit; 0 = run until halt/exit        */
    int      readonly_disk;
    int      fill_cmos;
    /* ini [display] width/height preserved for parity with the original */
    uint32_t display_width;
    uint32_t display_height;
} pc_config;

void pc_config_defaults(pc_config *cfg);

/* Per-run pacing counter state (moved out of a function static so the
 * emulator stays reentrant and portable). */
typedef struct {
    uint32_t counter;
    uint32_t calls_per_tick;
} pace_t;
void pace_init(pace_t *p, uint32_t calls_per_tick);
int  pace_should_tick(pace_t *p);

#endif /* PLATFORM_H */
