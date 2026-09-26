/*
 * Munt386 -- x86 CPU core.
 *
 * Phase 1/2/3 deliver real-mode 16-bit execution with the register file laid
 * out for 32-bit growth (see include/munt386.h).  The core is deliberately
 * independent of any CSE hardware and of the host platform so it can be driven
 * by the host test harness or by the Z80 firmware.
 *
 * Interrupt strategy
 * ------------------
 * The BIOS owns a block of "stub" addresses in segment F000 at offset 0x1000.
 * Each vector n has a stub at F000:1000 + n*4.  The IVT is initialised to point
 * at these stubs.  When the CPU arrives at a stub it traps to the host-side
 * BIOS/DOS handler.  If guest software installs its own handler (as real DOS
 * does for INT 21h) the IVT points elsewhere and genuine x86 code runs instead.
 * This is the standard "emulator owns the BIOS ROM" technique and is faithful
 * to real trap/IRET semantics.
 */
#include "munt386.h"
#include <stdio.h>
#include <string.h>

/* Fault-message formatting.
 * Host builds use snprintf; the Z80 firmware build (SDCC) has no printf
 * family, so MUNT386_CSE gets a hand-rolled bounded formatter.  Both paths
 * produce exactly "<prefix><2 hex digits> at <4 hex>:<4 hex>". */
#ifdef MUNT386_CSE
static void fm_put(char *dst, size_t cap, size_t *n, char ch)
{
    if (*n + 1 < cap) dst[(*n)++] = ch;
}
static void fm_hex(char *dst, size_t cap, size_t *n, unsigned v, int digits)
{
    static const char fm_hexd[] = "0123456789ABCDEF";
    int i;
    for (i = digits - 1; i >= 0; i--)
        fm_put(dst, cap, n, fm_hexd[(v >> (i * 4)) & 0xF]);
}
static void fault_msg_fmt(char *dst, size_t cap, const char *prefix,
                          uint8_t op, uint16_t cs, uint16_t eip)
{
    size_t n = 0;
    if (!cap) return;
    while (*prefix) fm_put(dst, cap, &n, *prefix++);
    fm_hex(dst, cap, &n, op, 2);
    fm_put(dst, cap, &n, ' ');
    fm_put(dst, cap, &n, 'a');
    fm_put(dst, cap, &n, 't');
    fm_put(dst, cap, &n, ' ');
    fm_hex(dst, cap, &n, cs, 4);
    fm_put(dst, cap, &n, ':');
    fm_hex(dst, cap, &n, eip, 4);
    dst[n] = 0;
}
#else
#define fault_msg_fmt(buf, cap, prefix, opv, csv, eipv) \
    snprintf((buf), (cap), prefix "%02X at %04X:%04X", (opv), (csv), (eipv))
#endif

/* Convert a segment-register view to a linear address.
 * Real mode: (seg << 4) + off.  Protected mode: seg_cache.base + off with a
 * limit check that raises #GP/#SS when the offset exceeds the limit. */
static uint32_t lin_addr(pc_t *pc, int sreg, uint16_t off, int *exc, int *exc_code)
{
    cpu386_t *c = &pc->cpu;
    *exc = 0;
    if (!c->in_protected)
        return ((uint32_t)c->sreg[sreg] << 4) + off;
    seg_cache_t *sc = &c->sc[sreg];
    if (!sc->valid) {
        *exc = (sreg == SREG_SS) ? EXC_SS : EXC_GP;
        *exc_code = c->sreg[sreg];
        return 0;
    }
    if (off > sc->limit) {
        *exc = (sreg == SREG_SS) ? EXC_SS : EXC_GP;
        *exc_code = 0;
        return 0;
    }
    return sc->base + off;
}

static uint8_t seg_read8(pc_t *pc, int sreg, uint16_t off)
{
    int e, ec; uint32_t lin = lin_addr(pc, sreg, off, &e, &ec);
    if (e) { cpu_raise_exception(pc, (uint8_t)e, (uint32_t)ec); return 0; }
    return mem_lread8(pc, lin);
}
static uint16_t seg_read16(pc_t *pc, int sreg, uint16_t off)
{
    int e, ec; uint32_t lin = lin_addr(pc, sreg, off, &e, &ec);
    if (e) { cpu_raise_exception(pc, (uint8_t)e, (uint32_t)ec); return 0; }
    return mem_lread16(pc, lin);
}
static void seg_write8(pc_t *pc, int sreg, uint16_t off, uint8_t v)
{
    int e, ec; uint32_t lin = lin_addr(pc, sreg, off, &e, &ec);
    if (e) { cpu_raise_exception(pc, (uint8_t)e, (uint32_t)ec); return; }
    mem_lwrite8(pc, lin, v);
}
static void seg_write16(pc_t *pc, int sreg, uint16_t off, uint16_t v)
{
    int e, ec; uint32_t lin = lin_addr(pc, sreg, off, &e, &ec);
    if (e) { cpu_raise_exception(pc, (uint8_t)e, (uint32_t)ec); return; }
    mem_lwrite16(pc, lin, v);
}

#define BIOS_STUB_SEG 0xF000
#define BIOS_STUB_OFF 0x1000
#define BIOS_STUB_COUNT 256

/* Single-threaded execution context. */
static int g_seg_override = SEG_NONE;
static int g_rep = 0;   /* 0 none, 1 REP/REPE, 2 REPNE */

#define CX_SET_DEC(c) ((c)->r[REG_CX] = ((c)->r[REG_CX] - 1) & 0xFFFF)

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

int parity_even(uint8_t v)
{
    int n = 0;
    for (int i = 0; i < 8; i++) n += (v >> i) & 1;
    return (n & 1) == 0;
}

static inline uint8_t fetch8(pc_t *pc)
{
    cpu386_t *c = &pc->cpu;
    uint8_t v = seg_read8(pc, SREG_CS, (uint16_t)c->eip);
    c->eip = (c->eip + 1) & 0xFFFF;
    return v;
}

static inline uint16_t fetch16(pc_t *pc)
{
    uint16_t lo = fetch8(pc);
    uint16_t hi = fetch8(pc);
    return (uint16_t)(lo | (hi << 8));
}

void cpu_push16(pc_t *pc, uint16_t v)
{
    cpu386_t *c = &pc->cpu;
    c->r[REG_SP] = (c->r[REG_SP] - 2) & 0xFFFF;
    seg_write16(pc, SREG_SS, (uint16_t)c->r[REG_SP], v);
}

uint16_t cpu_pop16(pc_t *pc)
{
    cpu386_t *c = &pc->cpu;
    uint16_t v = seg_read16(pc, SREG_SS, (uint16_t)c->r[REG_SP]);
    c->r[REG_SP] = (c->r[REG_SP] + 2) & 0xFFFF;
    return v;
}

/* ------------------------------------------------------------------ */
/* Register access by ModR/M index                                     */
/* ------------------------------------------------------------------ */

uint16_t cpu_get_r16(cpu386_t *c, int idx) { return (uint16_t)c->r[idx & 7]; }
void cpu_set_r16(cpu386_t *c, int idx, uint16_t v) { c->r[idx & 7] = v; }

uint8_t cpu_get_r8(cpu386_t *c, int idx)
{
    idx &= 7;
    if (idx < 4) return (uint8_t)(c->r[idx] & 0xFF);
    return (uint8_t)((c->r[idx - 4] >> 8) & 0xFF);
}

void cpu_set_r8(cpu386_t *c, int idx, uint8_t v)
{
    idx &= 7;
    if (idx < 4) c->r[idx] = (c->r[idx] & 0xFF00u) | v;
    else c->r[idx - 4] = (c->r[idx - 4] & 0x00FFu) | ((uint32_t)v << 8);
}

/* ------------------------------------------------------------------ */
/* Flag computation                                                    */
/* ------------------------------------------------------------------ */

static uint8_t add8f(cpu386_t *c, uint8_t a, uint8_t b, int cin)
{
    uint32_t r = (uint32_t)a + (uint32_t)b + (uint32_t)cin;
    uint8_t res = (uint8_t)r;
    uint32_t f = c->eflags & ~(FLAG_CF | FLAG_AF | FLAG_OF | FLAG_ZF | FLAG_SF | FLAG_PF);
    if (r > 0xFF) f |= FLAG_CF;
    if ((a ^ b ^ res) & 0x10) f |= FLAG_AF;
    if ((a ^ res) & (b ^ res) & 0x80) f |= FLAG_OF;
    if (res == 0) f |= FLAG_ZF;
    if (res & 0x80) f |= FLAG_SF;
    if (parity_even(res)) f |= FLAG_PF;
    c->eflags = f | FLAG_FIXED;
    return res;
}

static uint16_t add16f(cpu386_t *c, uint16_t a, uint16_t b, int cin)
{
    uint32_t r = (uint32_t)a + (uint32_t)b + (uint32_t)cin;
    uint16_t res = (uint16_t)r;
    uint32_t f = c->eflags & ~(FLAG_CF | FLAG_AF | FLAG_OF | FLAG_ZF | FLAG_SF | FLAG_PF);
    if (r > 0xFFFF) f |= FLAG_CF;
    if ((a ^ b ^ res) & 0x10) f |= FLAG_AF;
    if ((a ^ res) & (b ^ res) & 0x8000) f |= FLAG_OF;
    if (res == 0) f |= FLAG_ZF;
    if (res & 0x8000) f |= FLAG_SF;
    if (parity_even((uint8_t)res)) f |= FLAG_PF;
    c->eflags = f | FLAG_FIXED;
    return res;
}

static uint8_t sub8f(cpu386_t *c, uint8_t a, uint8_t b, int bin)
{
    uint32_t r = (uint32_t)a - (uint32_t)b - (uint32_t)bin;
    uint8_t res = (uint8_t)r;
    uint32_t f = c->eflags & ~(FLAG_CF | FLAG_AF | FLAG_OF | FLAG_ZF | FLAG_SF | FLAG_PF);
    if (r & 0x100) f |= FLAG_CF;
    if ((a ^ b ^ res) & 0x10) f |= FLAG_AF;
    if ((a ^ b) & (a ^ res) & 0x80) f |= FLAG_OF;
    if (res == 0) f |= FLAG_ZF;
    if (res & 0x80) f |= FLAG_SF;
    if (parity_even(res)) f |= FLAG_PF;
    c->eflags = f | FLAG_FIXED;
    return res;
}

static uint16_t sub16f(cpu386_t *c, uint16_t a, uint16_t b, int bin)
{
    uint32_t r = (uint32_t)a - (uint32_t)b - (uint32_t)bin;
    uint16_t res = (uint16_t)r;
    uint32_t f = c->eflags & ~(FLAG_CF | FLAG_AF | FLAG_OF | FLAG_ZF | FLAG_SF | FLAG_PF);
    if (r & 0x10000) f |= FLAG_CF;
    if ((a ^ b ^ res) & 0x10) f |= FLAG_AF;
    if ((a ^ b) & (a ^ res) & 0x8000) f |= FLAG_OF;
    if (res == 0) f |= FLAG_ZF;
    if (res & 0x8000) f |= FLAG_SF;
    if (parity_even((uint8_t)res)) f |= FLAG_PF;
    c->eflags = f | FLAG_FIXED;
    return res;
}

void set_flags_add8 (cpu386_t *c, uint8_t a, uint8_t b, uint8_t r)  { (void)r; add8f(c, a, b, 0); }
void set_flags_add16(cpu386_t *c, uint16_t a, uint16_t b, uint16_t r){ (void)r; add16f(c, a, b, 0); }
void set_flags_sub8 (cpu386_t *c, uint8_t a, uint8_t b, uint8_t r)  { (void)r; sub8f(c, a, b, 0); }
void set_flags_sub16(cpu386_t *c, uint16_t a, uint16_t b, uint16_t r){ (void)r; sub16f(c, a, b, 0); }

void set_flags_logic8(cpu386_t *c, uint8_t r)
{
    uint32_t f = c->eflags & ~(FLAG_CF | FLAG_OF | FLAG_AF | FLAG_ZF | FLAG_SF | FLAG_PF);
    if (r == 0) f |= FLAG_ZF;
    if (r & 0x80) f |= FLAG_SF;
    if (parity_even(r)) f |= FLAG_PF;
    c->eflags = f | FLAG_FIXED;
}

void set_flags_logic16(cpu386_t *c, uint16_t r)
{
    uint32_t f = c->eflags & ~(FLAG_CF | FLAG_OF | FLAG_AF | FLAG_ZF | FLAG_SF | FLAG_PF);
    if (r == 0) f |= FLAG_ZF;
    if (r & 0x8000) f |= FLAG_SF;
    if (parity_even((uint8_t)r)) f |= FLAG_PF;
    c->eflags = f | FLAG_FIXED;
}

void set_flags_inc8(cpu386_t *c, uint8_t a, uint8_t r)
{
    (void)r;
    uint32_t cf = c->eflags & FLAG_CF;
    add8f(c, a, 1, 0);
    c->eflags = (c->eflags & ~FLAG_CF) | cf;
}

void set_flags_dec8(cpu386_t *c, uint8_t a, uint8_t r)
{
    (void)r;
    uint32_t cf = c->eflags & FLAG_CF;
    sub8f(c, a, 1, 0);
    c->eflags = (c->eflags & ~FLAG_CF) | cf;
}

void set_flags_inc16(cpu386_t *c, uint16_t a, uint16_t r)
{
    (void)r;
    uint32_t cf = c->eflags & FLAG_CF;
    add16f(c, a, 1, 0);
    c->eflags = (c->eflags & ~FLAG_CF) | cf;
}

void set_flags_dec16(cpu386_t *c, uint16_t a, uint16_t r)
{
    (void)r;
    uint32_t cf = c->eflags & FLAG_CF;
    sub16f(c, a, 1, 0);
    c->eflags = (c->eflags & ~FLAG_CF) | cf;
}

/* ------------------------------------------------------------------ */
/* ModR/M decoding                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    int is_mem;
    int mod, reg, rm;
    int sreg;       /* segment register index for memory operands */
    uint16_t off;   /* 16-bit effective offset */
} rm_t;

static void decode_rm(pc_t *pc, rm_t *m)
{
    cpu386_t *c = &pc->cpu;
    uint8_t b = fetch8(pc);
    m->mod = b >> 6;
    m->reg = (b >> 3) & 7;
    m->rm = b & 7;
    m->is_mem = (m->mod != 3);
    m->off = 0;
    m->sreg = SREG_DS;
    if (!m->is_mem) return;

    int def = SREG_DS;
    uint32_t base = 0;
    switch (m->rm) {
        case 0: base = (uint16_t)(BX(c) + SI(c)); def = SREG_DS; break;
        case 1: base = (uint16_t)(BX(c) + DI(c)); def = SREG_DS; break;
        case 2: base = (uint16_t)(BP(c) + SI(c)); def = SREG_SS; break;
        case 3: base = (uint16_t)(BP(c) + DI(c)); def = SREG_SS; break;
        case 4: base = SI(c); def = SREG_DS; break;
        case 5: base = DI(c); def = SREG_DS; break;
        case 6:
            if (m->mod == 0) { base = fetch16(pc); def = SREG_DS; }
            else             { base = BP(c);       def = SREG_SS; }
            break;
        case 7: base = BX(c); def = SREG_DS; break;
    }
    if (m->mod == 1) base += (int8_t)fetch8(pc);
    else if (m->mod == 2) base += (int16_t)fetch16(pc);

    m->off = (uint16_t)base;
    m->sreg = (g_seg_override != SEG_NONE) ? (g_seg_override - 1) : def;
}

static uint16_t rm_get16(pc_t *pc, rm_t *m)
{
    if (!m->is_mem) return cpu_get_r16(&pc->cpu, m->rm);
    return seg_read16(pc, m->sreg, m->off);
}

static void rm_set16(pc_t *pc, rm_t *m, uint16_t v)
{
    if (!m->is_mem) { cpu_set_r16(&pc->cpu, m->rm, v); return; }
    seg_write16(pc, m->sreg, m->off, v);
}

static uint8_t rm_get8(pc_t *pc, rm_t *m)
{
    if (!m->is_mem) return cpu_get_r8(&pc->cpu, m->rm);
    return seg_read8(pc, m->sreg, m->off);
}

static void rm_set8(pc_t *pc, rm_t *m, uint8_t v)
{
    if (!m->is_mem) { cpu_set_r8(&pc->cpu, m->rm, v); return; }
    seg_write8(pc, m->sreg, m->off, v);
}

/* ------------------------------------------------------------------ */
/* ALU                                                                 */
/* ------------------------------------------------------------------ */

enum { OP_ADD, OP_OR, OP_ADC, OP_SBB, OP_AND, OP_SUB, OP_XOR, OP_CMP };

static uint8_t alu8(cpu386_t *c, int op, uint8_t a, uint8_t b)
{
    switch (op) {
        case OP_ADD: return add8f(c, a, b, 0);
        case OP_ADC: return add8f(c, a, b, (c->eflags & FLAG_CF) ? 1 : 0);
        case OP_SUB: return sub8f(c, a, b, 0);
        case OP_SBB: return sub8f(c, a, b, (c->eflags & FLAG_CF) ? 1 : 0);
        case OP_CMP: sub8f(c, a, b, 0); return a;
        case OP_OR:  { uint8_t r = a | b; set_flags_logic8(c, r); return r; }
        case OP_AND: { uint8_t r = a & b; set_flags_logic8(c, r); return r; }
        case OP_XOR: { uint8_t r = a ^ b; set_flags_logic8(c, r); return r; }
    }
    return a;
}

static uint16_t alu16(cpu386_t *c, int op, uint16_t a, uint16_t b)
{
    switch (op) {
        case OP_ADD: return add16f(c, a, b, 0);
        case OP_ADC: return add16f(c, a, b, (c->eflags & FLAG_CF) ? 1 : 0);
        case OP_SUB: return sub16f(c, a, b, 0);
        case OP_SBB: return sub16f(c, a, b, (c->eflags & FLAG_CF) ? 1 : 0);
        case OP_CMP: sub16f(c, a, b, 0); return a;
        case OP_OR:  { uint16_t r = a | b; set_flags_logic16(c, r); return r; }
        case OP_AND: { uint16_t r = a & b; set_flags_logic16(c, r); return r; }
        case OP_XOR: { uint16_t r = a ^ b; set_flags_logic16(c, r); return r; }
    }
    return a;
}

/* ------------------------------------------------------------------ */
/* Shifts / rotates                                                    */
/* ------------------------------------------------------------------ */

static uint32_t shift_generic(cpu386_t *c, int op, uint32_t v, int count, int width)
{
    uint32_t mask    = (width == 8) ? 0xFFu : 0xFFFFu;
    uint32_t signbit = (width == 8) ? 0x80u : 0x8000u;
    int n = count & 0x1F;
    v &= mask;
    if (n == 0) return v;

    uint32_t cf = c->eflags & FLAG_CF;
    for (int i = 0; i < n; i++) {
        switch (op) {
            case 0: /* ROL */
                cf = (v >> (width - 1)) & 1u;
                v = ((v << 1) | cf) & mask;
                break;
            case 1: /* ROR */
                cf = v & 1u;
                v = ((v >> 1) | (cf << (width - 1))) & mask;
                break;
            case 2: /* RCL */
                { uint32_t nb = (v >> (width - 1)) & 1u;
                  v = ((v << 1) | cf) & mask; cf = nb; }
                break;
            case 3: /* RCR */
                { uint32_t nb = v & 1u;
                  v = ((v >> 1) | (cf << (width - 1))) & mask; cf = nb; }
                break;
            case 4: case 6: /* SHL / SAL */
                cf = (v >> (width - 1)) & 1u;
                v = (v << 1) & mask;
                break;
            case 5: /* SHR */
                cf = v & 1u;
                v = (v >> 1) & mask;
                break;
            case 7: /* SAR */
                cf = v & 1u;
                v = ((v >> 1) | (v & signbit)) & mask;
                break;
        }
    }

    uint32_t f = c->eflags;
    if (cf) f |= FLAG_CF; else f &= ~FLAG_CF;

    if (op <= 3) {
        uint32_t msb = (v >> (width - 1)) & 1u;
        uint32_t ofv = 0;
        if (n == 1) {
            if (op == 0)      ofv = cf ^ msb;
            else if (op == 1) ofv = msb ^ ((v >> (width - 2)) & 1u);
            else              ofv = cf ^ msb;
        }
        if (ofv) f |= FLAG_OF; else f &= ~FLAG_OF;
    } else {
        if (n == 1) {
            uint32_t msb = (v >> (width - 1)) & 1u;
            if (cf ^ msb) f |= FLAG_OF; else f &= ~FLAG_OF;
        }
        if ((v & mask) == 0) f |= FLAG_ZF; else f &= ~FLAG_ZF;
        if (v & signbit) f |= FLAG_SF; else f &= ~FLAG_SF;
        if (parity_even((uint8_t)(v & 0xFF))) f |= FLAG_PF; else f &= ~FLAG_PF;
    }
    c->eflags = f | FLAG_FIXED;
    return v & mask;
}

/* ------------------------------------------------------------------ */
/* Interrupts                                                          */
/* ------------------------------------------------------------------ */

void cpu_interrupt(pc_t *pc, uint8_t vector)
{
    /* Real-mode fast path (kept for the BIOS/DOS stub machinery). */
    cpu386_t *c = &pc->cpu;
    if (!c->in_protected) {
        uint16_t ip = seg_read16(pc, SREG_DS, (uint16_t)(vector * 4));
        uint16_t cs = seg_read16(pc, SREG_DS, (uint16_t)(vector * 4 + 2));
        cpu_push16(pc, (uint16_t)c->eflags);
        cpu_push16(pc, c->sreg[SREG_CS]);
        cpu_push16(pc, (uint16_t)c->eip);
        c->eflags &= ~(FLAG_IF | FLAG_TF);
        c->sreg[SREG_CS] = cs;
        c->eip = ip;
        return;
    }
    cpu_deliver_interrupt(pc, vector, 0, 0);
}

static void div_error(pc_t *pc)
{
    cpu_raise_exception(pc, EXC_DE, 0);
}

/* ------------------------------------------------------------------ */
/* Condition codes                                                     */
/* ------------------------------------------------------------------ */

static int cond(cpu386_t *c, int cc)
{
    uint32_t f = c->eflags;
    int cf = (f & FLAG_CF) != 0, zf = (f & FLAG_ZF) != 0;
    int sf = (f & FLAG_SF) != 0, of = (f & FLAG_OF) != 0, pf = (f & FLAG_PF) != 0;
    switch (cc & 0xF) {
        case 0x0: return of;                  /* O   */
        case 0x1: return !of;                 /* NO  */
        case 0x2: return cf;                  /* B   */
        case 0x3: return !cf;                 /* AE  */
        case 0x4: return zf;                  /* E   */
        case 0x5: return !zf;                 /* NE  */
        case 0x6: return cf || zf;            /* BE  */
        case 0x7: return !cf && !zf;          /* A   */
        case 0x8: return sf;                  /* S   */
        case 0x9: return !sf;                 /* NS  */
        case 0xA: return pf;                  /* P   */
        case 0xB: return !pf;                 /* NP  */
        case 0xC: return sf != of;            /* L   */
        case 0xD: return sf == of;            /* GE  */
        case 0xE: return zf || (sf != of);    /* LE  */
        case 0xF: return !zf && (sf == of);   /* G   */
    }
    return 0;
}

static void jmp_rel8(pc_t *pc)
{
    int8_t d = (int8_t)fetch8(pc);
    pc->cpu.eip = (pc->cpu.eip + d) & 0xFFFF;
}

static void jmp_rel16(pc_t *pc)
{
    int16_t d = (int16_t)fetch16(pc);
    pc->cpu.eip = (pc->cpu.eip + d) & 0xFFFF;
}

/* ------------------------------------------------------------------ */
/* String operations                                                   */
/* ------------------------------------------------------------------ */

static void do_string(pc_t *pc, uint8_t op)
{
    cpu386_t *c = &pc->cpu;
    int w = op & 1;                 /* 0xA4/0xA5 etc: low bit selects word */
    int bytes = w ? 2 : 1;
    int df = (c->eflags & FLAG_DF) ? -1 : 1;
    int rep = g_rep;

    do {
        switch (op) {
            case 0xA4: case 0xA5: /* MOVS */
                if (w) { uint16_t v = seg_read16(pc, SREG_DS, SI(c)); seg_write16(pc, SREG_ES, DI(c), v); }
                else   { uint8_t b = seg_read8(pc, SREG_DS, SI(c)); seg_write8(pc, SREG_ES, DI(c), b); }
                c->r[REG_SI] = (uint16_t)(c->r[REG_SI] + df * bytes);
                c->r[REG_DI] = (uint16_t)(c->r[REG_DI] + df * bytes);
                break;
            case 0xA6: case 0xA7: /* CMPS */
                if (w) sub16f(c, seg_read16(pc, SREG_DS, SI(c)), seg_read16(pc, SREG_ES, DI(c)), 0);
                else   sub8f(c, seg_read8(pc, SREG_DS, SI(c)), seg_read8(pc, SREG_ES, DI(c)), 0);
                c->r[REG_SI] = (uint16_t)(c->r[REG_SI] + df * bytes);
                c->r[REG_DI] = (uint16_t)(c->r[REG_DI] + df * bytes);
                break;
            case 0xAA: case 0xAB: /* STOS */
                if (w) seg_write16(pc, SREG_ES, DI(c), AX(c));
                else   seg_write8(pc, SREG_ES, DI(c), AL(c));
                c->r[REG_DI] = (uint16_t)(c->r[REG_DI] + df * bytes);
                break;
            case 0xAC: case 0xAD: /* LODS */
                if (w) SET_AX(c, seg_read16(pc, SREG_DS, SI(c)));
                else   SET_AL(c, seg_read8(pc, SREG_DS, SI(c)));
                c->r[REG_SI] = (uint16_t)(c->r[REG_SI] + df * bytes);
                break;
            case 0xAE: case 0xAF: /* SCAS */
                if (w) sub16f(c, AX(c), seg_read16(pc, SREG_ES, DI(c)), 0);
                else   sub8f(c, AL(c), seg_read8(pc, SREG_ES, DI(c)), 0);
                c->r[REG_DI] = (uint16_t)(c->r[REG_DI] + df * bytes);
                break;
        }
        if (rep == 0) break;
        CX_SET_DEC(c);
        if (CX(c) == 0) break;
        if (op == 0xA6 || op == 0xA7 || op == 0xAE || op == 0xAF) {
            /* REPE/REPNE stop when ZF no longer matches the condition. */
            int zf = (c->eflags & FLAG_ZF) != 0;
            if (rep == 1 && !zf) break;
            if (rep == 2 && zf) break;
        }
    } while (1);
}

/* ------------------------------------------------------------------ */
/* Two-byte opcodes (0x0F prefix)                                      */
/*                                                                       */
/* Implements the system instructions required to enter and operate in   */
/* protected mode plus the common 386 register forms.  Unimplemented     */
/* opcodes raise #UD rather than being silently ignored.                 */
/* ------------------------------------------------------------------ */

static int cpu_step_0f(pc_t *pc)
{
    cpu386_t *c = &pc->cpu;
    uint8_t op2 = fetch8(pc);
    c->insn_count++;

    rm_t m;
    switch (op2) {

    /* MOV r32, CRn / MOV CRn, r32 (encoded as r16 today) */
    case 0x20: {
        uint8_t b_20 = fetch8(pc);
        int cr_20 = (b_20 >> 3) & 7;
        int rr_20 = b_20 & 7;
        uint32_t v_20 = cr_20 == 0 ? c->cr0 : (cr_20 == 2 ? c->cr2 : (cr_20 == 3 ? c->cr3 : 0));
        cpu_set_r16(c, rr_20, (uint16_t)v_20);
        break;
    }
    case 0x22: {
        uint8_t b_22 = fetch8(pc);
        int cr_22 = (b_22 >> 3) & 7;
        uint32_t v_22 = cpu_get_r16(c, b_22 & 7);
        if (cr_22 == 0) {
            uint32_t old_22 = c->cr0;
            c->cr0 = v_22 | CR0_ET;
            if ((old_22 ^ c->cr0) & CR0_PE) cpu_update_mode(pc);
        } else if (cr_22 == 2) c->cr2 = v_22;
        else if (cr_22 == 3) c->cr3 = v_22 & 0xFFFFF000u;
        break;
    }

    /* Group 7: 0F 01 /0 LGDTm, /1 LIDTm, /2 LGDTm16? ... standard forms */
    case 0x01: {
        decode_rm(pc, &m);
        switch (m.reg) {
            case 0: { /* LGDT m16&16 */
                uint32_t lin_g0 = c->sc[m.sreg].base + m.off;
                uint16_t lim_g0 = mem_lread16(pc, lin_g0);
                uint32_t base_g0 = mem_lread16(pc, lin_g0 + 2) |
                                ((uint32_t)mem_lread8(pc, lin_g0 + 4) << 16);
                c->gdtr_limit = lim_g0; c->gdtr_base = base_g0;
                break;
            }
            case 1: { /* LIDT m16&16 */
                uint32_t lin_g1 = c->sc[m.sreg].base + m.off;
                uint16_t lim_g1 = mem_lread16(pc, lin_g1);
                uint32_t base_g1 = mem_lread16(pc, lin_g1 + 2) |
                                ((uint32_t)mem_lread8(pc, lin_g1 + 4) << 16);
                c->idtr_limit = lim_g1; c->idtr_base = base_g1;
                break;
            }
            case 2: { /* LGDT with 32-bit base form accepted too */
                uint32_t lin_g2 = c->sc[m.sreg].base + m.off;
                uint16_t lim_g2 = mem_lread16(pc, lin_g2);
                uint32_t base_g2 = mem_lread32(pc, lin_g2 + 2);
                c->gdtr_limit = lim_g2; c->gdtr_base = base_g2;
                break;
            }
            case 3: { /* LIDT 32-bit base form */
                uint32_t lin_g3 = c->sc[m.sreg].base + m.off;
                uint16_t lim_g3 = mem_lread16(pc, lin_g3);
                uint32_t base_g3 = mem_lread32(pc, lin_g3 + 2);
                c->idtr_limit = lim_g3; c->idtr_base = base_g3;
                break;
            }
            case 6: { /* LMSW: load MSW (low 16 bits of CR0) */
                uint16_t v_g6 = rm_get16(pc, &m);
                uint32_t old_g6 = c->cr0;
                c->cr0 = (c->cr0 & 0xFFFF0000u) | v_g6 | CR0_ET;
                if ((old_g6 ^ c->cr0) & CR0_PE) cpu_update_mode(pc);
                break;
            }
            default:
                goto ud;
        }
        break;
    }

    /* IMUL r16, r/m16 */
    case 0xAF: {
        decode_rm(pc, &m);
        int32_t a_af = (int16_t)cpu_get_r16(c, m.reg);
        int32_t b_af = (int16_t)rm_get16(pc, &m);
        int32_t p_af = a_af * b_af;
        SET_AX(c, 0);
        cpu_set_r16(c, m.reg, (uint16_t)p_af);
        if (p_af != (int32_t)(int16_t)p_af) c->eflags |= (FLAG_CF | FLAG_OF);
        else c->eflags &= ~(FLAG_CF | FLAG_OF);
        break;
    }

    /* MOVZX r16, r/m8 */
    case 0xB6: {
        decode_rm(pc, &m);
        cpu_set_r16(c, m.reg, rm_get8(pc, &m));
        break;
    }
    /* MOVZX r16, r/m16 (same width today; documented) */
    case 0xB7: {
        decode_rm(pc, &m);
        cpu_set_r16(c, m.reg, rm_get16(pc, &m));
        break;
    }
    /* MOVSX r16, r/m8 */
    case 0xBE: {
        decode_rm(pc, &m);
        cpu_set_r16(c, m.reg, (uint16_t)(int16_t)(int8_t)rm_get8(pc, &m));
        break;
    }
    case 0xBF: {
        decode_rm(pc, &m);
        cpu_set_r16(c, m.reg, rm_get16(pc, &m));
        break;
    }

    /* PUSH/POP FS, GS */
    case 0xA0: cpu_push16(pc, c->sreg[SREG_FS]); break;
    case 0xA1: { uint16_t sel_0a1 = cpu_pop16(pc); int e_0a1 = seg_load(pc, SREG_FS, sel_0a1);
                 if (e_0a1) cpu_raise_exception(pc, (uint8_t)e_0a1, sel_0a1); break; }
    case 0xA8: cpu_push16(pc, c->sreg[SREG_GS]); break;
    case 0xA9: { uint16_t sel_0a9 = cpu_pop16(pc); int e_0a9 = seg_load(pc, SREG_GS, sel_0a9);
                 if (e_0a9) cpu_raise_exception(pc, (uint8_t)e_0a9, sel_0a9); break; }

    ud:
    default:
        c->fault = 1;
        c->fault_eip = c->eip - 2;
        fault_msg_fmt(c->fault_msg, sizeof(c->fault_msg),
                      "unimplemented opcode 0F ",
                      op2, c->sreg[SREG_CS], (uint16_t)(c->eip - 2));
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Reset                                                               */
/* ------------------------------------------------------------------ */

void cpu_reset(cpu386_t *cpu)
{
    memset(cpu, 0, sizeof(*cpu));
    cpu->sreg[SREG_CS] = 0xFFFF;
    cpu->eip = 0x0000;
    cpu->eflags = FLAG_FIXED;
    /* Real-mode segment caches. */
    for (int i = 0; i < 6; i++) {
        cpu->sc[i].base = (uint32_t)cpu->sreg[i] << 4;
        cpu->sc[i].limit = 0xFFFF;
        cpu->sc[i].ar = 0x92;
        cpu->sc[i].flags = 0;
        cpu->sc[i].valid = 1;
    }
}

/* ------------------------------------------------------------------ */
/* Main step                                                           */
/* ------------------------------------------------------------------ */

int cpu_step(pc_t *pc)
{
    cpu386_t *c = &pc->cpu;

    /* BIOS/DOS stub trap. */
    if (c->sreg[SREG_CS] == BIOS_STUB_SEG &&
        c->eip >= BIOS_STUB_OFF &&
        c->eip < (uint32_t)(BIOS_STUB_OFF + BIOS_STUB_COUNT * 4)) {
        int vec = (int)((c->eip - BIOS_STUB_OFF) / 4);
        c->insn_count++;
        bios_handle_int(pc, (uint8_t)vec);
        dos_handle_int(pc, (uint8_t)vec);
        uint16_t ip = cpu_pop16(pc);
        uint16_t cs = cpu_pop16(pc);
        uint16_t fl = cpu_pop16(pc);
        c->eip = ip;
        c->sreg[SREG_CS] = cs;
        /* Handlers return their result flags; only IF/TF come from the frame. */
        c->eflags = (c->eflags & ~(FLAG_IF | FLAG_TF)) | (fl & (FLAG_IF | FLAG_TF));
        c->eflags |= FLAG_FIXED;
        return 0;
    }

    g_seg_override = SEG_NONE;
    g_rep = 0;

    uint8_t op;
    /* Prefix loop */
    for (;;) {
        op = fetch8(pc);
        if (op == 0x26) { g_seg_override = SEG_ES; continue; }
        if (op == 0x2E) { g_seg_override = SEG_CS; continue; }
        if (op == 0x36) { g_seg_override = SEG_SS; continue; }
        if (op == 0x3E) { g_seg_override = SEG_DS; continue; }
        if (op == 0x64) { g_seg_override = SEG_FS; continue; }
        if (op == 0x65) { g_seg_override = SEG_GS; continue; }
        if (op == 0xF0) { continue; }              /* LOCK - no-op */
        if (op == 0xF2) { g_rep = 2; continue; }   /* REPNE */
        if (op == 0xF3) { g_rep = 1; continue; }   /* REP/REPE */
        break;
    }

    c->last_opcode = op;
    c->insn_count++;

    rm_t m;
    switch (op) {

    /* ---- ALU group 0x00-0x3F ---- */
    case 0x00: case 0x01: case 0x02: case 0x03:
    case 0x08: case 0x09: case 0x0A: case 0x0B:
    case 0x10: case 0x11: case 0x12: case 0x13:
    case 0x18: case 0x19: case 0x1A: case 0x1B:
    case 0x20: case 0x21: case 0x22: case 0x23:
    case 0x28: case 0x29: case 0x2A: case 0x2B:
    case 0x30: case 0x31: case 0x32: case 0x33:
    case 0x38: case 0x39: case 0x3A: case 0x3B: {
        int opk = (op >> 3) & 7;
        int form = op & 7;
        decode_rm(pc, &m);
        if (form <= 1) {           /* r/m <- r */
            if (form == 0) { uint8_t a = rm_get8(pc, &m), b = cpu_get_r8(c, m.reg); rm_set8(pc, &m, alu8(c, opk, a, b)); }
            else           { uint16_t a = rm_get16(pc, &m), b = cpu_get_r16(c, m.reg); rm_set16(pc, &m, alu16(c, opk, a, b)); }
        } else if (form <= 3) {    /* r <- r/m */
            if (form == 2) { uint8_t a = cpu_get_r8(c, m.reg), b = rm_get8(pc, &m); cpu_set_r8(c, m.reg, alu8(c, opk, a, b)); }
            else           { uint16_t a = cpu_get_r16(c, m.reg), b = rm_get16(pc, &m); cpu_set_r16(c, m.reg, alu16(c, opk, a, b)); }
        }
        break;
    }
    case 0x04: { uint8_t a = AL(c), b = fetch8(pc); SET_AL(c, alu8(c, OP_ADD, a, b)); break; }
    case 0x05: { uint16_t a = AX(c), b = fetch16(pc); SET_AX(c, alu16(c, OP_ADD, a, b)); break; }
    case 0x0C: { uint8_t a = AL(c), b = fetch8(pc); SET_AL(c, alu8(c, OP_OR, a, b)); break; }
    case 0x0D: { uint16_t a = AX(c), b = fetch16(pc); SET_AX(c, alu16(c, OP_OR, a, b)); break; }
    case 0x14: { uint8_t a = AL(c), b = fetch8(pc); SET_AL(c, alu8(c, OP_ADC, a, b)); break; }
    case 0x15: { uint16_t a = AX(c), b = fetch16(pc); SET_AX(c, alu16(c, OP_ADC, a, b)); break; }
    case 0x1C: { uint8_t a = AL(c), b = fetch8(pc); SET_AL(c, alu8(c, OP_SBB, a, b)); break; }
    case 0x1D: { uint16_t a = AX(c), b = fetch16(pc); SET_AX(c, alu16(c, OP_SBB, a, b)); break; }
    case 0x24: { uint8_t a = AL(c), b = fetch8(pc); SET_AL(c, alu8(c, OP_AND, a, b)); break; }
    case 0x25: { uint16_t a = AX(c), b = fetch16(pc); SET_AX(c, alu16(c, OP_AND, a, b)); break; }
    case 0x2C: { uint8_t a = AL(c), b = fetch8(pc); SET_AL(c, alu8(c, OP_SUB, a, b)); break; }
    case 0x2D: { uint16_t a = AX(c), b = fetch16(pc); SET_AX(c, alu16(c, OP_SUB, a, b)); break; }
    case 0x34: { uint8_t a = AL(c), b = fetch8(pc); SET_AL(c, alu8(c, OP_XOR, a, b)); break; }
    case 0x35: { uint16_t a = AX(c), b = fetch16(pc); SET_AX(c, alu16(c, OP_XOR, a, b)); break; }
    case 0x3C: { uint8_t a = AL(c), b = fetch8(pc); alu8(c, OP_CMP, a, b); break; }
    case 0x3D: { uint16_t a = AX(c), b = fetch16(pc); alu16(c, OP_CMP, a, b); break; }

    /* ---- PUSH/POP segment ---- */
    case 0x06: cpu_push16(pc, c->sreg[SREG_ES]); break;
    case 0x07: { uint16_t sel = cpu_pop16(pc); int e = seg_load(pc, SREG_ES, sel);
                 if (e) cpu_raise_exception(pc, (uint8_t)e, sel); break; }
    case 0x0E: cpu_push16(pc, c->sreg[SREG_CS]); break;
    case 0x16: cpu_push16(pc, c->sreg[SREG_SS]); break;
    case 0x17: { uint16_t sel = cpu_pop16(pc); int e = seg_load(pc, SREG_SS, sel);
                 if (e) cpu_raise_exception(pc, (uint8_t)e, sel); break; }
    case 0x1E: cpu_push16(pc, c->sreg[SREG_DS]); break;
    case 0x1F: { uint16_t sel = cpu_pop16(pc); int e = seg_load(pc, SREG_DS, sel);
                 if (e) cpu_raise_exception(pc, (uint8_t)e, sel); break; }

    /* ---- INC/DEC r16 ---- */
    case 0x40: case 0x41: case 0x42: case 0x43:
    case 0x44: case 0x45: case 0x46: case 0x47: {
        int i = op & 7; uint16_t a = cpu_get_r16(c, i); uint16_t r = (uint16_t)(a + 1);
        set_flags_inc16(c, a, r); cpu_set_r16(c, i, r); break;
    }
    case 0x48: case 0x49: case 0x4A: case 0x4B:
    case 0x4C: case 0x4D: case 0x4E: case 0x4F: {
        int i = op & 7; uint16_t a = cpu_get_r16(c, i); uint16_t r = (uint16_t)(a - 1);
        set_flags_dec16(c, a, r); cpu_set_r16(c, i, r); break;
    }

    /* ---- PUSH/POP r16 ---- */
    case 0x50: case 0x51: case 0x52: case 0x53:
    case 0x54: case 0x55: case 0x56: case 0x57:
        cpu_push16(pc, cpu_get_r16(c, op & 7)); break;
    case 0x58: case 0x59: case 0x5A: case 0x5B:
    case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        cpu_set_r16(c, op & 7, cpu_pop16(pc)); break;

    /* ---- 80186 PUSHA/POPA (aliases on 8086) ---- */
    case 0x60: {
        uint16_t sp0 = SP(c);
        cpu_push16(pc, AX(c)); cpu_push16(pc, CX(c)); cpu_push16(pc, DX(c)); cpu_push16(pc, BX(c));
        cpu_push16(pc, sp0);   cpu_push16(pc, BP(c)); cpu_push16(pc, SI(c)); cpu_push16(pc, DI(c));
        break;
    }
    case 0x61: {
        SET_DI(c, cpu_pop16(pc)); SET_SI(c, cpu_pop16(pc)); SET_BP(c, cpu_pop16(pc));
        (void)cpu_pop16(pc);      /* discarded SP */
        SET_BX(c, cpu_pop16(pc)); SET_DX(c, cpu_pop16(pc)); SET_CX(c, cpu_pop16(pc)); SET_AX(c, cpu_pop16(pc));
        break;
    }

    /* ---- Jcc rel8 ---- */
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
        int8_t d = (int8_t)fetch8(pc);
        if (cond(c, op & 0xF)) c->eip = (c->eip + d) & 0xFFFF;
        break;
    }

    /* ---- Group 1: ALU r/m, imm ---- */
    case 0x80: case 0x81: case 0x82: case 0x83: {
        int imm16 = (op == 0x81);
        decode_rm(pc, &m);
        int opk = m.reg;
        if (imm16) {
            uint16_t b = fetch16(pc);
            uint16_t a = rm_get16(pc, &m);
            uint16_t r = alu16(c, opk, a, b);
            if (opk != OP_CMP) rm_set16(pc, &m, r);
        } else {
            uint8_t b = fetch8(pc);
            if (op == 0x83) b = (uint8_t)(int8_t)b; /* sign-extended imm8 to 16-bit */
            if (op == 0x83) {
                uint16_t a = rm_get16(pc, &m);
                uint16_t b16 = (uint16_t)(int16_t)(int8_t)b;
                uint16_t r = alu16(c, opk, a, b16);
                if (opk != OP_CMP) rm_set16(pc, &m, r);
            } else {
                uint8_t a = rm_get8(pc, &m);
                uint8_t r = alu8(c, opk, a, b);
                if (opk != OP_CMP) rm_set8(pc, &m, r);
            }
        }
        break;
    }

    /* ---- TEST/XCHG ---- */
    case 0x84: { decode_rm(pc, &m); uint8_t r = rm_get8(pc, &m) & cpu_get_r8(c, m.reg); set_flags_logic8(c, r); break; }
    case 0x85: { decode_rm(pc, &m); uint16_t r = rm_get16(pc, &m) & cpu_get_r16(c, m.reg); set_flags_logic16(c, r); break; }
    case 0x86: { decode_rm(pc, &m); uint8_t a = rm_get8(pc, &m), b = cpu_get_r8(c, m.reg); rm_set8(pc, &m, b); cpu_set_r8(c, m.reg, a); break; }
    case 0x87: { decode_rm(pc, &m); uint16_t a = rm_get16(pc, &m), b = cpu_get_r16(c, m.reg); rm_set16(pc, &m, b); cpu_set_r16(c, m.reg, a); break; }

    /* ---- MOV ---- */
    case 0x88: { decode_rm(pc, &m); rm_set8(pc, &m, cpu_get_r8(c, m.reg)); break; }
    case 0x89: { decode_rm(pc, &m); rm_set16(pc, &m, cpu_get_r16(c, m.reg)); break; }
    case 0x8A: { decode_rm(pc, &m); cpu_set_r8(c, m.reg, rm_get8(pc, &m)); break; }
    case 0x8B: { decode_rm(pc, &m); cpu_set_r16(c, m.reg, rm_get16(pc, &m)); break; }
    case 0x8C: { decode_rm(pc, &m); rm_set16(pc, &m, c->sreg[m.reg & 5]); break; }
    case 0x8E: {
        decode_rm(pc, &m);
        uint16_t sel = rm_get16(pc, &m);
        int sreg = m.reg & 5;
        int e = seg_load(pc, sreg, sel);
        if (e) { cpu_raise_exception(pc, (uint8_t)e, sel); }
        break;
    }
    case 0x8D: { decode_rm(pc, &m); cpu_set_r16(c, m.reg, m.off); break; }  /* LEA */
    case 0x8F: { decode_rm(pc, &m); rm_set16(pc, &m, cpu_pop16(pc)); break; }

    /* ---- XCHG AX, r16 / NOP ---- */
    case 0x90: break;
    case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: {
        int i = op & 7; uint16_t t = AX(c); SET_AX(c, cpu_get_r16(c, i)); cpu_set_r16(c, i, t); break;
    }
    case 0x98: SET_AX(c, (uint16_t)(int16_t)(int8_t)AL(c)); break;          /* CBW */
    case 0x99: SET_DX(c, (AX(c) & 0x8000) ? 0xFFFF : 0x0000); break;          /* CWD */
    case 0x9A: { uint16_t off = fetch16(pc); uint16_t seg = fetch16(pc);      /* CALL far */
        cpu_push16(pc, c->sreg[SREG_CS]); cpu_push16(pc, (uint16_t)c->eip);
        c->sreg[SREG_CS] = seg; c->eip = off; break; }
    case 0x9B: break;                                                          /* WAIT */
    case 0x9C: cpu_push16(pc, (uint16_t)c->eflags); break;                     /* PUSHF */
    case 0x9D: c->eflags = (cpu_pop16(pc) & ~0x0000) | FLAG_FIXED; break;      /* POPF */
    case 0x9E: { uint8_t ah = AH(c);                                               /* SAHF */
        uint32_t f = c->eflags & ~(FLAG_CF|FLAG_PF|FLAG_AF|FLAG_ZF|FLAG_SF);
        if (ah & 0x01) f |= FLAG_CF;
        if (ah & 0x04) f |= FLAG_PF;
        if (ah & 0x10) f |= FLAG_AF;
        if (ah & 0x40) f |= FLAG_ZF;
        if (ah & 0x80) f |= FLAG_SF;
        c->eflags = f | FLAG_FIXED; break; }
    case 0x9F:
        SET_AH(c, (uint8_t)(((c->eflags & FLAG_SF) ? 0x80 : 0) | ((c->eflags & FLAG_ZF) ? 0x40 : 0) |
                            ((c->eflags & FLAG_AF) ? 0x10 : 0) | ((c->eflags & FLAG_PF) ? 0x04 : 0) |
                            ((c->eflags & FLAG_CF) ? 0x01 : 0) | 0x02)); break;   /* LAHF */

    /* ---- moffs MOV ---- */
    case 0xA0: SET_AL(c, seg_read8(pc, SREG_DS, fetch16(pc))); break;
    case 0xA1: SET_AX(c, seg_read16(pc, SREG_DS, fetch16(pc))); break;
    case 0xA2: seg_write8(pc, SREG_DS, fetch16(pc), AL(c)); break;
    case 0xA3: seg_write16(pc, SREG_DS, fetch16(pc), AX(c)); break;

    /* ---- String ops ---- */
    case 0xA4: case 0xA5: case 0xA6: case 0xA7:
    case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF:
        do_string(pc, op); break;

    case 0xA8: { uint8_t r = AL(c) & fetch8(pc); set_flags_logic8(c, r); break; }
    case 0xA9: { uint16_t r = AX(c) & fetch16(pc); set_flags_logic16(c, r); break; }

    /* ---- MOV r, imm ---- */
    case 0xB0: case 0xB1: case 0xB2: case 0xB3:
    case 0xB4: case 0xB5: case 0xB6: case 0xB7:
        cpu_set_r8(c, op & 7, fetch8(pc)); break;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB:
    case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        cpu_set_r16(c, op & 7, fetch16(pc)); break;

    /* ---- Shifts by 1 (D0/D1) and by CL (D2/D3) ---- */
    case 0xD0: case 0xD1: case 0xD2: case 0xD3: {
        int w = (op == 0xD1 || op == 0xD3);
        int cnt = (op == 0xD2 || op == 0xD3) ? CL(c) : 1;
        decode_rm(pc, &m);
        if (w) rm_set16(pc, &m, (uint16_t)shift_generic(c, m.reg, rm_get16(pc, &m), cnt, 16));
        else   rm_set8(pc, &m, (uint8_t)shift_generic(c, m.reg, rm_get8(pc, &m), cnt, 8));
        break;
    }
    /* ---- Shifts by imm8 (80186 form) ---- */
    case 0xC0: case 0xC1: {
        int w = (op == 0xC1);
        decode_rm(pc, &m);
        int cnt = fetch8(pc);
        if (w) rm_set16(pc, &m, (uint16_t)shift_generic(c, m.reg, rm_get16(pc, &m), cnt, 16));
        else   rm_set8(pc, &m, (uint8_t)shift_generic(c, m.reg, rm_get8(pc, &m), cnt, 8));
        break;
    }

    /* ---- RET ---- */
    case 0xC2: { uint16_t n = fetch16(pc); c->eip = cpu_pop16(pc); c->r[REG_SP] = (c->r[REG_SP] + n) & 0xFFFF; break; }
    case 0xC3: c->eip = cpu_pop16(pc); break;
    case 0xCA: { uint16_t n = fetch16(pc); c->eip = cpu_pop16(pc); c->sreg[SREG_CS] = cpu_pop16(pc); c->r[REG_SP] = (c->r[REG_SP] + n) & 0xFFFF; break; }
    case 0xCB: { c->eip = cpu_pop16(pc); c->sreg[SREG_CS] = cpu_pop16(pc); break; }

    case 0xC4: { decode_rm(pc, &m); /* LES: reg <- m16:16 */
        uint16_t off = rm_get16(pc, &m);
        uint16_t seg = seg_read16(pc, m.sreg, (uint16_t)(m.off + 2));
        cpu_set_r16(c, m.reg, off); c->sreg[SREG_ES] = seg; break; }
    case 0xC5: { decode_rm(pc, &m);
        uint16_t off = rm_get16(pc, &m);
        uint16_t seg = seg_read16(pc, m.sreg, (uint16_t)(m.off + 2));
        cpu_set_r16(c, m.reg, off); c->sreg[SREG_DS] = seg; break; }

    case 0xC6: { decode_rm(pc, &m); uint8_t v = fetch8(pc); rm_set8(pc, &m, v); break; }
    case 0xC7: { decode_rm(pc, &m); uint16_t v = fetch16(pc); rm_set16(pc, &m, v); break; }

    case 0xC8: case 0xC9: /* 80186 ENTER/LEAVE not required in phase 2 */
        break;
    case 0xCC: cpu_deliver_interrupt(pc, 3, 0, 0); break;                     /* INT3 */
    case 0xCD: { uint8_t n = fetch8(pc); cpu_deliver_interrupt(pc, n, 0, 0); break; }
    case 0xCE: if (c->eflags & FLAG_OF) cpu_deliver_interrupt(pc, 4, 0, 0); break; /* INTO */
    case 0xCF: {                                                 /* IRET */
        c->eip = cpu_pop16(pc);
        c->sreg[SREG_CS] = cpu_pop16(pc);
        c->eflags = cpu_pop16(pc) | FLAG_FIXED;
        break;
    }

    /* ---- AAM/AAD/SALC/XLAT ---- */
    case 0xD4: { uint8_t base = fetch8(pc); uint8_t a = AL(c);
        SET_AH(c, base ? (uint8_t)(a / base) : 0);
        SET_AL(c, base ? (uint8_t)(a % base) : 0);
        set_flags_logic8(c, AL(c)); break; }
    case 0xD5: { uint8_t base = fetch8(pc);
        uint8_t a = (uint8_t)(AH(c) * base + AL(c));
        SET_AL(c, a); SET_AH(c, 0); set_flags_logic8(c, a); break; }
    case 0xD6: SET_AL(c, (c->eflags & FLAG_CF) ? 0xFF : 0x00); break; /* SALC */
    case 0xD7: SET_AL(c, seg_read8(pc, SREG_DS, (uint16_t)(BX(c) + AL(c)))); break; /* XLAT */

    case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
        /* ESC / x87 escape: no FPU in phase 2. Consume ModR/M like the 8086. */
        decode_rm(pc, &m);
        break;

    /* ---- LOOP / JCXZ ---- */
    case 0xE0: {
        int8_t d = (int8_t)fetch8(pc); CX_SET_DEC(c);
        if (CX(c) != 0 && !(c->eflags & FLAG_ZF)) c->eip = (c->eip + d) & 0xFFFF;
        break;
    }
    case 0xE1: {
        int8_t d = (int8_t)fetch8(pc); CX_SET_DEC(c);
        if (CX(c) != 0 && (c->eflags & FLAG_ZF)) c->eip = (c->eip + d) & 0xFFFF;
        break;
    }
    case 0xE2: {
        int8_t d = (int8_t)fetch8(pc); CX_SET_DEC(c);
        if (CX(c) != 0) c->eip = (c->eip + d) & 0xFFFF;
        break;
    }
    case 0xE3: {
        int8_t d = (int8_t)fetch8(pc);
        if (CX(c) == 0) c->eip = (c->eip + d) & 0xFFFF;
        break;
    }

    /* ---- IN/OUT imm ---- */
    case 0xE4: { uint8_t p = fetch8(pc); uint32_t v = 0; io_port_in(pc, p, 1, &v); SET_AL(c, (uint8_t)v); break; }
    case 0xE5: { uint8_t p = fetch8(pc); uint32_t v = 0; io_port_in(pc, p, 2, &v); SET_AX(c, (uint16_t)v); break; }
    case 0xE6: { uint8_t p = fetch8(pc); io_port_out(pc, p, 1, AL(c)); break; }
    case 0xE7: { uint8_t p = fetch8(pc); io_port_out(pc, p, 2, AX(c)); break; }

    /* ---- CALL/JMP rel16, JMP rel8 ---- */
    case 0xE8: { int16_t d = (int16_t)fetch16(pc); cpu_push16(pc, (uint16_t)c->eip);
        c->eip = (c->eip + d) & 0xFFFF; break; }
    case 0xE9: jmp_rel16(pc); break;
    case 0xEA: { uint16_t off = fetch16(pc); uint16_t seg = fetch16(pc);
        c->eip = off; c->sreg[SREG_CS] = seg; break; }
    case 0xEB: jmp_rel8(pc); break;

    /* ---- IN/OUT DX ---- */
    case 0xEC: { uint32_t v = 0; io_port_in(pc, DX(c), 1, &v); SET_AL(c, (uint8_t)v); break; }
    case 0xED: { uint32_t v = 0; io_port_in(pc, DX(c), 2, &v); SET_AX(c, (uint16_t)v); break; }
    case 0xEE: io_port_out(pc, DX(c), 1, AL(c)); break;
    case 0xEF: io_port_out(pc, DX(c), 2, AX(c)); break;

    /* ---- HLT / flags ---- */
    case 0xF4: c->halted = 1; break;
    case 0xF5: c->eflags ^= FLAG_CF; break;                 /* CMC */
    case 0xF8: c->eflags &= ~FLAG_CF; break;                /* CLC */
    case 0xF9: c->eflags |= FLAG_CF; break;                 /* STC */
    case 0xFA: c->eflags &= ~FLAG_IF; break;                /* CLI */
    case 0xFB: c->eflags |= FLAG_IF; break;                 /* STI */
    case 0xFC: c->eflags &= ~FLAG_DF; break;                /* CLD */
    case 0xFD: c->eflags |= FLAG_DF; break;                 /* STD */

    /* ---- Group 3 (F6/F7) ---- */
    case 0xF6: case 0xF7: {
        int w = (op == 0xF7);
        decode_rm(pc, &m);
        switch (m.reg) {
            case 0: case 1:
                if (w) { uint16_t r = rm_get16(pc, &m) & fetch16(pc); set_flags_logic16(c, r); }
                else   { uint8_t r = rm_get8(pc, &m) & fetch8(pc); set_flags_logic8(c, r); }
                break;
            case 2: /* NOT */
                if (w) rm_set16(pc, &m, (uint16_t)~rm_get16(pc, &m));
                else   rm_set8(pc, &m, (uint8_t)~rm_get8(pc, &m));
                break;
            case 3: /* NEG */
                if (w) { uint16_t a = rm_get16(pc, &m); rm_set16(pc, &m, sub16f(c, 0, a, 0));
                         if (a != 0) c->eflags |= FLAG_CF; }
                else   { uint8_t a = rm_get8(pc, &m); rm_set8(pc, &m, sub8f(c, 0, a, 0));
                         if (a != 0) c->eflags |= FLAG_CF; }
                break;
            case 4: /* MUL */
                if (w) { uint32_t p = (uint32_t)AX(c) * rm_get16(pc, &m);
                    SET_AX(c, (uint16_t)p); SET_DX(c, (uint16_t)(p >> 16));
                    if (DX(c) != 0) c->eflags |= (FLAG_CF | FLAG_OF); else c->eflags &= ~(FLAG_CF | FLAG_OF); }
                else   { uint16_t p = (uint16_t)AL(c) * rm_get8(pc, &m);
                    SET_AX(c, p);
                    if (AH(c) != 0) c->eflags |= (FLAG_CF | FLAG_OF); else c->eflags &= ~(FLAG_CF | FLAG_OF); }
                break;
            case 5: /* IMUL */
                if (w) { int32_t p = (int32_t)(int16_t)AX(c) * (int32_t)(int16_t)rm_get16(pc, &m);
                    SET_AX(c, (uint16_t)p); SET_DX(c, (uint16_t)(p >> 16));
                    if (p != (int32_t)(int16_t)(uint16_t)p) c->eflags |= (FLAG_CF | FLAG_OF);
                    else c->eflags &= ~(FLAG_CF | FLAG_OF); }
                else   { int16_t p = (int16_t)((int8_t)AL(c) * (int8_t)rm_get8(pc, &m));
                    SET_AX(c, (uint16_t)p);
                    if (p != (int16_t)(int8_t)p) c->eflags |= (FLAG_CF | FLAG_OF);
                    else c->eflags &= ~(FLAG_CF | FLAG_OF); }
                break;
            case 6: /* DIV */
                if (w) { uint16_t d = rm_get16(pc, &m); uint32_t n = ((uint32_t)DX(c) << 16) | AX(c);
                    if (d == 0 || (n / d) > 0xFFFF) { div_error(pc); }
                    else { SET_AX(c, (uint16_t)(n / d)); SET_DX(c, (uint16_t)(n % d)); } }
                else   { uint8_t d = rm_get8(pc, &m); uint16_t n = AX(c);
                    if (d == 0 || (n / d) > 0xFF) { div_error(pc); }
                    else { SET_AL(c, (uint8_t)(n / d)); SET_AH(c, (uint8_t)(n % d)); } }
                break;
            case 7: /* IDIV */
                if (w) { int16_t d = (int16_t)rm_get16(pc, &m); int32_t n = (int32_t)(((uint32_t)DX(c) << 16) | AX(c));
                    if (d == 0 || ((n / d) > 32767) || ((n / d) < -32768)) { div_error(pc); }
                    else { SET_AX(c, (uint16_t)(n / d)); SET_DX(c, (uint16_t)(n % d)); } }
                else   { int8_t d = (int8_t)rm_get8(pc, &m); int16_t n = (int16_t)AX(c);
                    if (d == 0 || ((n / d) > 127) || ((n / d) < -128)) { div_error(pc); }
                    else { SET_AL(c, (uint8_t)(n / d)); SET_AH(c, (uint8_t)(n % d)); } }
                break;
        }
        break;
    }

    /* ---- Group 4 (FE): INC/DEC r/m8 ---- */
    case 0xFE: {
        decode_rm(pc, &m);
        if (m.reg == 0) { uint8_t a = rm_get8(pc, &m); uint8_t r = (uint8_t)(a + 1); set_flags_inc8(c, a, r); rm_set8(pc, &m, r); }
        else if (m.reg == 1) { uint8_t a = rm_get8(pc, &m); uint8_t r = (uint8_t)(a - 1); set_flags_dec8(c, a, r); rm_set8(pc, &m, r); }
        break;
    }
    /* ---- Group 5 (FF) ---- */
    case 0xFF: {
        decode_rm(pc, &m);
        switch (m.reg) {
            case 0: { uint16_t a = rm_get16(pc, &m); uint16_t r = (uint16_t)(a + 1); set_flags_inc16(c, a, r); rm_set16(pc, &m, r); break; }
            case 1: { uint16_t a = rm_get16(pc, &m); uint16_t r = (uint16_t)(a - 1); set_flags_dec16(c, a, r); rm_set16(pc, &m, r); break; }
            case 2: cpu_push16(pc, (uint16_t)c->eip); c->eip = rm_get16(pc, &m); break;   /* CALL near */
            case 3: { /* CALL far */
                uint16_t off = rm_get16(pc, &m);
                uint16_t seg = seg_read16(pc, m.sreg, (uint16_t)(m.off + 2));
                cpu_push16(pc, c->sreg[SREG_CS]); cpu_push16(pc, (uint16_t)c->eip);
                c->sreg[SREG_CS] = seg; c->eip = off; break; }
            case 4: c->eip = rm_get16(pc, &m); break;                                     /* JMP near */
            case 5: { uint16_t off = rm_get16(pc, &m);
                uint16_t seg = seg_read16(pc, m.sreg, (uint16_t)(m.off + 2));
                c->eip = off; c->sreg[SREG_CS] = seg; break; }                            /* JMP far */
            case 6: cpu_push16(pc, rm_get16(pc, &m)); break;                              /* PUSH r/m */
            case 7: break;                                                                /* undefined */
        }
        break;
    }

    case 0x0F:
        return cpu_step_0f(pc);

    default:
        c->fault = 1;
        c->fault_eip = c->eip - 1;
        fault_msg_fmt(c->fault_msg, sizeof(c->fault_msg),
                      "unimplemented opcode 0x",
                      op, c->sreg[SREG_CS], (uint16_t)(c->eip - 1));
        return 1;
    }

    return 0;
}

uint64_t cpu_run(pc_t *pc, uint64_t max_steps)
{
    uint64_t steps = 0;
    while (pc->running && !pc->cpu.halted && !pc->cpu.fault && steps < max_steps) {
        if (cpu_step(pc) != 0) break;
        steps++;
        if ((steps & 0xFF) == 0) {
            pit_advance(pc);
            pic_deliver(pc);
        }
    }
    return steps;
}
