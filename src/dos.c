/*
 * Munt386 -- minimal DOS compatibility layer.
 *
 * This is the compatibility shim used when no real DOS is loaded: a small but
 * genuine implementation of the INT 21h services that console programs rely
 * on, plus DOS-style memory allocation.  When a real DOS image boots, it
 * installs its own INT 21h handler and this layer is bypassed entirely (the
 * IVT no longer points at our stub).
 *
 * File-system services are not implemented yet; they return "invalid function"
 * so callers fail cleanly.  See docs/ROADMAP.md.
 */
#include "munt386.h"
#include <string.h>

/* A trivial DOS arena used by INT 21h AH=48h/4Ah/49h. Real DOS manages an
 * MCB chain; we provide a single bump allocator starting at segment 0x1000
 * (above the 64 KiB the loader uses) so simple console programs that ask for
 * a block and use it succeed. */
#define DOS_ARENA_SEG   0x1000
#define DOS_ARENA_END   0x9000u

static uint16_t dos_brk = DOS_ARENA_SEG;

static void dos_fail(pc_t *pc, uint16_t code)
{
    pc->cpu.eflags |= FLAG_CF;
    SET_AX(&pc->cpu, code);
}

static void dos_ok(pc_t *pc, uint16_t ax)
{
    pc->cpu.eflags &= ~FLAG_CF;
    SET_AX(&pc->cpu, ax);
}

static void dos_puts(pc_t *pc, uint16_t seg, uint16_t off)
{
    for (uint32_t i = 0; i < 0x10000; i++) {
        uint8_t ch = mem_read8(pc, seg, (uint16_t)(off + i));
        if (ch == '$') break;
        bios_teletype(pc, ch);
    }
}

static int console_in(pc_t *pc, int echo)
{
    uint16_t tail = mem_pread16(pc, 0x400 + 0x1C);
    uint16_t head = mem_pread16(pc, 0x400 + 0x1A);
    if (head == tail) return 0;
    if (head < 0x1E || head >= 0x3E) head = 0x1E;
    uint8_t ascii = mem_pread8(pc, 0x400 + head);
    head += 2; if (head >= 0x3E) head = 0x1E;
    mem_pwrite16(pc, 0x400 + 0x1A, head);
    SET_AL(&pc->cpu, ascii);
    if (echo) bios_teletype(pc, ascii);
    return 1;
}

static int int21(pc_t *pc)
{
    cpu386_t *c = &pc->cpu;
    uint16_t ax = AX(c);
    uint8_t ah = (uint8_t)(ax >> 8);

    switch (ah) {
        case 0x00: case 0x4C:                       /* terminate */
            pc->exit_code = AL(c);
            pc->running = 0;
            return 1;

        case 0x01:                                  /* read char, echo */
            if (!console_in(pc, 1)) SET_AL(c, 0);
            return 1;
        case 0x07: case 0x08:                       /* read char, no echo */
            if (!console_in(pc, 0)) SET_AL(c, 0);
            return 1;
        case 0x06:                                  /* direct console I/O */
            if (DL(c) == 0xFF) { if (!console_in(pc, 0)) c->eflags |= FLAG_ZF; else c->eflags &= ~FLAG_ZF; }
            else bios_teletype(pc, DL(c));
            return 1;
        case 0x02: bios_teletype(pc, DL(c)); return 1;   /* output char */
        case 0x09: dos_puts(pc, c->sreg[SREG_DS], DX(c)); return 1; /* print string */
        case 0x0A: {                                /* buffered input */
            /* Minimal: read one key into the buffer, terminate with CR. */
            uint16_t off = DX(c);
            if (console_in(pc, 1)) {
                uint8_t ch = AL(c);
                mem_write8(pc, c->sreg[SREG_DS], (uint16_t)(off + 2), ch);
                mem_write8(pc, c->sreg[SREG_DS], (uint16_t)(off + 1), 1);
            } else {
                mem_write8(pc, c->sreg[SREG_DS], (uint16_t)(off + 1), 0);
            }
            return 1;
        }
        case 0x0B: {                                /* check input status */
            uint16_t tail = mem_pread16(pc, 0x41C);
            uint16_t head = mem_pread16(pc, 0x41A);
            SET_AL(c, (head != tail) ? 0xFF : 0x00);
            return 1;
        }
        case 0x0C: return 1;                        /* flush + read (simplified) */
        case 0x0D: dos_ok(pc, 0); return 1;         /* disk reset */
        case 0x0E: SET_AL(c, (uint8_t)(DL(c))); return 1; /* select drive */
        case 0x19: SET_AL(c, 2); return 1;          /* current drive = C: */
        case 0x1A: return 1;                        /* set DTA (not modelled) */
        case 0x25: {                                /* set interrupt vector */
            uint8_t vec = AL(c);
            mem_pwrite16(pc, (uint32_t)vec * 4, DX(c));
            mem_pwrite16(pc, (uint32_t)vec * 4 + 2, c->sreg[SREG_DS]);
            return 1;
        }
        case 0x2A: {                                /* get date */
            uint8_t *r = pc->cmos.ram;
            SET_CX(c, 2000 + r[0x09]);
            SET_DH(c, r[0x08]);
            SET_DL(c, r[0x07]);
            SET_AL(c, r[0x06]);
            return 1;
        }
        case 0x2C: {                                /* get time */
            uint8_t *r = pc->cmos.ram;
            SET_CH(c, r[0x04]); SET_CL(c, r[0x02]);
            SET_DH(c, r[0x00]); SET_DL(c, 0);
            return 1;
        }
        case 0x2F: SET_BX(c, 0); c->sreg[SREG_ES] = 0; return 1;  /* get DTA */
        case 0x30: SET_AL(c, 6); SET_AH(c, 0); SET_BX(c, 0); SET_CX(c, 0); return 1; /* DOS 6.0 */
        case 0x33: SET_AL(c, 0); SET_DX(c, 0); return 1;  /* Ctrl-Break check */
        case 0x35: {                                /* get interrupt vector */
            uint8_t vec = AL(c);
            SET_BX(c, mem_read16(pc, 0, (uint16_t)(vec * 4)));
            c->sreg[SREG_ES] = mem_read16(pc, 0, (uint16_t)(vec * 4 + 2));
            return 1;
        }
        case 0x3B: case 0x39: case 0x3A: case 0x41: case 0x43: case 0x56:
            dos_fail(pc, 2); return 1;              /* file ops: not found */
        case 0x3C: case 0x3D: case 0x5B: dos_fail(pc, 3); return 1; /* open/create: not found */
        case 0x3E: case 0x45: case 0x46: dos_fail(pc, 6); return 1;
        case 0x3F: dos_ok(pc, 0); return 1;         /* read: end of file */
        case 0x40: dos_ok(pc, 0); return 1;         /* write: nothing */
        case 0x42: SET_DX(c, 0); SET_AX(c, 0); return 1; /* lseek -> 0 */
        case 0x44: dos_fail(pc, 1); return 1;       /* IOCTL unsupported */
        case 0x47: {                                /* get current dir */
            mem_write8(pc, c->sreg[SREG_DS], SI(c), 0);
            dos_ok(pc, 0x0100);
            return 1;
        }
        case 0x48: {                                /* allocate memory */
            uint16_t want = BX(c);
            if (want == 0xFFFF) { SET_BX(c, (uint16_t)(DOS_ARENA_END - dos_brk)); dos_ok(pc, 0); return 1; }
            if ((uint32_t)dos_brk + want > DOS_ARENA_END) { dos_fail(pc, 8); SET_BX(c, (uint16_t)(DOS_ARENA_END - dos_brk)); return 1; }
            SET_AX(c, dos_brk);
            dos_brk = (uint16_t)(dos_brk + want);
            c->eflags &= ~FLAG_CF;
            return 1;
        }
        case 0x49: dos_ok(pc, 0); return 1;         /* free memory */
        case 0x4A: {                                /* resize block (best effort) */
            uint16_t seg = c->sreg[SREG_ES];
            uint16_t want = BX(c);
            uint32_t end = (uint32_t)seg + want;
            if (end <= DOS_ARENA_END) { dos_brk = (uint16_t)end; dos_ok(pc, 0); }
            else dos_fail(pc, 8);
            return 1;
        }
        case 0x4B: dos_fail(pc, 8); return 1;       /* exec: unsupported */
        case 0x4D: SET_AX(c, 0); return 1;          /* get return code */
        case 0x4E: case 0x4F: dos_fail(pc, 18); return 1; /* find first/next */
        case 0x58: dos_ok(pc, 0); return 1;         /* allocation strategy */
        case 0x50: case 0x51: case 0x62: SET_BX(c, 0); return 1;
        default: dos_fail(pc, 1); return 1;
    }
}

int dos_handle_int(pc_t *pc, uint8_t vector)
{
    switch (vector) {
        case 0x20: pc->running = 0; return 1;       /* terminate */
        case 0x21: return int21(pc);
        case 0x22: case 0x23: case 0x24: case 0x27: case 0x28:
            return 1;                               /* no-op DOS vectors */
        default: return 0;
    }
}
