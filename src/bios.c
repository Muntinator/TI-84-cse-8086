/*
 * Munt386 -- minimal PC BIOS compatibility layer.
 *
 * Only the services DOS and early Windows actually call are implemented.  Each
 * handler returns 1 if it recognised the vector.  Handlers run in the context
 * of a trapped INT, so they may modify the saved interrupt frame (used for
 * INT 19h boot and for chaining the INT 1Ch user timer tick).
 */
#include "munt386.h"
#include <string.h>

#define BDA(off) (0x400u + (uint32_t)(off))

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Chain the current trapped interrupt to another vector by rewriting the
 * pending return frame so the outer trap returns into `vec` while the stack
 * retains the original return address for that handler's IRET. */
static void chain_int(pc_t *pc, uint8_t vec)
{
    cpu386_t *c = &pc->cpu;
    uint16_t sp = (uint16_t)c->r[REG_SP];

    uint16_t orig_ip = mem_read16(pc, c->sreg[SREG_SS], sp);
    uint16_t orig_cs = mem_read16(pc, c->sreg[SREG_SS], (uint16_t)(sp + 2));
    uint16_t orig_fl = mem_read16(pc, c->sreg[SREG_SS], (uint16_t)(sp + 4));

    cpu_push16(pc, orig_fl);
    cpu_push16(pc, orig_cs);
    cpu_push16(pc, orig_ip);

    uint16_t nsp = (uint16_t)c->r[REG_SP];
    uint16_t vip = mem_read16(pc, 0, (uint16_t)(vec * 4));
    uint16_t vcs = mem_read16(pc, 0, (uint16_t)(vec * 4 + 2));
    mem_write16(pc, c->sreg[SREG_SS], nsp, vip);
    mem_write16(pc, c->sreg[SREG_SS], (uint16_t)(nsp + 2), vcs);
}

static int vector_is_guest(pc_t *pc, uint8_t vec)
{
    uint16_t cs = mem_read16(pc, 0, (uint16_t)(vec * 4 + 2));
    uint16_t ip = mem_read16(pc, 0, (uint16_t)(vec * 4));
    if (cs == 0xF000 && ip >= 0x1000 && ip < 0x1400) return 0;
    return 1;
}

static void eoi(pc_t *pc, int irq)
{
    (void)irq;
    io_port_out(pc, 0x20, 1, 0x20);
}

/* Pop one (ascii,scan) pair from the BIOS keyboard ring buffer. */
static int bios_kbd_pop(pc_t *pc, uint8_t *ascii, uint8_t *scan)
{
    uint16_t head = mem_pread16(pc, BDA(0x1A));
    uint16_t tail = mem_pread16(pc, BDA(0x1C));
    if (head == tail) return 0;
    if (head < 0x1E || head >= 0x3E) head = 0x1E;
    *ascii = mem_pread8(pc, BDA(head));
    *scan = mem_pread8(pc, BDA(head + 1));
    head += 2;
    if (head >= 0x3E) head = 0x1E;
    mem_pwrite16(pc, BDA(0x1A), head);
    return 1;
}

static int bios_kbd_peek(pc_t *pc, uint8_t *ascii, uint8_t *scan)
{
    uint16_t head = mem_pread16(pc, BDA(0x1A));
    uint16_t tail = mem_pread16(pc, BDA(0x1C));
    if (head == tail) return 0;
    if (head < 0x1E || head >= 0x3E) head = 0x1E;
    *ascii = mem_pread8(pc, BDA(head));
    *scan = mem_pread8(pc, BDA(head + 1));
    return 1;
}

/* ------------------------------------------------------------------ */
/* Video (INT 10h)                                                     */
/* ------------------------------------------------------------------ */

static void text_scroll(pc_t *pc, vga_t *v, int lines, uint8_t attr,
                        int top, int left, int bottom, int right, int up)
{
    if (lines <= 0 || lines > bottom - top) lines = bottom - top;
    int w = right - left + 1;
    for (int row = top; row <= bottom; row++) {
        int src = up ? row + lines : row - lines;
        for (int col = left; col <= right; col++) {
            uint32_t d = v->base_phys + (uint32_t)(row * v->cols + col) * 2;
            if (src >= top && src <= bottom) {
                uint32_t s = v->base_phys + (uint32_t)(src * v->cols + col) * 2;
                mem_pwrite8(pc, d, mem_pread8(pc, s));
                mem_pwrite8(pc, d + 1, mem_pread8(pc, s + 1));
            } else {
                mem_pwrite8(pc, d, ' ');
                mem_pwrite8(pc, d + 1, attr);
            }
        }
        (void)w;
    }
    v->dirty = 1;
}

void bios_teletype(pc_t *pc, uint8_t ch)
{
    vga_t *v = &pc->vga;
    if (ch == 7) return;                     /* BEL */
    if (ch == 8) { if (v->cursor_col) v->cursor_col--; return; }
    if (ch == 13) { v->cursor_col = 0; return; }
    if (ch == 10) {
        v->cursor_row++;
    } else {
        uint32_t cell = v->base_phys + (uint32_t)(v->cursor_row * v->cols + v->cursor_col) * 2;
        mem_pwrite8(pc, cell, ch);
        mem_pwrite8(pc, cell + 1, v->attr);
        if (++v->cursor_col >= v->cols) { v->cursor_col = 0; v->cursor_row++; }
    }
    if (v->cursor_row >= v->rows) {
        text_scroll(pc, v, 1, v->attr, 0, 0, v->rows - 1, v->cols - 1, 1);
        v->cursor_row = v->rows - 1;
    }
    v->dirty = 1;
}

static int int10(pc_t *pc)
{
    cpu386_t *c = &pc->cpu;
    vga_t *v = &pc->vga;
    uint8_t ah = AH(c);
    switch (ah) {
        case 0x00: vga_set_mode(pc, AL(c)); SET_AH(c, 0); break;
        case 0x01: v->cursor_start = CH(c); v->cursor_end = CL(c); break;
        case 0x02: v->cursor_row = DH(c); v->cursor_col = DL(c); break;
        case 0x03: SET_DH(c, v->cursor_row); SET_DL(c, v->cursor_col);
                   SET_CH(c, v->cursor_start); SET_CL(c, v->cursor_end); break;
        case 0x05: v->page = BH(c); break;
        case 0x06: text_scroll(pc, v, AL(c), BH(c), CH(c), CL(c), DH(c), DL(c), 1); break;
        case 0x07: text_scroll(pc, v, AL(c), BH(c), CH(c), CL(c), DH(c), DL(c), 0); break;
        case 0x08: { uint32_t cell = v->base_phys + (uint32_t)(v->cursor_row * v->cols + v->cursor_col) * 2;
                     SET_AL(c, mem_pread8(pc, cell)); SET_AH(c, mem_pread8(pc, cell + 1)); break; }
        case 0x09: case 0x0A: {
            uint8_t ch2 = AL(c), attr = (ah == 0x09) ? BL(c) : v->attr;
            int n = CX(c);
            if (n <= 0) n = 1;
            for (int i = 0; i < n; i++) {
                int col = v->cursor_col + i;
                int row = v->cursor_row;
                if (col >= v->cols) { col -= v->cols; row++; }
                if (row >= v->rows) break;
                uint32_t cell = v->base_phys + (uint32_t)(row * v->cols + col) * 2;
                mem_pwrite8(pc, cell, ch2);
                if (ah == 0x09) mem_pwrite8(pc, cell + 1, attr);
            }
            v->dirty = 1;
            break;
        }
        case 0x0B: /* set palette / background */
            if (BH(c) == 0) v->attr = (uint8_t)((v->attr & 0xF0) | (BL(c) & 0x0F));
            else if (BH(c) == 1) v->attr = (uint8_t)((v->attr & 0x0F) | ((BL(c) & 0x0F) << 4));
            break;
        case 0x0C: { /* write pixel */
            int x = CX(c), y = DX(c);
            uint8_t color = AL(c);
            if (v->mode == 0x13) mem_pwrite8(pc, X86_VGA_BASE + (uint32_t)(y * 320 + x), color);
            else {
                uint32_t rowbase = v->base_phys + (uint32_t)((y & 1) ? 0x2000 : 0) + (uint32_t)(y / 2) * 80;
                uint8_t b = mem_pread8(pc, rowbase + (uint32_t)(x / 4));
                int sh = 6 - (x % 4) * 2;
                b = (uint8_t)((b & ~(3u << sh)) | ((color & 3u) << sh));
                mem_pwrite8(pc, rowbase + (uint32_t)(x / 4), b);
            }
            v->dirty = 1;
            break;
        }
        case 0x0D: { int x = CX(c), y = DX(c);
            if (v->mode == 0x13) SET_AL(c, mem_pread8(pc, X86_VGA_BASE + (uint32_t)(y * 320 + x)));
            else { uint32_t rowbase = v->base_phys + (uint32_t)((y & 1) ? 0x2000 : 0) + (uint32_t)(y / 2) * 80;
                   uint8_t b = mem_pread8(pc, rowbase + (uint32_t)(x / 4));
                   SET_AL(c, (uint8_t)((b >> (6 - (x % 4) * 2)) & 3)); }
            break; }
        case 0x0E: bios_teletype(pc, AL(c)); break;
        case 0x0F: SET_AL(c, v->mode); SET_AH(c, (uint8_t)v->cols); SET_BH(c, v->page); break;
        case 0x10: case 0x11: case 0x12: case 0x1A: case 0x1B: case 0x1C: break; /* accept, no-op */
        default: break;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Disk (INT 13h)                                                      */
/* ------------------------------------------------------------------ */

static disk_t *disk_for_drive(pc_t *pc, uint8_t dl)
{
    if (dl < 2) return &pc->disk[dl];
    if (dl >= 0x80 && dl < 0x82) return &pc->disk[2 + (dl - 0x80)];
    return NULL;
}

static int do_disk_xfer(pc_t *pc, int write)
{
    cpu386_t *c = &pc->cpu;
    disk_t *d = disk_for_drive(pc, DL(c));
    if (!d || !d->present) { c->eflags |= FLAG_CF; SET_AH(c, 0x01); return 1; }

    int count = AL(c);
    int cyl = ((CL(c) & 0xC0) << 2) | CH(c);
    int sec = CL(c) & 0x3F;
    int head = DH(c);
    if (count == 0) count = 1;
    if (sec == 0) { c->eflags |= FLAG_CF; SET_AH(c, 0x04); return 1; }

    uint32_t lba = (uint32_t)((cyl * d->heads + head) * d->sectors + (sec - 1));
    uint8_t buf[DISK_SECTOR_SIZE];
    uint16_t bx = BX(c);

    for (int i = 0; i < count; i++) {
        int rc;
        if (write) {
            for (int b = 0; b < DISK_SECTOR_SIZE; b++)
                buf[b] = mem_read8(pc, c->sreg[SREG_ES], (uint16_t)(bx + i * DISK_SECTOR_SIZE + b));
            rc = disk_write_sector(d, lba + i, buf);
        } else {
            rc = disk_read_sector(d, lba + i, buf);
            if (rc == 0)
                for (int b = 0; b < DISK_SECTOR_SIZE; b++)
                    mem_write8(pc, c->sreg[SREG_ES], (uint16_t)(bx + i * DISK_SECTOR_SIZE + b), buf[b]);
        }
        if (rc != 0) { c->eflags |= FLAG_CF; SET_AH(c, 0x0C); SET_AL(c, (uint8_t)i); return 1; }
    }
    c->eflags &= ~FLAG_CF;
    SET_AH(c, 0x00);
    SET_AL(c, (uint8_t)count);
    return 1;
}

static int int13(pc_t *pc)
{
    cpu386_t *c = &pc->cpu;
    switch (AH(c)) {
        case 0x00: c->eflags &= ~FLAG_CF; SET_AH(c, 0); return 1;
        case 0x01: SET_AH(c, 0); c->eflags &= ~FLAG_CF; return 1;
        case 0x02: return do_disk_xfer(pc, 0);
        case 0x03: return do_disk_xfer(pc, 1);
        case 0x08: {
            disk_t *d = disk_for_drive(pc, DL(c));
            if (!d || !d->present) { c->eflags |= FLAG_CF; SET_AH(c, 0x01); return 1; }
            SET_CH(c, (uint8_t)(d->cylinders & 0xFF));
            SET_CL(c, (uint8_t)(((d->cylinders >> 8) & 3) | (d->sectors & 0x3F)));
            SET_DH(c, (uint8_t)(d->heads - 1));
            SET_DL(c, d->is_hdd ? 0x01 : 0x01);
            c->eflags &= ~FLAG_CF; SET_AH(c, 0);
            return 1;
        }
        case 0x15: { /* get disk type */
            disk_t *d = disk_for_drive(pc, DL(c));
            if (!d || !d->present) { SET_AH(c, 0x00); c->eflags |= FLAG_CF; }
            else if (d->is_hdd) { SET_AH(c, 0x03); SET_CX(c, (uint16_t)(d->cylinders * d->heads * d->sectors)); c->eflags &= ~FLAG_CF; }
            else { SET_AH(c, 0x01); c->eflags &= ~FLAG_CF; }
            return 1;
        }
        default: c->eflags &= ~FLAG_CF; SET_AH(c, 0); return 1;
    }
}

/* ------------------------------------------------------------------ */
/* Keyboard (INT 16h)                                                  */
/* ------------------------------------------------------------------ */

static int int16(pc_t *pc)
{
    cpu386_t *c = &pc->cpu;
    uint8_t ascii, scan;
    int fn = AH(c);
    if (fn == 0x10 || fn == 0x00) {
        if (!bios_kbd_pop(pc, &ascii, &scan)) { SET_AX(c, 0); return 1; }
        SET_AL(c, ascii); SET_AH(c, scan);
        return 1;
    }
    if (fn == 0x11 || fn == 0x01) {
        if (!bios_kbd_peek(pc, &ascii, &scan)) { c->eflags |= FLAG_ZF; SET_AX(c, 0); }
        else { c->eflags &= ~FLAG_ZF; SET_AL(c, ascii); SET_AH(c, scan); }
        return 1;
    }
    if (fn == 0x12 || fn == 0x02) { SET_AL(c, pc->kbd.shift_flags); return 1; }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Time (INT 1Ah)                                                      */
/* ------------------------------------------------------------------ */

static int int1a(pc_t *pc)
{
    cpu386_t *c = &pc->cpu;
    uint8_t *r = pc->cmos.ram;
    switch (AH(c)) {
        case 0x00: { uint32_t t = mem_pread32(pc, BDA(0x6C));
            SET_CX(c, (uint16_t)(t >> 16)); SET_DX(c, (uint16_t)(t & 0xFFFF));
            SET_AL(c, 0); break; }
        case 0x01: mem_pwrite32(pc, BDA(0x6C), ((uint32_t)CX(c) << 16) | DX(c)); break;
        case 0x02: SET_CH(c, r[0x04]); SET_CL(c, r[0x02]); SET_DH(c, r[0x00]); SET_DL(c, 0); break;
        case 0x04: SET_CH(c, r[0x32]); SET_CL(c, r[0x09]); SET_DH(c, r[0x08]); SET_DL(c, r[0x07]); break;
        default: break;
    }
    c->eflags &= ~FLAG_CF;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Dispatch                                                            */
/* ------------------------------------------------------------------ */

int bios_handle_int(pc_t *pc, uint8_t vector)
{
    switch (vector) {
        case 0x08: {                                   /* timer tick */
            uint32_t t = mem_pread32(pc, BDA(0x6C));
            mem_pwrite32(pc, BDA(0x6C), t + 1);
            pc->pit.ticks++;
            eoi(pc, 0);
            if (vector_is_guest(pc, 0x1C)) chain_int(pc, 0x1C);
            return 1;
        }
        case 0x09: {                                   /* keyboard */
            int k = kbd_pop_key(pc);
            if (k >= 0) kbd_decode_scancode(pc, (uint8_t)k);
            eoi(pc, 1);
            return 1;
        }
        case 0x10: return int10(pc);
        case 0x11: SET_AX(&pc->cpu, 0x0021); return 1;  /* equipment */
        case 0x12: SET_AX(&pc->cpu, 640); return 1;     /* base memory KB */
        case 0x13: return int13(pc);
        case 0x16: return int16(pc);
        case 0x19: {                                    /* bootstrap */
            machine_boot(pc);
            if (pc->running) machine_set_return_frame(pc, 0x0000, 0x7C00);
            return 1;
        }
        case 0x1A: return int1a(pc);
        case 0x1B: case 0x1C: return 1;                 /* no-op, user hooks */
        case 0x1E: SET_SI(&pc->cpu, 0); return 1;       /* floppy params */
        default: return 0;
    }
}
