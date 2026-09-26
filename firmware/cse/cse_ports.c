/*
 * Munt386-CSE -- CSE hardware access layer (ports, timers, keypad, boot init).
 *
 * This is the ONLY file allowed to touch physical CSE registers.  All
 * emulator-side code reaches hardware through this module or through
 * cse_video/cse_keys, which are thin wrappers over it.  The x86 CPU core
 * never sees any of this.
 *
 * Two builds:
 *   MUNT386_HW_EMULATE (host tests): port I/O goes to a simulated MMIO
 *     space so every code path here is exercised by tests/cse/test_cse.c.
 *   real SDCC build: port I/O compiles to Z80 IN/OUT through __sfr
 *     declarations (no assembly needed for register access).
 */
#include "cse_hardware.h"
#include "cse_ports.h"
#include "platform.h"
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Port I/O                                                            */
/* ------------------------------------------------------------------ */

#ifdef MUNT386_HW_EMULATE

/* Simulated MMIO: the ports we use plus an LCD GRAM shadow so tests can
 * observe display output. */
#define SIM_PORTS 0x40
static uint8_t  sim_port[SIM_PORTS];
static uint32_t sim_lcd_addr;
static uint16_t sim_lcd_shadow[CSE_LCD_WIDTH * CSE_LCD_HEIGHT];
static uint8_t  sim_key_cols[8];      /* "pressed" columns per row group */
static int      sim_data_hi;          /* high/low byte phase for GRAM    */

void cse_sim_reset(void)
{
    memset(sim_port, 0, sizeof(sim_port));
    memset(sim_lcd_shadow, 0, sizeof(sim_lcd_shadow));
    sim_lcd_addr = 0;
    sim_data_hi = 1;
    memset(sim_key_cols, 0, sizeof(sim_key_cols));
    sim_port[PORT_KEYPAD] = 0xFF;       /* nothing driven */
    sim_port[PORT_CPUSPEED] = CPUSPEED_6MHZ;   /* reset speed: 6 MHz */
}

void cse_sim_key_event(int group_index, uint8_t col_bit)
{
    if (group_index >= 0 && group_index < 8)
        sim_key_cols[group_index] |= col_bit;
}

uint16_t cse_sim_last_pixel(uint32_t index)
{
    return sim_lcd_shadow[index % (CSE_LCD_WIDTH * CSE_LCD_HEIGHT)];
}

uint8_t cse_port_in(uint8_t port)
{
    if (port == PORT_KEYPAD) {
        /* Active-low columns of the currently driven row.  The drive mask
         * is one-hot low; no drive -> 0xFF (nothing selected). */
        uint8_t drive = sim_port[PORT_KEYPAD];
        uint8_t rows = (uint8_t)~drive;
        if (!rows) return 0xFF;
        for (int r = 0; r < 8; r++)
            if (rows & (1u << r))
                return (uint8_t)~sim_key_cols[r];
        return 0xFF;
    }
    if (port == PORT_CRYS1_COUNTER) {
        /* Simulated crystal-timer 1 counter: advances with host time so the
         * startup self-test sees a live timer block, as on the device. */
        return (uint8_t)((clock() / (CLOCKS_PER_SEC / 1000u)) & 0xFF);
    }
    return sim_port[port % SIM_PORTS];
}
void cse_port_out(uint8_t port, uint8_t value)
{
    port = (uint8_t)(port % SIM_PORTS);

    if (port == PORT_CPUSPEED) {
        /* Model the device register: only the documented speed indexes
         * (0 = 6 MHz, 1 = 15 MHz) latch.  The unimplemented 20/25 MHz
         * selections (2/3) do not take effect -- the register keeps the
         * previous speed, as on hardware where the modes were cut before
         * production. */
        if (value <= CPUSPEED_15MHZ) sim_port[PORT_CPUSPEED] = value;
        return;
    }

    sim_port[port] = value;

    if (port == PORT_LCD_CMD) {
        if (value == LCDR_GRAM) {
            sim_lcd_addr = 0;           /* GRAM address resets on 0x22 */
            sim_data_hi = 1;
        }
    } else if (port == PORT_LCD_DATA) {
        static uint16_t cur = 0;
        if (sim_data_hi) cur = (uint16_t)(value << 8);
        else {
            sim_lcd_shadow[sim_lcd_addr % (CSE_LCD_WIDTH * CSE_LCD_HEIGHT)] =
                (uint16_t)(cur | value);
            sim_lcd_addr++;
        }
        sim_data_hi = !sim_data_hi;
    }
}
#else /* real hardware: SDCC __sfr port I/O compiles to Z80 IN/OUT */

__sfr __at 0x01 __port_keypad;
__sfr __at 0x03 __port_int_mask;
__sfr __at 0x04 __port_int_trig;
__sfr __at 0x05 __port_ram_page;
__sfr __at 0x06 __port_banka;
__sfr __at 0x07 __port_bankb;
__sfr __at 0x0E __port_mema_high;
__sfr __at 0x0F __port_memb_high;
__sfr __at 0x10 __port_lcd_cmd;
__sfr __at 0x11 __port_lcd_data;
__sfr __at 0x20 __port_cpuspeed;
__sfr __at 0x39 __port_gpio_config;
__sfr __at 0x3A __port_gpio_rw;
uint8_t cse_port_in(uint8_t port)
{
    switch (port) {
        case PORT_KEYPAD:     return __port_keypad;
        case PORT_GPIO_RW:    return __port_gpio_rw;
        default:              return 0xFF;   /* unmapped reads: see VERIFY */
    }
}

void cse_port_out(uint8_t port, uint8_t value)
{
    switch (port) {
        case PORT_KEYPAD:         __port_keypad = value; break;
        case PORT_INT_MASK:       __port_int_mask = value; break;
        case PORT_INT_TRIG:       __port_int_trig = value; break;
        case PORT_RAM_PAGING:     __port_ram_page = value; break;
        case PORT_BANKA:          __port_banka = value; break;
        case PORT_BANKB:          __port_bankb = value; break;
        case PORT_MEMA_HIGH:      __port_mema_high = value; break;
        case PORT_MEMB_HIGH:      __port_memb_high = value; break;
        case PORT_LCD_CMD:        __port_lcd_cmd = value; break;
        case PORT_LCD_DATA:       __port_lcd_data = value; break;
        case PORT_CPUSPEED:       __port_cpuspeed = value; break;
        case PORT_GPIO_CONFIG:    __port_gpio_config = value; break;
        case PORT_GPIO_RW:        __port_gpio_rw = value; break;
        default: break;                          /* ignore unmapped writes */
    }
}

#endif /* MUNT386_HW_EMULATE */

/* ------------------------------------------------------------------ */
/* LCD primitives (protocol identical in both builds)                  */
/* ------------------------------------------------------------------ */

void cse_lcd_write_reg(uint8_t reg, uint16_t value)
{
    cse_port_out(PORT_LCD_CMD, reg);
    cse_port_out(PORT_LCD_CMD, reg);          /* index twice (documented) */
    cse_port_out(PORT_LCD_DATA, (uint8_t)(value >> 8));
    cse_port_out(PORT_LCD_DATA, (uint8_t)(value & 0xFF));
}

/* ------------------------------------------------------------------ */
/* Keypad scan primitives (rows driven, columns read, active low)      */
/* ------------------------------------------------------------------ */

void cse_keypad_start(void)
{
    cse_port_out(PORT_KEYPAD, 0xFF);          /* clear drive */
}

uint8_t cse_keypad_read_group(uint8_t group_mask)
{
    cse_port_out(PORT_KEYPAD, group_mask);
    for (volatile int i = 0; i < 8; i++) { /* drive settle */ }
    return cse_port_in(PORT_KEYPAD);
}

void cse_keypad_end(void)
{
    cse_port_out(PORT_KEYPAD, 0xFF);
}

/* ------------------------------------------------------------------ */
/* Time keeping                                                        */
/* ------------------------------------------------------------------ */

/* Software timer fed by the crystal-timer interrupt (im 2 vector table on
 * device).  The emulated build's test harness advances it directly. */
static volatile uint32_t timer_ticks;

void cse_timer_interrupt(void)
{
    timer_ticks++;
}

uint32_t cse_timer_ticks(void)
{
    return timer_ticks;
}

uint64_t platform_time_us(void)
{
    /* VERIFY: measure the real timer-1 period on device and replace this
     * constant.  100 Hz is the conservative minimum; using it keeps guest
     * time from running fast if the real period is longer. */
    return ((uint64_t)timer_ticks * 1000000ull) / CSE_TICK_HZ_MIN;
}

void platform_delay_us(uint32_t us)
{
    uint64_t end = platform_time_us() + us;
    while (platform_time_us() < end) { /* busy wait */ }
}

/* ------------------------------------------------------------------ */
/* CPU speed (port 0x20, speed index: 0 = 6 MHz, 1 = 15 MHz)            */
/* ------------------------------------------------------------------ */
/* 15 MHz (index 1) is the highest stable software-selectable speed on the
 * CSE.  Indexes 2/3 were intended for 20/25 MHz but left unimplemented
 * before production: they measure ~15.0 MHz on real silicon with different
 * delay-state defaults (ports 0x29-0x2F) that can break LCD timing and
 * opcode fetch, and newer ASICs ignore them entirely.  Reaching the hidden
 * 20/25 MHz modes requires soldering to the ASIC and is unstable above
 * ~22-23 MHz (flash rated 20 MHz) - out of scope for firmware.
 * See cse_hardware.h and docs/CSE_HARDWARE.md for the sources. */
int cse_cpu_speed_set(int index)
{
    if (index != CPUSPEED_6MHZ && index != CPUSPEED_15MHZ) return -1;
    cse_port_out(PORT_CPUSPEED, (uint8_t)index);
    return cse_cpu_speed_get();
}

int cse_cpu_speed_get(void)
{
    int v = cse_port_in(PORT_CPUSPEED) & 0x03;
    if (v != CPUSPEED_6MHZ && v != CPUSPEED_15MHZ) return -1;
    return v;
}

/* ------------------------------------------------------------------ */
/* Boot-time hardware initialisation                                   */
/* ------------------------------------------------------------------ */

void cse_hw_early_init(void)
{
    /* Memory-timer speed field in the interrupt-trigger register. */
    cse_port_out(PORT_INT_TRIG, (uint8_t)(3u << 2));
    cse_port_out(PORT_MEMA_HIGH, 0x00);
    cse_port_out(PORT_MEMB_HIGH, 0x00);

    /* Bank layout: 0x0000 flash p0, 0x4000 flash p1,
     * 0x8000/0xC000 RAM pages (bank B selects RAM page 1). */
    cse_port_out(PORT_BANKB, (uint8_t)(1u | BANKB_ISRAM_CPU15));

    /* CPU to the highest stable speed: 15 MHz = speed index 1 on port 0x20.
     * (Index 0x02 would select the undocumented, unimplemented 20 MHz mode
     * -- see cse_hardware.h.  BANKB_ISRAM_CPU15 above is the bank-B RAM flag
     * that also feeds the 15 MHz clock domain; it is independent of this
     * port value.) */
    cse_port_out(PORT_CPUSPEED, CPUSPEED_HIGHEST);

    /* Enable the ON-key interrupt line only; timer IRQs are enabled after
     * the diagnostic phase so bring-up stays deterministic. */
    cse_port_out(PORT_INT_MASK, INT_ON);
}

void cse_backlight(int on)
{
    cse_port_out(PORT_GPIO_CONFIG, 0xE0);
    uint8_t v = cse_port_in(PORT_GPIO_RW);
    if (on) v |= GPIO_RW_BACKLIGHT;
    else    v = (uint8_t)(v & ~GPIO_RW_BACKLIGHT);
    cse_port_out(PORT_GPIO_RW, v);
}
