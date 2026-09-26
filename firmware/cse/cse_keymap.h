/*
 * Munt386-CSE -- TI-84 Plus CSE keypad to PC set-1 scancode translation.
 *
 * Architecture required by the spec:
 *
 *     CSE keypad (port 1, active-low matrix)
 *         -> cse_keymap.h translation table
 *             -> PC set-1 scancodes (make + break)
 *                 -> kbd_push_scancode()  [virtual 8042, src/machine.c]
 *                     -> IRQ1 -> BIOS ring buffer -> guest software
 *
 * Modifier mapping (documented limitation set -- see docs/PORT_MAP.md):
 *   2nd       -> Left Shift   (make 0x2A / break 0xAA)
 *   Alpha-lock-> holds Shift while active
 *   5th       -> Left Ctrl    (make 0x1D / break 0x9D)
 *   4th       -> Left Alt     (make 0x38 / break 0xB8)
 */
#ifndef CSE_KEYMAP_H
#define CSE_KEYMAP_H

#include <stdint.h>

/* PC set-1 scancodes used by the map. */
#define SC_NONE      0x00
#define SC_ESC       0x01
#define SC_1         0x02
#define SC_2         0x03
#define SC_3         0x04
#define SC_4         0x05
#define SC_5         0x06
#define SC_6         0x07
#define SC_7         0x08
#define SC_8         0x09
#define SC_9         0x0A
#define SC_0         0x0B
#define SC_MINUS     0x0C
#define SC_EQUALS    0x0D
#define SC_BACKSPACE 0x0E
#define SC_TAB       0x0F
#define SC_Q         0x10
#define SC_W         0x11
#define SC_E         0x12
#define SC_R         0x13
#define SC_T         0x14
#define SC_Y         0x15
#define SC_U         0x16
#define SC_I         0x17
#define SC_O         0x18
#define SC_P         0x19
#define SC_LBRACK    0x1A
#define SC_RBRACK    0x1B
#define SC_ENTER     0x1C
#define SC_LCTRL     0x1D
#define SC_A         0x1E
#define SC_S         0x1F
#define SC_D         0x20
#define SC_F         0x21
#define SC_G         0x22
#define SC_H         0x23
#define SC_J         0x24
#define SC_K         0x25
#define SC_L         0x26
#define SC_SEMICOLON 0x27
#define SC_QUOTE     0x28
#define SC_GRAVE     0x29
#define SC_LSHIFT    0x2A
#define SC_BACKSLASH 0x2B
#define SC_Z         0x2C
#define SC_X         0x2D
#define SC_C         0x2E
#define SC_V         0x2F
#define SC_B         0x30
#define SC_N         0x31
#define SC_M         0x32
#define SC_COMMA     0x33
#define SC_DOT       0x34
#define SC_SLASH     0x35
#define SC_RSHIFT    0x36
#define SC_KP_MUL    0x37
#define SC_LALT      0x38
#define SC_SPACE     0x39
#define SC_F1        0x3B
#define SC_F2        0x3C
#define SC_F3        0x3D
#define SC_F4        0x3E
#define SC_F5        0x3F
#define SC_F6        0x40
#define SC_F7        0x41
#define SC_F8        0x42

/* Extended keys arrive as 0xE0 followed by the "make" byte below. */
#define SC_EXT_PREFIX 0xE0
#define SC_EXT_UP     0x48
#define SC_EXT_DOWN   0x50
#define SC_EXT_LEFT   0x4B
#define SC_EXT_RIGHT  0x4D

/* Modifier scancodes (used to synthesise make/break around a key). */
#define SC_MOD_SHIFT 0x2A
#define SC_MOD_CTRL  0x1D
#define SC_MOD_ALT   0x38

/* CSE keypad logical keys (bit positions in the scan result). */
enum {
    CSEK_DOWN = 0, CSEK_LEFT, CSEK_RIGHT, CSEK_UP,
    CSEK_ENTER,    CSEK_PLUS, CSEK_MINUS, CSEK_MUL,
    CSEK_DIV,      CSEK_CARET, CSEK_CLEAR, CSEK_NEG,
    CSEK_3,        CSEK_6,     CSEK_9,     CSEK_RPAREN,
    CSEK_2,        CSEK_5,     CSEK_8,     CSEK_LPAREN,
    CSEK_1,        CSEK_4,     CSEK_7,     CSEK_COMMA,
    CSEK_SIN,      CSEK_COS,   CSEK_TAN,   CSEK_PI,
    CSEK_STATS,    CSEK_APPPS, CSEK_GRAPH, CSEK_TRACE,
    CSEK_2ND,      CSEK_MODE,  CSEK_DEL,   CSEK_ALPHA,
    CSEK_XTON,     CSEK_STO,   CSEK_LN,    CSEK_LOG,
    CSEK_SQUARE,   CSEK_COMMA2,CSEK_RECIP, CSEK_MATH,
    CSEK_UP2,      CSEK_WINDOW,CSEK_ZOOM,  CSEK_YEQ,
    CSEK_ON,       CSEK_5TH,   CSEK_4TH,   CSEK_SPACE,
    CSEK_COUNT
};

/* Modifier flag bits for the translation state. */
#define CSEMOD_SHIFT 0x01
#define CSEMOD_CTRL  0x02
#define CSEMOD_ALT   0x04

/* Base scancode for each logical CSE key. 0x00 = not mapped. */
extern const uint8_t cse_keymap_base[CSEK_COUNT];

/* Modifier requirement per key (0 = none, else CSEMOD_*). */
extern const uint8_t cse_keymap_mod[CSEK_COUNT];

/* Scancode emitted when a key is pressed with 2nd held (F-key row). */
extern const uint8_t cse_keymap_2nd[CSEK_COUNT];

/* Text labels for the diagnostic screen / ini bridge. */
extern const char *const cse_keymap_names[CSEK_COUNT];

/* Scancode -> ASCII table (set 1), shared with the keymap's ini bridge. */
extern const char cse_sc_ascii[128];
extern const char cse_sc_ascii_shift[128];

/* Simple PC-keyboard ini description for tiny386-style config parity. */
typedef struct {
    const char *key;    /* CSE key name  */
    const char *pc;     /* PC key name   */
    uint8_t     sc;     /* set-1 scancode */
} cse_keymap_entry;

#define CSE_KEYMAP_MAX 48
typedef struct {
    cse_keymap_entry entries[CSE_KEYMAP_MAX];
    uint8_t count;
} cse_keymap_ini;

/* Build the tiny386-ini-compatible key mapping description. */
void cse_keys_get_keymap(cse_keymap_ini *out);

#endif /* CSE_KEYMAP_H */
