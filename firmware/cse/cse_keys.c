/*
 * Munt386-CSE -- keypad scanning and PC-scancode translation.
 *
 * Scans the CSE matrix, tracks pressed/released keys between scans, and
 * emits PC set-1 make/break sequences (including the 0xE0 prefix for the
 * arrow group and synthesised Shift/Ctrl/Alt make/break pairs).
 *
 * Emitted bytes go to kbd_push_scancode() on the attached pc_t, or to a
 * test-installed callback (cse_keys_set_emit) so the translation logic is
 * testable without a full machine.
 */
#include "cse_keymap.h"
#include "cse_ports.h"
#include "munt386.h"          /* kbd_push_scancode, pc_t */
#include <string.h>

/* ---------------- translation tables ---------------- */

const uint8_t cse_keymap_base[CSEK_COUNT] = {
    /* down left right up   (extended: emitted with 0xE0 prefix) */
    0x50, 0x4B, 0x4D, 0x48,
    /* enter plus minus2 mul */
    SC_ENTER, SC_EQUALS, 0x00, SC_KP_MUL,
    /* div caret clear neg */
    SC_SLASH, SC_GRAVE, SC_ESC, SC_MINUS,
    /* 3 6 9 rparen */
    SC_3, SC_6, SC_9, SC_0,
    /* 2 5 8 lparen */
    SC_2, SC_5, SC_8, SC_9,
    /* 1 4 7 comma */
    SC_1, SC_4, SC_7, SC_COMMA,
    /* sin cos tan pi */
    SC_S, SC_C, SC_T, SC_P,
    /* stat apps graph trace */
    SC_A, SC_Q, SC_G, SC_R,
    /* 2nd mode del alpha */
    0x00, SC_TAB, SC_BACKSPACE, 0x00,
    /* xton sto ln log */
    SC_X, SC_SEMICOLON, SC_L, SC_K,
    /* square comma2 recip math */
    SC_J, SC_COMMA, SC_I, SC_M,
    /* up2 window zoom yeq */
    0x00, SC_W, SC_Z, SC_Y,
    /* on 5th 4th space */
    0x00, 0x00, 0x00, SC_SPACE,
};

const uint8_t cse_keymap_mod[CSEK_COUNT] = {
    0, 0, 0, 0,
    0, CSEMOD_SHIFT, 0, 0,          /* plus needs Shift for '+'  */
    0, CSEMOD_SHIFT, 0, 0,          /* caret needs Shift for '^' */
    0, 0, 0, CSEMOD_SHIFT,          /* 0+Shift = ')'             */
    0, 0, 0, CSEMOD_SHIFT,          /* 9+Shift = '('             */
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, CSEMOD_CTRL, CSEMOD_ALT, 0,  /* 5th=Ctrl, 4th=Alt         */
};

/* 2nd-function equivalents: the practical F-key row plus extras. */
const uint8_t cse_keymap_2nd[CSEK_COUNT] = {
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, SC_EQUALS,             /* 2nd+(-) = '+'                */
    SC_F3, SC_F6, 0x00, 0x00,
    SC_F2, SC_F5, SC_F8, 0x00,
    SC_F1, SC_F4, SC_F7, 0x00,
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0,
};

const char *const cse_keymap_names[CSEK_COUNT] = {
    "down", "left", "right", "up",
    "enter", "plus", "minus2", "mul",
    "div", "caret", "clear", "neg",
    "3", "6", "9", "rparen",
    "2", "5", "8", "lparen",
    "1", "4", "7", "comma",
    "sin", "cos", "tan", "pi",
    "stat", "apps", "graph", "trace",
    "2nd", "mode", "del", "alpha",
    "xton", "sto", "ln", "log",
    "square", "comma2", "recip", "math",
    "up2", "window", "zoom", "yeq",
    "on", "5th", "4th", "space",
};

const char cse_sc_ascii[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', 8, 9,
    'q','w','e','r','t','y','u','i','o','p','[',']', 13, 0, 'a','s',
    'd','f','g','h','j','k','l',';','\'','`', 0,'\\','z','x','c','v',
    'b','n','m',',','.','/', 0,'*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

const char cse_sc_ascii_shift[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', 8, 9,
    'Q','W','E','R','T','Y','U','I','O','P','{','}', 13, 0, 'A','S',
    'D','F','G','H','J','K','L',':','"','~', 0,'|','Z','X','C','V',
    'B','N','M','<','>','?', 0,'*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

/* ---------------- scan state ---------------- */

static uint8_t  prev_state[CSEK_COUNT];
static uint8_t  mod_state;      /* currently emitted modifiers      */
static uint8_t  alpha_lock;
static void (*emit_fn)(void *ud, uint8_t sc);
static void *emit_ud;

static void default_emit(void *ud, uint8_t sc)
{
    if (ud) kbd_push_scancode((struct pc *)ud, sc);
}

static void emit_byte(uint8_t sc)
{
    if (emit_fn) emit_fn(emit_ud, sc);
    else         default_emit(emit_ud, sc);
}

void cse_keys_reset(void)
{
    memset(prev_state, 0, sizeof(prev_state));
    mod_state = 0;
    alpha_lock = 0;
}

void cse_keys_attach(struct pc *pc)
{
    /* Legacy hook: the attached machine becomes the emit sink. */
    emit_fn = NULL;
    emit_ud = pc;
}

void cse_keys_set_emit(void (*fn)(void *ud, uint8_t sc), void *ud)
{
    if (fn) {
        emit_fn = fn;
        emit_ud = ud;
    } else {
        /* Fall back to the pc attached with cse_keys_attach(): emit_ud is
         * deliberately left untouched so the attach() sink survives. */
        emit_fn = NULL;
    }
}

/* Extended keys (arrows, F1-F8 here) use the 0xE0 prefix form. */
static int is_extended(uint8_t sc)
{
    return sc >= SC_F1 && sc != SC_SPACE;
}

static void emit_make(uint8_t sc)
{
    if (is_extended(sc)) { emit_byte(SC_EXT_PREFIX); emit_byte(sc); }
    else                 { emit_byte(sc); }
}

static void emit_break(uint8_t sc)
{
    if (is_extended(sc)) { emit_byte(SC_EXT_PREFIX); emit_byte((uint8_t)(sc | 0x80)); }
    else                 { emit_byte((uint8_t)(sc | 0x80)); }
}

static void press_modifier(uint8_t flag, uint8_t sc)
{
    if (!(mod_state & flag)) { mod_state |= flag; emit_byte(sc); }
}

static void release_modifier(uint8_t flag, uint8_t sc)
{
    if (mod_state & flag) { mod_state &= (uint8_t)~flag; emit_byte((uint8_t)(sc | 0x80)); }
}

/* One full keypad scan; emits make/break pairs for every transition. */
void cse_keys_scan(void)
{
    uint8_t pressed[CSEK_COUNT];
    memset(pressed, 0, sizeof(pressed));

    /* Drive each row group and read the active-low column byte.  The
     * group->logical-key assignment follows the matrix order in
     * cse_keymap.h; the exact on-device wiring is a VERIFY item. */
    static const uint8_t groups[8] = { 0xFE,0xFD,0xFB,0xF7,0xEF,0xDF,0xBF,0x7F };
    uint8_t raw[8];
    cse_keypad_start();
    for (int g = 0; g < 8; g++)
        raw[g] = cse_keypad_read_group(groups[g]);
    cse_keypad_end();

    for (int g = 0; g < 8; g++) {
        uint8_t bits = (uint8_t)(~raw[g]);
        for (int b = 0; b < 8; b++)
            if (bits & (1u << b)) {
                int idx = g * 8 + b;
                if (idx < CSEK_COUNT) pressed[idx] = 1;
            }
    }

    /* Modifier edges.  2nd = Shift (and selects the 2nd function row);
     * Alpha toggles the alpha-lock (held Shift); 5th = Ctrl; 4th = Alt. */
    if (pressed[CSEK_2ND] && !prev_state[CSEK_2ND])
        press_modifier(CSEMOD_SHIFT, SC_MOD_SHIFT);
    if (!pressed[CSEK_2ND] && prev_state[CSEK_2ND])
        release_modifier(CSEMOD_SHIFT, SC_MOD_SHIFT);
    if (pressed[CSEK_5TH] && !prev_state[CSEK_5TH])
        press_modifier(CSEMOD_CTRL, SC_MOD_CTRL);
    if (!pressed[CSEK_5TH] && prev_state[CSEK_5TH])
        release_modifier(CSEMOD_CTRL, SC_MOD_CTRL);
    if (pressed[CSEK_4TH] && !prev_state[CSEK_4TH])
        press_modifier(CSEMOD_ALT, SC_MOD_ALT);
    if (!pressed[CSEK_4TH] && prev_state[CSEK_4TH])
        release_modifier(CSEMOD_ALT, SC_MOD_ALT);
    if (pressed[CSEK_ALPHA] && !prev_state[CSEK_ALPHA])
        alpha_lock = (uint8_t)!alpha_lock;

    int second_held = pressed[CSEK_2ND];

    for (int k = 0; k < CSEK_COUNT; k++) {
        if (k == CSEK_2ND || k == CSEK_5TH || k == CSEK_4TH ||
            k == CSEK_ALPHA || k == CSEK_ON)
            continue;                          /* handled above/separately */

        if (pressed[k] && !prev_state[k]) {
            uint8_t sc = (second_held && cse_keymap_2nd[k])
                       ? cse_keymap_2nd[k] : cse_keymap_base[k];
            if (!sc) continue;

            int want_shift = (mod_state & CSEMOD_SHIFT) || alpha_lock ||
                             (cse_keymap_mod[k] == CSEMOD_SHIFT);
            int want_ctrl = (cse_keymap_mod[k] == CSEMOD_CTRL);
            int want_alt  = (cse_keymap_mod[k] == CSEMOD_ALT);

            int transient_shift = 0;
            if (want_shift && !(mod_state & CSEMOD_SHIFT)) {
                press_modifier(CSEMOD_SHIFT, SC_MOD_SHIFT);
                transient_shift = 1;
            }
            if (want_ctrl) press_modifier(CSEMOD_CTRL, SC_MOD_CTRL);
            if (want_alt)  press_modifier(CSEMOD_ALT, SC_MOD_ALT);

            emit_make(sc);

            if (transient_shift) release_modifier(CSEMOD_SHIFT, SC_MOD_SHIFT);
            if (want_ctrl && !pressed[CSEK_5TH]) release_modifier(CSEMOD_CTRL, SC_MOD_CTRL);
            if (want_alt && !pressed[CSEK_4TH]) release_modifier(CSEMOD_ALT, SC_MOD_ALT);
        } else if (!pressed[k] && prev_state[k]) {
            uint8_t sc = cse_keymap_base[k];
            if (!sc) continue;
            emit_break(sc);
        }
    }

    memcpy(prev_state, pressed, sizeof(prev_state));
}

/* ON key: returns 1 while held (recovery entry). */
int cse_keys_on_held(void)
{
    cse_keypad_start();
    uint8_t raw = cse_keypad_read_group(0xFE);
    cse_keypad_end();
    return ((uint8_t)(~raw) & 0x01) ? 1 : 0;     /* VERIFY: ON matrix bit */
}

/* ---------------- tiny386-ini bridge ---------------- */

static void add_entry(cse_keymap_ini *out, const char *k, const char *pc, uint8_t sc)
{
    if (out->count >= CSE_KEYMAP_MAX) return;
    out->entries[out->count].key = k;
    out->entries[out->count].pc  = pc;
    out->entries[out->count].sc  = sc;
    out->count++;
}

void cse_keys_get_keymap(cse_keymap_ini *out)
{
    memset(out, 0, sizeof(*out));
    add_entry(out, "enter", "Return",    SC_ENTER);
    add_entry(out, "plus",  "equal",     SC_EQUALS);
    add_entry(out, "minus", "minus",     SC_MINUS);
    add_entry(out, "mul",   "kp_mul",    SC_KP_MUL);
    add_entry(out, "div",   "slash",     SC_SLASH);
    add_entry(out, "clear", "Escape",    SC_ESC);
    add_entry(out, "del",   "BackSpace", SC_BACKSPACE);
    add_entry(out, "mode",  "Tab",       SC_TAB);
    add_entry(out, "space", "space",     SC_SPACE);
    add_entry(out, "3", "3", SC_3);
    add_entry(out, "6", "6", SC_6);
    add_entry(out, "9", "9", SC_9);
    add_entry(out, "2", "2", SC_2);
    add_entry(out, "5", "5", SC_5);
    add_entry(out, "8", "8", SC_8);
    add_entry(out, "1", "1", SC_1);
    add_entry(out, "4", "4", SC_4);
    add_entry(out, "7", "7", SC_7);
    add_entry(out, "comma", "comma", SC_COMMA);
    add_entry(out, "rparen", "0", SC_0);
    add_entry(out, "lparen", "9", SC_9);
    add_entry(out, "window", "w", SC_W);
    add_entry(out, "zoom",   "z", SC_Z);
    add_entry(out, "yeq",    "y", SC_Y);
    add_entry(out, "graph",  "g", SC_G);
    add_entry(out, "trace",  "r", SC_R);
    add_entry(out, "stat",   "a", SC_A);
    add_entry(out, "xton",   "x", SC_X);
    add_entry(out, "ln",     "l", SC_L);
    add_entry(out, "log",    "k", SC_K);
    add_entry(out, "sin",    "s", SC_S);
    add_entry(out, "cos",    "c", SC_C);
    add_entry(out, "tan",    "t", SC_T);
    add_entry(out, "up",    "Up",    SC_EXT_UP);
    add_entry(out, "down",  "Down",  SC_EXT_DOWN);
    add_entry(out, "left",  "Left",  SC_EXT_LEFT);
    add_entry(out, "right", "Right", SC_EXT_RIGHT);
    add_entry(out, "2nd",   "Shift",   SC_LSHIFT);
    add_entry(out, "5th",   "Control", SC_LCTRL);
    add_entry(out, "4th",   "Alt",     SC_LALT);
    add_entry(out, "sto",   "semicolon", SC_SEMICOLON);
    add_entry(out, "f1", "F1", SC_F1);
    add_entry(out, "f2", "F2", SC_F2);
    add_entry(out, "f3", "F3", SC_F3);
    add_entry(out, "f4", "F4", SC_F4);
    add_entry(out, "f5", "F5", SC_F5);
    add_entry(out, "f6", "F6", SC_F6);
    add_entry(out, "f7", "F7", SC_F7);
    add_entry(out, "f8", "F8", SC_F8);
}
