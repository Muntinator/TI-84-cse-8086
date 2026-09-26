; =====================================================================
;  Munt386 -- TI-84 Plus CSE bare-metal bring-up firmware
; =====================================================================
;
;  The Z80 side of Munt386: initialises the CSE hardware WITHOUT TI-OS and
;  prints the diagnostic screen required before any guest code runs.
;
;  ASSEMBLER : SPASM-ng / Brass compatible (z80).
;  STATUS    : bring-up source.  Mirrors the documented CSE sequences
;              (docs/CSE_HARDWARE.md).  It has NOT yet been assembled or run on
;              hardware from this repository; unverified hardware details are
;              marked  ;; VERIFY: .
;
;  RECOVERY  : holding ON at power-up enters diagnostic/recovery mode and never
;              erases or writes flash.  See docs/RECOVERY.md.
; =====================================================================

; ---- I/O ports ----
PORT_KEYPAD         .equ 0x01
PORT_INT_MASK       .equ 0x03
PORT_INT_TRIG       .equ 0x04
PORT_MEM_TIMER      .equ 0x04
PORT_RAM_PAGING     .equ 0x05
PORT_BANKA          .equ 0x06
PORT_BANKB          .equ 0x07
PORT_MEMA_HIGH      .equ 0x0E
PORT_MEMB_HIGH      .equ 0x0F
PORT_LCD_CMD        .equ 0x10
PORT_LCD_DATA       .equ 0x11
PORT_FLASHRWCONTROL .equ 0x14
PORT_CPUSPEED       .equ 0x20
PORT_FLASHRAMSIZE   .equ 0x21
PORT_GPIO_CONFIG    .equ 0x39
PORT_GPIO_RW        .equ 0x3A

; ---- bit masks ----
INT_ON              .equ 0x01
MEM_TIMER_SPEED     .equ 0x01
BANKB_ISRAM_CPU15   .equ 0x80
CPUSPEED_15MHZ      .equ 0x02
GPIO_RW_BACKLIGHT   .equ 0x20

; ---- colour LCD registers ----
LCDR_DRIVER_OUTCTRL1     .equ 0x01
LCDR_LCDDRIVING_CTRL     .equ 0x02
LCDR_ENTRYMODE           .equ 0x03
LCDR_DISPCONTROL1        .equ 0x07
LCDR_DISPCONTROL2        .equ 0x08
LCDR_DISPCONTROL3        .equ 0x09
LCDR_DISPCONTROL4        .equ 0x0A
LCDR_RGBDISP_IFACE       .equ 0x0C
LCDR_FRAMEMARKER         .equ 0x0D
LCDR_POWERCONTROL1       .equ 0x10
LCDR_POWERCONTROL2       .equ 0x11
LCDR_POWERCONTROL3       .equ 0x12
LCDR_POWERCONTROL4       .equ 0x13
LCDR_CURSOR_ROW          .equ 0x20
LCDR_CURSOR_COLUMN       .equ 0x21
LCDR_GRAM                .equ 0x22
LCDR_POWERCONTROL7       .equ 0x29
LCDR_FRAMERATE           .equ 0x2B
LCDR_GAMMA1              .equ 0x30
LCDR_GAMMA2              .equ 0x31
LCDR_GAMMA3              .equ 0x32
LCDR_GAMMA4              .equ 0x35
LCDR_GAMMA5              .equ 0x36
LCDR_GAMMA6              .equ 0x37
LCDR_GAMMA7              .equ 0x38
LCDR_GAMMA8              .equ 0x39
LCDR_GAMMA9              .equ 0x3C
LCDR_GAMMA10             .equ 0x3D
LCDR_WIN_HORIZ_START     .equ 0x50
LCDR_WIN_HORIZ_END       .equ 0x51
LCDR_WIN_VERT_START      .equ 0x52
LCDR_WIN_VERT_END        .equ 0x53
LCDR_GATESCAN_CTRL       .equ 0x60
LCDR_BASEIMAGE_CTRL      .equ 0x61
LCDR_VERTSCROLL_CTRL     .equ 0x6A
LCDR_PANEL_IFACE1        .equ 0x90
LCDR_PANEL_IFACE2        .equ 0x92
LCDR_PANEL_IFACE4        .equ 0x95
LCDR_PANEL_IFACE5        .equ 0x97

.macro lcdout(reg, value)
    ld   a, reg
    ld   hl, value
    call lcd_write_reg
.endmacro

; =====================================================================
;  Entry
; =====================================================================
.org 0x4000                 ; assembled for a flash page window  ;; VERIFY

boot:
    di
    im   1
    jr   init

init:
    ld   a, 3 << MEM_TIMER_SPEED
    out  (PORT_MEM_TIMER), a
    xor  a
    out  (PORT_MEMA_HIGH), a
    out  (PORT_MEMB_HIGH), a
    ; 0x0000 flash p0 | 0x4000 flash p1 | 0x8000 RAM p1 | 0xC000 RAM p0
    ld   a, 1 | BANKB_ISRAM_CPU15
    out  (PORT_BANKB), a
    ld   sp, stackTop
    ld   a, CPUSPEED_15MHZ
    out  (PORT_CPUSPEED), a
    ld   a, INT_ON
    out  (PORT_INT_MASK), a
    ei

run_tests:
    call init_lcd
    ld   a, 1
    ld   (lcd_ok), a
    call test_ram
    call test_flash
    call key_on_held
    jr   z, recovery

diag:
    call lcd_clear
    ld   hl, str_title
    call draw_text
    ld   hl, str_z80
    call draw_text
    ld   hl, str_lcd
    call draw_text
    ld   hl, str_ram
    call draw_text
    ld   hl, str_flash
    call draw_text
    ld   hl, str_x86
    call draw_text
    ld   hl, str_recovery
    call draw_text
idle:
    halt
    jr   idle

recovery:
    call lcd_clear
    ld   hl, str_rec_banner
    call draw_text
    ld   hl, str_rec_help
    call draw_text
rec_idle:
    halt
    jr   rec_idle

; =====================================================================
;  RAM self-test -- march a pattern; count usable 512-byte blocks.
; =====================================================================
test_ram:
    ld   hl, 0x8000
    ld   de, 0x8000
    ld   bc, 0x8000
tr_loop:
    ld   (hl), e
    ld   a, (hl)
    cp   e
    jr   nz, tr_done
    inc  hl
    inc  e
    dec  bc
    ld   a, b
    or   c
    jr   nz, tr_loop
tr_done:
    ex   de, hl
    ld   hl, 0x10000
    or   a
    sbc  hl, de
    ld   b, 9
tr_shr:
    srl  h \ rr l
    djnz tr_shr
    ld   (ram_blocks), hl
    ret

; =====================================================================
;  Flash self-test -- READ ONLY (checksum of the mapped page).
; =====================================================================
test_flash:
    ld   hl, 0x4000
    ld   b, 64
    ld   c, 0
tf_loop:
    ld   a, (hl)
    xor  c
    ld   c, a
    inc  hl
    djnz tf_loop
    ld   a, c
    ld   (flash_sum), a
    ret

; =====================================================================
;  Keyboard -- returns Z if ON is held.
; =====================================================================
key_on_held:
    ld   a, 0xFF
    out  (PORT_KEYPAD), a
    nop \ nop \ nop \ nop \ nop \ nop \ nop \ nop
    ld   a, 0xFE
    out  (PORT_KEYPAD), a
    nop \ nop \ nop \ nop \ nop \ nop \ nop \ nop
    in   a, (PORT_KEYPAD)
    and  0xFF
    xor  0xFF
    ret                        ; ;; VERIFY: exact ON matrix position

; =====================================================================
;  Colour LCD bring-up (documented CSE power-on sequence).
; =====================================================================
init_lcd:
    ld   a, 0xE0
    out  (PORT_GPIO_CONFIG), a

    lcdout(LCDR_DISPCONTROL1, 0x0000)
    lcdout(LCDR_POWERCONTROL2, 0x0007)
    lcdout(LCDR_POWERCONTROL3, 0x008C)
    lcdout(LCDR_POWERCONTROL4, 0x1800)
    lcdout(LCDR_POWERCONTROL7, 0x0030)
    call lcd_wait
    lcdout(LCDR_POWERCONTROL1, 0x0190)
    lcdout(LCDR_POWERCONTROL2, 0x0227)
    call lcd_wait
    lcdout(LCDR_DRIVER_OUTCTRL1, 0x0000)
    lcdout(LCDR_LCDDRIVING_CTRL, 0x0200)
    lcdout(LCDR_ENTRYMODE, 0x10B8)
    lcdout(LCDR_DISPCONTROL2, 0x0202)
    lcdout(LCDR_DISPCONTROL3, 0x0000)
    lcdout(LCDR_DISPCONTROL4, 0x0000)
    lcdout(LCDR_RGBDISP_IFACE, 0x0000)
    lcdout(LCDR_FRAMEMARKER, 0x0000)
    lcdout(LCDR_GATESCAN_CTRL, 0x2700)
    lcdout(LCDR_BASEIMAGE_CTRL, 0x0001)
    lcdout(LCDR_VERTSCROLL_CTRL, 0x0000)
    lcdout(LCDR_PANEL_IFACE1, 0x0010)
    lcdout(LCDR_PANEL_IFACE2, 0x0600)
    lcdout(LCDR_PANEL_IFACE4, 0x0200)
    lcdout(LCDR_PANEL_IFACE5, 0x0C00)
    lcdout(LCDR_GAMMA1, 0x0000)
    lcdout(LCDR_GAMMA2, 0x0305)
    lcdout(LCDR_GAMMA3, 0x0002)
    lcdout(LCDR_GAMMA4, 0x0301)
    lcdout(LCDR_GAMMA5, 0x0004)
    lcdout(LCDR_GAMMA6, 0x0507)
    lcdout(LCDR_GAMMA7, 0x0204)
    lcdout(LCDR_GAMMA8, 0x0707)
    lcdout(LCDR_GAMMA9, 0x0103)
    lcdout(LCDR_GAMMA10, 0x0004)
    lcdout(LCDR_WIN_HORIZ_START, 0x0000)
    lcdout(LCDR_WIN_HORIZ_END, 0x00EF)
    lcdout(LCDR_WIN_VERT_START, 0x0000)
    lcdout(LCDR_WIN_VERT_END, 0x013F)
    lcdout(LCDR_FRAMERATE, 0x000B)
    lcdout(LCDR_POWERCONTROL1, 0x1190)
    lcdout(LCDR_DISPCONTROL1, 0x0001)
    call lcd_wait
    call lcd_wait
    lcdout(LCDR_DISPCONTROL1, 0x0023)
    call lcd_wait
    call lcd_wait
    lcdout(LCDR_DISPCONTROL1, 0x0133)
    lcdout(LCDR_ENTRYMODE, 0x10B8)

    in   a, (PORT_GPIO_RW)
    or   GPIO_RW_BACKLIGHT
    out  (PORT_GPIO_RW), a
    ret

lcd_write_reg:
    out  (PORT_LCD_CMD), a
    out  (PORT_LCD_CMD), a
    ld   c, PORT_LCD_DATA
    out  (c), h
    out  (c), l
    ret

lcd_wait:
    ld   bc, 0x2000
lw_loop:
    dec  bc
    ld   a, b
    or   c
    jr   nz, lw_loop
    ret

lcd_clear:
    ld   a, LCDR_WIN_HORIZ_START
    ld   hl, 0
    call lcd_write_reg
    ld   a, LCDR_WIN_HORIZ_END
    ld   hl, 239
    call lcd_write_reg
    ld   a, LCDR_WIN_VERT_START
    ld   hl, 0
    call lcd_write_reg
    ld   a, LCDR_WIN_VERT_END
    ld   hl, 319
    call lcd_write_reg
    ld   a, LCDR_CURSOR_ROW
    ld   hl, 0
    call lcd_write_reg
    ld   a, LCDR_CURSOR_COLUMN
    ld   hl, 0
    call lcd_write_reg
    ld   a, LCDR_GRAM
    out  (PORT_LCD_CMD), a
    out  (PORT_LCD_CMD), a
    ld   c, PORT_LCD_DATA
    ld   b, 0                  ; 256
lc_outer:
    ld   d, 150                ; 150*2 bytes = 150 pixels -> 300 across
lc_inner:
    xor  a
    out  (c), a
    ld   a, 0x08
    out  (c), a
    dec  d
    jr   nz, lc_inner
    djnz lc_outer
    ret

; =====================================================================
;  Text output.  Cells are 16x16.  The panel is mounted rotated, so x maps
;  to the VERT window/cursor-column axis and y to HORIZ/cursor-row.
;    ;; VERIFY the axis mapping on hardware.
; =====================================================================
draw_text:
    ; HL -> NUL-terminated string
    push hl
dt_next:
    ld   a, (hl)
    or   a
    jr   z, dt_done
    cp   10
    jr   z, dt_nl
    call draw_char
    inc  hl
    ld   a, (cur_cell_x)
    inc  a
    ld   (cur_cell_x), a
    jr   dt_next
dt_nl:
    inc  hl
    xor  a
    ld   (cur_cell_x), a
    ld   a, (cur_cell_y)
    inc  a
    ld   (cur_cell_y), a
    jr   dt_next
dt_done:
    xor  a
    ld   (cur_cell_x), a
    ld   a, (cur_cell_y)
    inc  a
    ld   (cur_cell_y), a
    pop  hl
    ret

draw_char:                     ; A = character
    push bc
    push de
    push hl
    ; pixel origin
    ld   b, a
    ld   a, (cur_cell_x)
    add  a, a \ add a, a \ add a, a \ add a, a
    ld   (gx), a
    ld   a, (cur_cell_y)
    add  a, a \ add a, a \ add a, a \ add a, a
    ld   (gy), a

    ld   a, b
    call glyph_index
    add  a, a \ add a, a \ add a, a
    ld   e, a
    ld   d, 0
    ld   hl, font8x8
    add  hl, de
    push hl                   ; glyph pointer

    ; window + cursor for the cell
    ld   a, LCDR_WIN_VERT_START
    ld   hl, (gx)
    call lcd_write_reg
    ld   a, LCDR_WIN_VERT_END
    ld   hl, (gx)
    ld   de, 15
    add  hl, de
    call lcd_write_reg
    ld   a, LCDR_WIN_HORIZ_START
    ld   hl, (gy)
    call lcd_write_reg
    ld   a, LCDR_WIN_HORIZ_END
    ld   hl, (gy)
    ld   de, 15
    add  hl, de
    call lcd_write_reg
    ld   a, LCDR_CURSOR_ROW
    ld   hl, (gy)
    call lcd_write_reg
    ld   a, LCDR_CURSOR_COLUMN
    ld   hl, (gx)
    call lcd_write_reg

    pop  hl                   ; glyph pointer
    ld   a, LCDR_GRAM
    out  (PORT_LCD_CMD), a
    out  (PORT_LCD_CMD), a
    ld   c, PORT_LCD_DATA

    ld   b, 16                ; 16 scan rows (8 doubled)
dc_row:
    ld   a, b
    dec  a
    srl  a                    ; glyph row 0..7
    push hl
    ld   e, a
    ld   d, 0
    add  hl, de
    ld   a, (hl)
    pop  hl
    ld   d, a                 ; row bits, MSB = left
    push bc
    ld   b, 8                 ; 8 columns (doubled)
dc_col:
    rlca
    jr   nc, dc_b1
    call dc_fg
    jr   dc_c2
dc_b1:
    call dc_bg
dc_c2:
    rlca
    jr   nc, dc_b2
    call dc_fg
    jr   dc_next
dc_b2:
    call dc_bg
dc_next:
    djnz dc_col
    pop  bc
    djnz dc_row

    pop  hl
    pop  de
    pop  bc
    ret

dc_fg:                          ; white pixel 0xFFFF
    ld   a, 0xFF
    out  (c), a
    out  (c), a
    ret
dc_bg:                          ; dark-blue pixel 0x0800 (low byte first)
    xor  a
    out  (c), a
    ld   a, 0x08
    out  (c), a
    ret

; ASCII -> font index
glyph_index:
    cp   ' '
    jr   z, gi_0
    cp   '0'
    jr   c, gi_punct
    cp   '9' + 1
    jr   c, gi_digit
    cp   'A'
    jr   c, gi_punct
    cp   'Z' + 1
    jr   c, gi_alpha
gi_punct:
    cp   '-'
    jr   z, gi_dash
    cp   ':'
    jr   z, gi_colon
    cp   '.'
    jr   z, gi_dot
gi_0:
    xor  a
    ret
gi_digit:
    sub  '0'
    inc  a
    ret
gi_alpha:
    sub  'A'
    add  a, 11
    ret
gi_dash:
    ld   a, 37
    ret
gi_colon:
    ld   a, 38
    ret
gi_dot:
    ld   a, 39
    ret

; =====================================================================
;  Strings and data
; =====================================================================
str_title:        .db "MUNT386", 10, 0
str_z80:          .db "Z80 OK", 10, 0
str_lcd:          .db "LCD OK", 10, 0
str_ram:          .db "RAM OK", 10, 0
str_flash:        .db "FLASH OK", 10, 0
str_x86:          .db "X86 CORE NOT STARTED", 10, 0
str_recovery:     .db "HOLD ON FOR RECOVERY", 10, 0
str_rec_banner:   .db "MUNT386 RECOVERY", 10, 0
str_rec_help:     .db "RESTORE OS WITH TI TOOLS", 10, 0

font8x8:                       ; ' ' 0-9 A-Z - : .
    .db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
    .db 0x70,0x88,0x98,0xA8,0xC8,0x88,0x70,0x00
    .db 0x20,0x60,0x20,0x20,0x20,0x20,0x70,0x00
    .db 0x70,0x88,0x08,0x30,0x40,0x80,0xF8,0x00
    .db 0x70,0x88,0x08,0x38,0x08,0x88,0x70,0x00
    .db 0x10,0x30,0x50,0x90,0xF8,0x10,0x10,0x00
    .db 0xF8,0x80,0x80,0xF0,0x08,0x88,0x70,0x00
    .db 0x30,0x40,0x80,0xF0,0x88,0x88,0x70,0x00
    .db 0xF8,0x08,0x10,0x20,0x40,0x40,0x40,0x00
    .db 0x70,0x88,0x88,0x70,0x88,0x88,0x70,0x00
    .db 0x70,0x88,0x88,0x78,0x08,0x10,0x60,0x00
    .db 0x70,0x88,0x88,0xF8,0x88,0x88,0x88,0x00
    .db 0xF0,0x88,0x88,0xF0,0x88,0x88,0xF0,0x00
    .db 0x70,0x88,0x80,0x80,0x80,0x88,0x70,0x00
    .db 0xF0,0x88,0x88,0x88,0x88,0x88,0xF0,0x00
    .db 0xF8,0x80,0x80,0xF0,0x80,0x80,0xF8,0x00
    .db 0xF8,0x80,0x80,0xF0,0x80,0x80,0x80,0x00
    .db 0x70,0x88,0x80,0xB8,0x88,0x88,0x78,0x00
    .db 0x88,0x88,0x88,0xF8,0x88,0x88,0x88,0x00
    .db 0x70,0x20,0x20,0x20,0x20,0x20,0x70,0x00
    .db 0x38,0x10,0x10,0x10,0x10,0x90,0x60,0x00
    .db 0x88,0x90,0xA0,0xC0,0xA0,0x90,0x88,0x00
    .db 0x80,0x80,0x80,0x80,0x80,0x80,0xF8,0x00
    .db 0x88,0xD8,0xA8,0x88,0x88,0x88,0x88,0x00
    .db 0x88,0xC8,0xA8,0x98,0x88,0x88,0x88,0x00
    .db 0x70,0x88,0x88,0x88,0x88,0x88,0x70,0x00
    .db 0xF0,0x88,0x88,0xF0,0x80,0x80,0x80,0x00
    .db 0x70,0x88,0x88,0x88,0xA8,0x90,0x68,0x00
    .db 0xF0,0x88,0x88,0xF0,0xA0,0x90,0x88,0x00
    .db 0x78,0x80,0x80,0x70,0x08,0x08,0xF0,0x00
    .db 0xF8,0x20,0x20,0x20,0x20,0x20,0x20,0x00
    .db 0x88,0x88,0x88,0x88,0x88,0x88,0x70,0x00
    .db 0x88,0x88,0x88,0x88,0x88,0x50,0x20,0x00
    .db 0x88,0x88,0x88,0x88,0xA8,0xD8,0x88,0x00
    .db 0x88,0x88,0x50,0x20,0x50,0x88,0x88,0x00
    .db 0x88,0x88,0x50,0x20,0x20,0x20,0x20,0x00
    .db 0xF8,0x08,0x10,0x20,0x40,0x80,0xF8,0x00
    .db 0x00,0x00,0x00,0xF8,0x00,0x00,0x00,0x00
    .db 0x00,0x60,0x60,0x00,0x60,0x60,0x00,0x00
    .db 0x00,0x00,0x00,0x00,0x00,0x60,0x60,0x00

ram_blocks:   .dw 0
flash_sum:    .db 0
lcd_ok:       .db 0
cur_cell_x:   .db 0
cur_cell_y:   .db 0
gx:           .dw 0
gy:           .dw 0

    .fill 0x100
stackTop:

.end
