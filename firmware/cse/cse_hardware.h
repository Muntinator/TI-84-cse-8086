/*
 * Munt386-CSE -- TI-84 Plus CSE hardware register definitions.
 *
 * Constants verified against the KnightOS kernel sources vendored at
 * reference/kernel-master (MIT; facts only, no code copied):
 *   include/constants.asm      -- bit positions
 *   src/00/boot.asm            -- bank/RAM layout at boot
 *   src/00/display-color.asm   -- LCD command/data protocol
 *   src/00/keyboard.asm        -- keypad scan pattern
 *   Makefile (TI84pCSE target) -- flash size, boot/privileged pages
 *
 * Items that still require on-device confirmation are marked VERIFY and are
 * never assumed by a code path that could damage the calculator.
 */
#ifndef CSE_HARDWARE_H
#define CSE_HARDWARE_H

#include <stdint.h>

/* ---------------- I/O ports ---------------- */
#define PORT_KEYPAD          0x01
#define PORT_INT_MASK        0x03
#define PORT_INT_TRIG        0x04
#define PORT_RAM_PAGING      0x05    /* selects SRAM page in 0x8000 window */
#define PORT_BANKA           0x06    /* flash page in 0x0000 window (low)  */
#define PORT_BANKB           0x07    /* flash page in 0x4000 window (low)  */
#define PORT_MEMA_HIGH       0x0E    /* bank A high bit                    */
#define PORT_MEMB_HIGH       0x0F    /* bank B high bit                    */
#define PORT_LCD_CMD         0x10    /* index written twice per access     */
#define PORT_LCD_DATA        0x11    /* 16-bit data, high byte first       */
#define PORT_FLASHRWCONTROL  0x14
#define PORT_CPUSPEED        0x20
#define PORT_FLASHRAMSIZE    0x21
#define PORT_CRYS1_FREQ      0x30
#define PORT_CRYS1_LOOP      0x33
#define PORT_CRYS1_COUNTER   0x36
#define PORT_GPIO_CONFIG     0x39
#define PORT_GPIO_RW         0x3A

/* ---------------- Bit masks ---------------- */
#define INT_ON               0x01
#define INT_TIMER1           0x02
#define INT_TIMER2           0x04
#define MEM_TIMER_SPEED      0x01    /* mask index 1 per constants.asm     */
#define BANKB_ISRAM_CPU15    0x80    /* bank B = RAM, CPU raised to 15 MHz */
/* Port 0x20 (PORT_CPUSPEED) takes the SPEED INDEX, not a bitmask:
 *   0 = 6 MHz, 1 = 15 MHz (constants.asm CPUSPEED_6MHZ/CPUSPEED_15MHZ).
 * Values 2 and 3 exist on the register but were intended for 20/25 MHz and
 * left unimplemented before production: measured ~15.0 MHz on real silicon,
 * with different default delay states (ports 0x29-0x2F) that can break LCD
 * timing/opcode fetch.  Newer ASICs show no difference at all.  15 MHz via
 * index 1 IS the highest stable software-selectable speed.  (WikiTI
 * "83Plus:Ports:20", crystal-timer measurements; matches the KnightOS
 * comment "there are also 2 and 3, but they should not be used".) */
#define CPUSPEED_6MHZ        0x00    /* speed index 0 (6.09 MHz measured)  */
#define CPUSPEED_15MHZ       0x01    /* speed index 1 (~14.97 MHz measured) */
#define CPUSPEED_HIGHEST     CPUSPEED_15MHZ
#define FLASHRWCONTROL_ENABLEWRITE 0x01
#define GPIO_RW_BACKLIGHT    0x20

/* ---------------- Colour LCD registers ---------------- */
#define LCDR_DRIVER_OUTCTRL1   0x01
#define LCDR_LCDDRIVING_CTRL   0x02
#define LCDR_ENTRYMODE         0x03
#define LCDR_DISPCONTROL1      0x07
#define LCDR_DISPCONTROL2      0x08
#define LCDR_DISPCONTROL3      0x09
#define LCDR_DISPCONTROL4      0x0A
#define LCDR_RGBDISP_IFACE     0x0C
#define LCDR_FRAMEMARKER       0x0D
#define LCDR_POWERCONTROL1     0x10
#define LCDR_POWERCONTROL2     0x11
#define LCDR_POWERCONTROL3     0x12
#define LCDR_POWERCONTROL4     0x13
#define LCDR_CURSOR_ROW        0x20
#define LCDR_CURSOR_COLUMN     0x21
#define LCDR_GRAM              0x22
#define LCDR_POWERCONTROL7     0x29
#define LCDR_FRAMERATE         0x2B
#define LCDR_GAMMA1            0x30
#define LCDR_GAMMA2            0x31
#define LCDR_GAMMA3            0x32
#define LCDR_GAMMA4            0x35
#define LCDR_GAMMA5            0x36
#define LCDR_GAMMA6            0x37
#define LCDR_GAMMA7            0x38
#define LCDR_GAMMA8            0x39
#define LCDR_GAMMA9            0x3C
#define LCDR_GAMMA10           0x3D
#define LCDR_WIN_HORIZ_START   0x50
#define LCDR_WIN_HORIZ_END     0x51
#define LCDR_WIN_VERT_START    0x52
#define LCDR_WIN_VERT_END      0x53
#define LCDR_GATESCAN_CTRL     0x60
#define LCDR_BASEIMAGE_CTRL    0x61
#define LCDR_VERTSCROLL_CTRL   0x6A
#define LCDR_PANEL_IFACE1      0x90
#define LCDR_PANEL_IFACE2      0x92
#define LCDR_PANEL_IFACE4      0x95
#define LCDR_PANEL_IFACE5      0x97

/* ---------------- Geometry ---------------- */
#define CSE_LCD_WIDTH   320
#define CSE_LCD_HEIGHT  240

/* ---------------- Flash layout ---------------- */
#define CSE_FLASH_PAGES      256u    /* 4 MiB / 16 KiB  */
#define CSE_FLASH_PAGE_SIZE  16384u
#define CSE_BOOT_PAGE        0xFFu   /* 0x3FC000 -- never touch */
#define CSE_PRIVILEGED_PAGE  0xFCu   /* 0x3F0000 -- never touch */
/* Reserved region for future persistent virtual-disk storage.
 * Deliberately below the KnightOS swap sectors (0xF7/0xF8) and the
 * OS-critical pages. */
#define CSE_STORAGE_FIRST_PAGE 0xECu
#define CSE_STORAGE_LAST_PAGE  0xF3u

/* Crystal timer 1 mode 3: ~111 Hz tick (loop=0 => fastest documented rate).
 * VERIFY: exact period on hardware; software assumes >= 100 Hz. */
#define CSE_TICK_HZ_MIN      100u

/* Returns the port-0x20 speed index currently in effect (read-back, device
 * and simulator), or -1 if the port does not read back. */
int cse_cpu_speed_get(void);
/* Selects the speed index and returns the read-back value for confirmation. */
int cse_cpu_speed_set(int index);

#endif /* CSE_HARDWARE_H */
