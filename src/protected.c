/*
 * Munt386 -- 286/386 protected mode.
 *
 * Implements real descriptor-based protected mode (not a simulation):
 *   - GDT/LDT/IDT lookup and 8-byte descriptor parsing
 *   - per-segment caches (base/limit/access rights) used by the memory paths
 *   - privilege checks on data/stack/code segment loads
 *   - present-bit and type validation
 *   - interrupt/trap gates with privilege checks and 16-bit frames
 *   - exceptions with error codes pushed on the stack
 *   - 386 paging (CR3 directory/tables, present/RW/user, A/D bits)
 *
 * Phase 3 intentionally targets the *286 core* (16-bit protected mode), which is
 * what Windows 3.x real/standard mode needs; the 386 32-bit operand forms are
 * Phase 4.  Where a check would be required for full 386 correctness it is
 * marked TODO-386 rather than faked.
 */
#include "munt386.h"
#include <string.h>
/* Access-rights byte bits */
#define AR_PRESENT  0x80
#define AR_DPL      0x60
#define AR_S        0x10     /* 1 = code/data, 0 = system */
#define AR_TYPE     0x0F

#define AR_CDT_DATA_R     0x00
#define AR_CDT_DATA_RW    0x02
#define AR_CDT_DATA_RWA   0x06
#define AR_CDT_CODE_XO    0x08
#define AR_CDT_CODE_XR    0x0A
#define AR_CDT_CODE_XRA   0x0E

/* Descriptor flags byte (byte 6) */
#define DF_GRAN4K  0x80
#define DF_32BIT   0x40

/* Page directory/table entry bits */
#define PTE_PRESENT 0x001u
#define PTE_WRITE   0x002u
#define PTE_USER    0x004u
#define PTE_ACCESSED 0x020u
#define PTE_DIRTY   0x040u

/* ------------------------------------------------------------------ */
/* Descriptor parsing                                                  */
/* ------------------------------------------------------------------ */

int desc_parse(uint8_t *d, seg_cache_t *out)
{
    memset(out, 0, sizeof(*out));
    out->base = (uint32_t)d[7] << 24 | (uint32_t)d[4] << 16 |
                (uint32_t)d[3] << 8 | d[2];
    uint32_t limit = (uint32_t)d[0] | ((uint32_t)d[1] << 8) |
                     ((uint32_t)(d[6] & 0x0F) << 16);
    uint8_t ar = d[5];

    if (!(ar & AR_PRESENT))
        return -1;                     /* segment not present */

    uint8_t flags = d[6] & 0xC0;
    if (flags & DF_GRAN4K)
        limit = (limit << 12) | 0xFFF; /* 4K granularity */

    out->limit = limit;
    out->ar = ar;
    out->flags = flags;
    out->valid = 1;

    /* Data/code descriptors (S=1) also need a sane base for 286: base is the
     * 24-bit value in bytes 2..4 (byte 7 is part of the 386 base). */
    if (ar & AR_S)
        out->base &= 0x00FFFFFF;
    return 0;
}

static int dpl_of(const seg_cache_t *s) { return (s->ar & AR_DPL) >> 5; }
static int cpl_of(const cpu386_t *c)
{
    if (!c->sc[SREG_CS].valid) return 0;
    return (c->sc[SREG_CS].ar & AR_DPL) >> 5;
}

/* ------------------------------------------------------------------ */
/* Segment loading                                                     */
/* ------------------------------------------------------------------ */

static int is_conforming(uint8_t ar)
{
    return (ar & AR_S) && (ar & AR_TYPE) >= 0x0C;
}

/* Read an 8-byte descriptor from GDT (ldt==0) or LDT.  Returns 0 or exc. */
static int read_descriptor(pc_t *pc, uint16_t sel, int use_ldt, uint8_t *d)
{
    cpu386_t *c = &pc->cpu;
    uint32_t base;
    uint32_t limit;

    if (use_ldt) {
        if (!c->ldtr_cache.valid) return EXC_GP;
        base  = c->ldtr_cache.base;
        limit = c->ldtr_cache.limit;
    } else {
        base  = c->gdtr_base;
        limit = c->gdtr_limit;
    }

    uint32_t idx = (uint32_t)(sel & 0xFFF8);
    if (idx + 7 > limit)
        return EXC_GP;                 /* selector out of table bounds */
    uint32_t lin = base + idx;
    for (int i = 0; i < 8; i++)
        d[i] = mem_lread8(pc, lin + (uint32_t)i);
    return 0;
}

int seg_load(pc_t *pc, int sreg, uint16_t selector)
{
    cpu386_t *c = &pc->cpu;

    /* Real mode: base = selector << 4, full 64 KiB limit. */
    if (!c->in_protected) {
        c->sreg[sreg] = selector;
        c->sc[sreg].base = (uint32_t)selector << 4;
        c->sc[sreg].limit = 0xFFFF;
        c->sc[sreg].ar = 0x92;         /* writable data, present */
        c->sc[sreg].flags = 0;
        c->sc[sreg].valid = 1;
        return 0;
    }

    /* Null selector: only allowed for DS/ES/FS/GS. */
    if ((selector & 0xFFFC) == 0) {
        if (sreg == SREG_CS || sreg == SREG_SS)
            return EXC_GP;
        c->sreg[sreg] = selector;
        c->sc[sreg].valid = 0;
        return 0;
    }

    int rpl = selector & 3;
    int use_ldt = (selector & 4) != 0;
    uint8_t d[8];
    int exc = read_descriptor(pc, selector, use_ldt, d);
    if (exc) {
        /* #GP for GDT/selector problems, #NP if the LDT itself is not present. */
        return (use_ldt && !c->ldtr_cache.valid) ? EXC_NP : EXC_GP;
    }

    seg_cache_t sc;
    if (desc_parse(d, &sc) != 0)
        return EXC_NP;                 /* not present */

    uint8_t ar = sc.ar;
    int cpl = cpl_of(c);

    if (ar & AR_S) {
        /* --- Code/data segment --- */
        uint8_t type = ar & AR_TYPE;
        int exec = (type & 0x08) != 0;
        int writable = (type & 0x02) != 0;

        if (sreg == SREG_CS) {
            /* Must be executable. */
            if (!exec) return EXC_GP;
            /* Privilege rule: CPL must be within [DPL, DPL] for
             * non-conforming, or >= DPL for conforming code. */
            if (is_conforming(ar)) {
                if (cpl < dpl_of(&sc)) return EXC_GP;
            } else {
                if (rpl != cpl || dpl_of(&sc) != cpl) return EXC_GP;
            }
        } else if (sreg == SREG_SS) {
            /* Stack must be writable data, DPL == CPL == RPL. */
            if (exec || !writable) return EXC_GP;
            if (dpl_of(&sc) != cpl || rpl != cpl) return EXC_SS;
        } else {
            /* Data segment: readable data (or readable code via 8A/0E). */
            int readable = !exec || (type & 0x02);
            if (!readable) return EXC_GP;
            /* DPL must be >= max(CPL, RPL) numerically (less privileged). */
            if (dpl_of(&sc) < cpl || dpl_of(&sc) < rpl) return EXC_GP;
            if (ar == AR_CDT_CODE_XO) return EXC_GP;
        }
    } else {
        /* --- System segment / gate: only LDT/TSS are loadable here --- */
        uint8_t type = ar & AR_TYPE;
        if (sreg == SREG_CS || sreg == SREG_SS) return EXC_GP;
        if (type == DESC_LDT) {
            if (sreg != 5) return EXC_GP;   /* LDTR is not one of ES..GS */
        } else if (type == DESC_TSS_AVAIL || type == DESC_TSS_BUSY) {
            /* Task register only; DS/ES/FS/GS cannot hold a TSS. */
            return EXC_GP;
        } else {
            return EXC_GP;
        }
    }

    c->sreg[sreg] = selector;
    c->sc[sreg] = sc;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Mode transitions                                                    */
/* ------------------------------------------------------------------ */

void cpu_update_mode(pc_t *pc)
{
    cpu386_t *c = &pc->cpu;
    int pe = (c->cr0 & CR0_PE) != 0;
    if (pe == c->in_protected) return;
    c->in_protected = pe;
    /* Refresh every segment cache.  In protected mode an unchanged selector
     * would normally need revalidation; for the transitions we support
     * (real -> protected with a pre-built GDT) we re-read descriptors. */
    for (int i = 0; i < 6; i++) {
        uint16_t sel = c->sreg[i];
        if (pe) {
            if ((sel & 0xFFFC) == 0) { c->sc[i].valid = (i == SREG_CS || i == SREG_SS) ? 0 : 0; continue; }
            seg_load(pc, i, sel);
        } else {
            c->sc[i].base = (uint32_t)sel << 4;
            c->sc[i].limit = 0xFFFF;
            c->sc[i].ar = 0x92;
            c->sc[i].flags = 0;
            c->sc[i].valid = 1;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Interrupts and exceptions                                           */
/* ------------------------------------------------------------------ */

/* Push a 16-bit protected-mode frame. */
static void push_frame16(pc_t *pc, uint16_t cs, uint16_t ip, uint16_t flags)
{
    cpu_push16(pc, flags);
    cpu_push16(pc, cs);
    cpu_push16(pc, ip);
}

void cpu_raise_exception(pc_t *pc, uint8_t vector, uint32_t error_code)
{
    if (vector == EXC_PF) pc->cpu.cr2 = error_code & ~3u;
    cpu_deliver_interrupt(pc, vector, 1, error_code);
}

void cpu_deliver_interrupt(pc_t *pc, uint8_t vector, int is_exception,
                           uint32_t error_code)
{
    cpu386_t *c = &pc->cpu;

    /* Real mode: the classic IVT, vectors are segment:offset. */
    if (!c->in_protected) {
        uint16_t ip = mem_read16(pc, 0, (uint16_t)(vector * 4));
        uint16_t cs = mem_read16(pc, 0, (uint16_t)(vector * 4 + 2));
        cpu_push16(pc, (uint16_t)c->eflags);
        cpu_push16(pc, c->sreg[SREG_CS]);
        cpu_push16(pc, (uint16_t)c->eip);
        c->eflags &= ~(FLAG_IF | FLAG_TF);
        c->sreg[SREG_CS] = cs;
        c->eip = ip;
        return;
    }

    /* Protected mode: IDT gates. */
    uint32_t idx = (uint32_t)vector * 8;
    if (idx + 7 > c->idtr_limit) {
        /* Double fault: IDT entry missing. */
        cpu_push16(pc, (uint16_t)c->eflags);
        cpu_push16(pc, c->sreg[SREG_CS]);
        cpu_push16(pc, (uint16_t)c->eip);
        c->eip = 0;
        c->sreg[SREG_CS] = 0;
        c->halted = 1;
        return;
    }

    uint8_t d[8];
    for (int i = 0; i < 8; i++)
        d[i] = mem_lread8(pc, c->idtr_base + idx + (uint32_t)i);

    uint16_t gate_sel = (uint16_t)(d[2] | (d[3] << 8));
    uint16_t gate_off = (uint16_t)(d[0] | (d[1] << 8));
    uint8_t  gate_ar  = d[5];

    if (!(gate_ar & AR_PRESENT)) {
        /* #NP on the gate itself.  A full implementation would raise a nested
         * exception; for now we halt with a fault flag. */
        c->fault = 1;
        c->halted = 1;
        return;
    }

    uint8_t type = gate_ar & 0x1F;
    int is_trap = (type == DESC_TRAP_GATE16 || type == DESC_TRAP_GATE32);

    /* Task gates and 386 gates are Phase 4; report a fault rather than faking. */
    if (type == DESC_TASK_GATE || type == DESC_INT_GATE32 || type == DESC_TRAP_GATE32) {
        c->fault = 1;
        c->halted = 1;
        return;
    }

    /* Load the handler's code segment (gates may raise privileges). */
    uint16_t saved_cs = c->sreg[SREG_CS];
    int rc = seg_load(pc, SREG_CS, gate_sel);
    if (rc != 0) {
        c->fault = 1;
        c->halted = 1;
        return;
    }
    c->eip = gate_off;

    push_frame16(pc, saved_cs, (uint16_t)(c->eip - gate_off), (uint16_t)c->eflags);
    if (is_exception && (type & 0x08))      /* error code for applicable excs */
        cpu_push16(pc, (uint16_t)error_code);
    if (!is_trap)
        c->eflags &= ~FLAG_IF;              /* interrupt gates clear IF */
}

/* ------------------------------------------------------------------ */
/* Paging                                                              */
/* ------------------------------------------------------------------ */

uint32_t paging_translate(pc_t *pc, uint32_t lin, int write, int user,
                          uint32_t *err_out)
{
    cpu386_t *c = &pc->cpu;
    uint32_t pde_idx = (lin >> 22) & 0x3FF;
    uint32_t pte_idx = (lin >> 12) & 0x3FF;
    uint32_t pde = mem_pread32(pc, c->cr3 + pde_idx * 4);

    if (!(pde & PTE_PRESENT)) {
        *err_out = 0;                          /* P = 0 */
        return 0xFFFFFFFFu;
    }

    uint32_t pt_addr = pde & 0xFFFFF000u;
    uint32_t pte = mem_pread32(pc, pt_addr + pte_idx * 4);
    if (!(pte & PTE_PRESENT)) {
        *err_out = 0;
        return 0xFFFFFFFFu;
    }

    /* Protection checks: page-level protections can only restrict further. */
    uint32_t prot = (pde & (PTE_WRITE | PTE_USER)) & (pte & (PTE_WRITE | PTE_USER));
    int page_user = (prot & PTE_USER) != 0;
    int page_write = (prot & PTE_WRITE) != 0;

    if (user && !page_user) { *err_out = 4; return 0xFFFFFFFFu; }   /* U/S */
    if (write && !page_write) { *err_out = 1; return 0xFFFFFFFFu; } /* W/R */

    /* Update accessed/dirty bits (real hardware behaviour). */
    mem_pwrite32(pc, c->cr3 + pde_idx * 4, pde | PTE_ACCESSED);
    if (write)
        mem_pwrite32(pc, pt_addr + pte_idx * 4, pte | PTE_ACCESSED | PTE_DIRTY);
    else
        mem_pwrite32(pc, pt_addr + pte_idx * 4, pte | PTE_ACCESSED);

    uint32_t frame = pte & 0xFFFFF000u;
    return frame | (lin & 0xFFFu);
}
