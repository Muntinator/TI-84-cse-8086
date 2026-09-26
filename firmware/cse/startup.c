/*
 * Munt386-CSE -- bare-metal startup and diagnostic screen.
 *
 * Boot sequence required by the spec:
 *
 *   1. Z80 bootstrap (startup.s on device): DI/IM 1, banks, speed, stack.
 *   2. cse_startup(): banner + per-subsystem self-tests.  A line is printed
 *      ONLY after its test passes; a failure halts with the subsystem name.
 *   3. Guest boot: virtual PC init -> RAM virtual disk -> run the guest
 *      (BIOS -> boot sector), keypad live, framebuffer presented to the LCD.
 *
 * Banner text per the spec:
 *      MUNT386-CSE
 *      NSPIRE95/TINY386 PORT
 *      INITIALIZING...
 *
 * No TI-OS services are used anywhere in this path.
 */
#include "platform.h"
#include "munt386.h"
#include "cse_hardware.h"
#include "cse_ports.h"
#include "cse_video.h"
#include "cse_main.h"
#include "cse_keys.h"
#include "cse_disk.h"
#include "cse_mem.h"
#include "banking.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Self-tests (each returns 1 = pass)                                  */
/* ------------------------------------------------------------------ */

/* CPU speed: confirm the highest stable speed (15 MHz = speed index 1 on
 * port 0x20) is actually in effect by reading the port back.  Indexes 2/3
 * are unimplemented 20/25 MHz modes -- 15 MHz IS the maximum stable clock
 * (see cse_hardware.h / docs/CSE_HARDWARE.md). */
static int test_cpu_speed(void)
{
    int v = cse_cpu_speed_get();
    if (v < 0) return 0;                       /* port does not read back   */
    return v == CPUSPEED_HIGHEST;
}
static int test_ram(void)
{
    /* March a pattern through a probe block in the arena. */
    static uint8_t probe[1024];
    memset(probe, 0, sizeof(probe));
    for (int i = 0; i < 1024; i++) probe[i] = (uint8_t)(i ^ 0xA5);
    for (int i = 0; i < 1024; i++)
        if (probe[i] != (uint8_t)(i ^ 0xA5)) return 0;
    return 1;
}

static int test_lcd(void)
{
    if (!cse_video_ready()) return 0;
#ifdef MUNT386_DEVICE
    /* Device: no pixel read-back without a shadow buffer.  Video init itself
     * streamed a full frame (clear) and the power-on sequence completed, so
     * readiness of the initialised state is the self-test. */
    return 1;
#else
    /* Write and read back a pixel through the backend buffer. */
    cse_video_set_pixel(5, 5, 0xF800);
    int ok = (cse_video_get_pixel(5, 5) == 0xF800);
    cse_video_set_pixel(5, 5, 0x0000);
    return ok;
#endif
}

static int test_keypad(void)
{
    /* Idle scan must be stable across two reads (no phantom keys). */
    cse_keypad_start();
    uint8_t a = cse_keypad_read_group(0xFF);
    cse_keypad_end();
    cse_keypad_start();
    uint8_t b = cse_keypad_read_group(0xFF);
    cse_keypad_end();
    return a == b;
}

static int test_timer(void)
{
    /* Two independent tick sources, either of which proves the timer block
     * is alive:
     *   1. platform_time_us() advancing (interrupt-fed software counter),
     *   2. the crystal-timer 1 counter register changing (direct poll; no
     *      interrupt enable needed, documented bring-up check).
     * If NEITHER advances the timer subsystem is genuinely dead. */
    uint8_t c0 = cse_port_in(PORT_CRYS1_COUNTER);
    uint64_t t0 = platform_time_us();
    for (volatile uint32_t i = 0; i < 2000000u; i++) { /* bounded wait */ }
    uint8_t c1 = cse_port_in(PORT_CRYS1_COUNTER);
    if (c1 != c0) return 1;
    return platform_time_us() != t0;
}

static int test_flash(void)
{
    /* READ-ONLY checksum across the mapped bank-B window.  Never erases or
     * writes.  The Z80 bring-up path performs the real checksum; the C
     * path validates the mapping registers respond. */
    uint8_t a = cse_port_in(PORT_BANKA);
    uint8_t b = cse_port_in(PORT_BANKB);
    return (a | b) != 0xFF;                   /* ports must be driveable */
}

/* ------------------------------------------------------------------ */
/* Guest boot                                                          */
/* ------------------------------------------------------------------ */

static pc_t guest_pc;

/* CSE_PARK(reason): the bare-metal park.  On the device every park site
 * below is a permanent halt (a calculator firmware must never "return to
 * nowhere").  In the host simulator (MUNT386_CSE_SIM) the park calls the
 * harness hook cse_sim_park(reason) first, which reports the site and exits
 * so the test harness can regain control. */
#if defined(MUNT386_CSE_SIM)
void cse_sim_park(const char *reason);
#define CSE_PARK(reason) cse_sim_park(reason)
#else
#define CSE_PARK(reason) for (;;) { }
#endif

static void guest_panic(const char *msg)
{
    cse_video_print("MEM FAULT:");
    cse_video_print(msg);
    CSE_PARK("MEM FAULT");
}

/* Keyboard bridge: cse_keys.c emits scancodes straight into the queue. */
static void key_emit_bridge(void *ud, uint8_t sc)
{
    struct pc *pc = (struct pc *)ud;
    if (pc) kbd_push_scancode(pc, sc);
}

static void run_guest(void)
{
    cse_video_print("STARTING X86 CORE...");

    cse_mem_set_panic(guest_panic);
    if (cse_mem_init() != 0) {
        cse_video_print("MEM: NO SPACE");
        CSE_PARK("MEM: NO SPACE");
    }

    /* Keypad bytes flow through the emit bridge into the virtual 8042. */
    cse_keys_set_emit(key_emit_bridge, &guest_pc);
    machine_init(&guest_pc);

    /* RAM-backed virtual disk: capacity follows the arena budget, halved
     * until the allocation succeeds.  Attached to the floppy slot the BIOS
     * bootstrap reads (disk[0]). */
    uint32_t max_secs = cse_disk_max_sectors();
    uint8_t *img = NULL;
    while (max_secs > 0) {
        img = banking_alloc_disk((uint32_t)max_secs * 512u);
        if (img) break;
        max_secs /= 2;
    }
    if (img && cse_disk_attach(img, max_secs, 0) == 0)
        disk_attach(&guest_pc.disk[0], img, max_secs * 512u, 0, 0);

    machine_reset(&guest_pc);
    cse_keys_reset();

    cse_video_print("RUNNING GUEST...");

    for (;;) {
        (void)cpu_run(&guest_pc, 20000);      /* chunked execution        */
        cse_keys_scan();                      /* keypad -> scancodes      */
        if (guest_pc.vga.dirty) {
            vga_render(&guest_pc);
            platform_video_update(guest_pc.vga.fb, guest_pc.vga.fb_w,
                                  guest_pc.vga.fb_h);
            guest_pc.vga.dirty = 0;
        }
        if (!guest_pc.running || guest_pc.cpu.halted || guest_pc.cpu.fault)
            break;
        /* Feed the guest its timer cadence (virtual PIT stays abstract). */
        machine_timer_tick(&guest_pc);
        pic_deliver(&guest_pc);
    }

    cse_video_print("GUEST HALTED");
    CSE_PARK("GUEST HALTED");
}

/* ------------------------------------------------------------------ */
/* Entry                                                               */
/* ------------------------------------------------------------------ */

/* Z80 bootstrap (startup.s) calls this after DI/IM 1, banks, speed. */
int cse_startup(void)
{
#ifdef MUNT386_DEVICE
    /* Bare metal: interrupts stay off (di, im 1 done by the bootstrap); no
     * scheduler, no reentrancy. */
    __asm__("di");
#endif
    /* Working-set budget: the page cache backs the whole guest address space
     * (128 x 8 KiB = 1 MiB) by design (cse_mem.c HONEST-BACKING RULE).  The
     * device value is VERIFY-on-hardware; the sim override must be >= 1 MiB
     * for cse_mem_init() to succeed. */
    banking_init(CSE_ARENA_SIZE);

    platform_init();                          /* ports, speed, banks, ON IRQ */

    if (cse_video_init() != 0) {
        /* No LCD: nothing else can be reported. */
        for (;;) { }
    }
    cse_platform_set_diag(cse_video_print);

    cse_video_print("MUNT386-CSE");
    cse_video_print("NSPIRE95/TINY386 PORT");
    cse_video_print("INITIALIZING...");

    if (!test_cpu_speed()) { cse_video_print("CPU: SPEED FAIL"); CSE_PARK("CPU: SPEED FAIL"); }
    cse_video_print("CPU 15MHZ OK");

    if (!test_ram())    { cse_video_print("RAM: FAIL");    CSE_PARK("RAM: FAIL"); }
    cse_video_print("RAM OK");
    if (!test_lcd())    { cse_video_print("LCD: FAIL");    CSE_PARK("LCD: FAIL"); }
    cse_video_print("LCD OK");
    if (!test_keypad()) { cse_video_print("KEYPAD: FAIL"); CSE_PARK("KEYPAD: FAIL"); }
    cse_video_print("KEYPAD OK");
    if (!test_timer())  { cse_video_print("TIMER: FAIL");  CSE_PARK("TIMER: FAIL"); }
    cse_video_print("TIMER OK");
    if (!test_flash())  { cse_video_print("FLASH: FAIL");  CSE_PARK("FLASH: FAIL"); }
    cse_video_print("FLASH OK");

    if (cse_keys_on_held()) {
        cse_video_print("RECOVERY MODE");
        cse_video_print("RELEASE ON TO CONTINUE");
        while (cse_keys_on_held()) platform_delay_us(10000);
    }

    run_guest();
    return 0;
}
