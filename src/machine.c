/*
 * Munt386 -- virtual IBM-PC-compatible chipset.
 *
 * Implements the minimum hardware DOS/Windows needs: 8259 PIC, 8254 PIT,
 * 8042-style keyboard controller, CMOS/RTC, PC speaker latch, and the boot
 * path (reset vector -> INT 19h -> INT 13h read of sector 0 -> 0000:7C00).
 *
 * All of this is host-portable.  The CSE backend only has to provide the
 * memory backing and a display; the chipset behaviour is identical.
 */
#include "munt386.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>

#define PIT_CALLS_PER_TICK 4   /* IRQ0 every N pit_advance calls */
#define BIOS_DATA_SEG 0x0040
#define BIOS_KBD_HEAD 0x001A
#define BIOS_KBD_TAIL 0x001C
#define BIOS_KBD_BUF  0x001E
#define BIOS_KBD_END  0x003E
#define BIOS_TIMER    0x006C
#define BIOS_EQUIP    0x0010
#define BIOS_MEMSIZE  0x0013
#define BIOS_HDDCOUNT 0x0075

/* Scancode-set-1 to ASCII translation tables (index 0..127). */
static const char sc_ascii[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', 8, 9,
    'q','w','e','r','t','y','u','i','o','p','[',']', 13, 0, 'a','s',
    'd','f','g','h','j','k','l',';','\'','`', 0,'\\','z','x','c','v',
    'b','n','m',',','.','/', 0,'*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};
static const char sc_ascii_shift[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', 8, 9,
    'Q','W','E','R','T','Y','U','I','O','P','{','}', 13, 0, 'A','S',
    'D','F','G','H','J','K','L',':','"','~', 0,'|','Z','X','C','V',
    'B','N','M','<','>','?', 0,'*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

static void machine_install_ivt(pc_t *pc)
{
    for (int n = 0; n < 256; n++) {
        uint32_t vec = (uint32_t)n * 4;
        uint16_t off = (uint16_t)(0x1000 + n * 4);
        mem_pwrite16(pc, vec, off);
        mem_pwrite16(pc, vec + 2, 0xF000);
        /* The stub byte itself (0xF1 marks an emulator-owned BIOS entry). */
        mem_pwrite8(pc, 0xF0000u + off, 0xF1);
    }
}

static void machine_cmos_defaults(pc_t *pc)
{
    uint8_t *r = pc->cmos.ram;
    memset(r, 0, sizeof(pc->cmos.ram));
    r[0x00] = 0x00; r[0x01] = 0x00;   /* seconds   */
    r[0x02] = 0x30; r[0x03] = 0x00;   /* minutes   */
    r[0x04] = 0x12; r[0x05] = 0x00;   /* hours, 12h */
    r[0x06] = 0x03;                   /* weekday   */
    r[0x07] = 0x15;                   /* day       */
    r[0x08] = 0x06;                   /* month     */
    r[0x09] = 0x26;                   /* year      */
    r[0x0A] = 0x26;                   /* status A  */
    r[0x0B] = 0x02;                   /* status B  */
    r[0x0E] = 0x00;                   /* diagnostic */
    r[0x0F] = 0x00;                   /* shutdown   */
    r[0x10] = 0x40;                   /* floppy types */
    r[0x12] = 0x00;                   /* hdd types    */
    r[0x14] = 0x2D;                   /* equipment    */
    r[0x15] = 0x80; r[0x16] = 0x02;   /* base mem 640K */
    r[0x17] = 0x00; r[0x18] = 0x00;   /* ext mem      */
    r[0x30] = 0x00; r[0x31] = 0x00;
    r[0x32] = 0x20;                   /* century      */
}

void machine_init(pc_t *pc)
{
    memset(pc, 0, sizeof(*pc));
    pc->mem = mem_backing_alloc(X86_MEM_SIZE);
    pc->int_pending = -1;
    pc->running = 1;
    pc->pic.vector_base = 0x08;
    pc->pit.base_freq = 1193182;
    pc->pit.reload[1] = 18;
    pc->port92 = 0;
    pace_init(&pc->pace, PIT_CALLS_PER_TICK);
    machine_cmos_defaults(pc);
    vga_init(pc, VGA_FB_MAX_W, VGA_FB_MAX_H);
    machine_reset(pc);
}

void machine_free(pc_t *pc)
{
    if (pc->vga.fb) {
        platform_memory_free(pc->vga.fb, (size_t)pc->vga.fb_w * pc->vga.fb_h * 3);
        pc->vga.fb = NULL;
    }
    if (pc->mem) { mem_backing_free(pc->mem, X86_MEM_SIZE); pc->mem = NULL; }
}

#if defined(MUNT386_CSE) || defined(MUNT386_CSE_SIM)
/* Paged guest-memory backend (firmware/cse/cse_mem.c): machine_reset uses it
 * instead of memset() over the flat 1 MiB array. */
void mem_clear_all(pc_t *pc);
#endif

void machine_reset(pc_t *pc)
{
#if defined(MUNT386_CSE) || defined(MUNT386_CSE_SIM)
    mem_clear_all(pc);          /* paged backend clears guest pages */
#else
    memset(pc->mem, 0, X86_MEM_SIZE);
#endif

    cpu_reset(&pc->cpu);
    machine_install_ivt(pc);

    /* BIOS data area. */
    mem_pwrite16(pc, BIOS_DATA_SEG * 16 + BIOS_EQUIP, 0x0021);      /* 1 floppy, 80x25 */
    mem_pwrite16(pc, BIOS_DATA_SEG * 16 + BIOS_MEMSIZE, 640);
    mem_pwrite16(pc, BIOS_DATA_SEG * 16 + BIOS_KBD_HEAD, BIOS_KBD_BUF);
    mem_pwrite16(pc, BIOS_DATA_SEG * 16 + BIOS_KBD_TAIL, BIOS_KBD_BUF);
    mem_pwrite8 (pc, BIOS_DATA_SEG * 16 + BIOS_HDDCOUNT, pc->disk[2].present ? 1 : 0);
    mem_pwrite32(pc, BIOS_DATA_SEG * 16 + BIOS_TIMER, 0);

    /* BIOS date string at FFFF:0005 (kept for authenticity). */
    const char *date = "06/15/26";
    for (int i = 0; i < 8; i++) mem_pwrite8(pc, 0xFFFF5u + i, (uint8_t)date[i]);
    mem_pwrite8(pc, 0xFFFFEu, 0x00);
    mem_pwrite8(pc, 0xFFFFFu, 0x00);

    /* Reset vector at F000:FFF0: INT 19h (bootstrap). */
    mem_pwrite8(pc, 0xFFFF0u, 0xCD);
    mem_pwrite8(pc, 0xFFFF1u, 0x19);

    pc->pic.irr = 0; pc->pic.isr = 0; pc->pic.imr = 0xFF;
    pc->pit.ticks = 0; pc->pit.frac = 0;
    pc->kbd.head = pc->kbd.tail = 0;
    pc->running = 1;
    pc->exit_code = 0;

    vga_reset(&pc->vga);

    pc->cpu.sreg[SREG_CS] = 0xF000;
    pc->cpu.eip = 0xFFF0;
    pc->cpu.eflags |= FLAG_IF;
}

/* ------------------------------------------------------------------ */
/* PIC                                                                 */
/* ------------------------------------------------------------------ */

void pic_raise(pc_t *pc, int irq) { pc->pic.irr |= (uint8_t)(1u << irq); }
void pic_lower(pc_t *pc, int irq) { pc->pic.irr &= (uint8_t)~(1u << irq); }

int pic_deliver(pc_t *pc)
{
    if (!(pc->cpu.eflags & FLAG_IF)) return 0;
    uint8_t pending = pc->pic.irr & (uint8_t)~pc->pic.imr;
    if (!pending) return 0;
    int irq = 0;
    while (!(pending & (1u << irq))) irq++;
    pc->pic.irr &= (uint8_t)~(1u << irq);
    pc->pic.isr |= (uint8_t)(1u << irq);
    cpu_interrupt(pc, (uint8_t)(pc->pic.vector_base + irq));
    return 1;
}

/* ------------------------------------------------------------------ */
/* PIT                                                                 */
/* ------------------------------------------------------------------ */

void machine_timer_tick(pc_t *pc)
{
    pic_raise(pc, IRQ_TIMER);
}

void pit_advance(pc_t *pc)
{
    if (pace_should_tick(&pc->pace))
        machine_timer_tick(pc);
}

/* ------------------------------------------------------------------ */
/* Keyboard                                                            */
/* ------------------------------------------------------------------ */

void kbd_push_scancode(pc_t *pc, uint8_t sc)
{
    unsigned next = (pc->kbd.tail + 1) % KBD_QUEUE_SIZE;
    if (next == pc->kbd.head) return;   /* full */
    pc->kbd.queue[pc->kbd.tail] = sc;
    pc->kbd.tail = next;
    pc->port60 = sc;
    pc->port64 = 0x01;                  /* output buffer full */
    pic_raise(pc, IRQ_KEYBOARD);
}

int kbd_has_key(pc_t *pc) { return pc->kbd.head != pc->kbd.tail; }

int kbd_pop_key(pc_t *pc)
{
    if (pc->kbd.head == pc->kbd.tail) return -1;
    int v = pc->kbd.queue[pc->kbd.head];
    pc->kbd.head = (pc->kbd.head + 1) % KBD_QUEUE_SIZE;
    return v;
}

/* Push a decoded key into the BIOS keyboard ring buffer. */
static void bios_kbd_push(pc_t *pc, uint8_t scan, uint8_t ascii)
{
    uint16_t head = mem_pread16(pc, BIOS_DATA_SEG * 16 + BIOS_KBD_HEAD);
    uint16_t tail = mem_pread16(pc, BIOS_DATA_SEG * 16 + BIOS_KBD_TAIL);
    if (tail < BIOS_KBD_BUF) tail = BIOS_KBD_BUF;
    if (head < BIOS_KBD_BUF || head >= BIOS_KBD_END) head = BIOS_KBD_BUF;
    uint16_t next = (uint16_t)(tail + 2);
    if (next >= BIOS_KBD_END) next = BIOS_KBD_BUF;
    if (next == head) return;           /* buffer full */
    mem_pwrite8(pc, BIOS_DATA_SEG * 16 + tail, ascii);
    mem_pwrite8(pc, BIOS_DATA_SEG * 16 + tail + 1, scan);
    mem_pwrite16(pc, BIOS_DATA_SEG * 16 + BIOS_KBD_TAIL, next);
}

/* Decode one scancode from the controller queue into the BIOS buffer. */
void kbd_decode_scancode(pc_t *pc, uint8_t sc)
{
    cpu386_t *c = &pc->cpu;
    if (sc == 0xE0) { (void)c; return; }
    if (sc & 0x80) {                    /* key release */
        uint8_t mk = sc & 0x7F;
        if (mk == 0x2A) pc->kbd.shift_flags &= ~0x03;
        if (mk == 0x36) pc->kbd.shift_flags &= ~0x03;
        if (mk == 0x1D) pc->kbd.shift_flags &= ~0x04;
        if (mk == 0x38) pc->kbd.shift_flags &= ~0x08;
        return;
    }
    if (sc == 0x2A || sc == 0x36) { pc->kbd.shift_flags |= 0x03; return; }  /* shift */
    if (sc == 0x1D) { pc->kbd.shift_flags |= 0x04; return; }                /* ctrl  */
    if (sc == 0x38) { pc->kbd.shift_flags |= 0x08; return; }                /* alt   */
    pc->kbd.last_scancode = sc;
    char a = ((pc->kbd.shift_flags & 0x03) ? sc_ascii_shift : sc_ascii)[sc & 0x7F];
    if (pc->kbd.shift_flags & 0x04) {
        if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 1);       /* Ctrl+letter */
    }
    bios_kbd_push(pc, sc, (uint8_t)a);
    mem_pwrite8(pc, BIOS_DATA_SEG * 16 + 0x17, pc->kbd.shift_flags);
    mem_pwrite8(pc, BIOS_DATA_SEG * 16 + 0x18, pc->kbd.shift_flags);
}

/* ------------------------------------------------------------------ */
/* Boot                                                                */
/* ------------------------------------------------------------------ */

void machine_boot(pc_t *pc)
{
    cpu386_t *c = &pc->cpu;
    uint8_t mbr[DISK_SECTOR_SIZE];

    if (!pc->disk[0].present || disk_read_sector(&pc->disk[0], 0, mbr) != 0) {
        pc->running = 0;
        return;
    }

    for (int i = 0; i < DISK_SECTOR_SIZE; i++)
        mem_pwrite8(pc, 0x7C00u + (uint32_t)i, mbr[i]);

    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        pc->running = 0;
        return;
    }

    /* NOTE: SS:SP are intentionally left untouched.  The interrupt return
     * frame that got us here lives on the current stack, and the bootstrap
     * loader must not move SP before that frame is popped.  Boot sectors set
     * up their own stack, exactly as they do on real hardware. */
    c->sreg[SREG_CS] = 0x0000;
    c->eip = 0x7C00;
    c->sreg[SREG_DS] = 0x0000;
    c->sreg[SREG_ES] = 0x0000;
    SET_DL(c, 0x00);        /* boot drive */
}

/* Patch the interrupt return frame so IRET resumes at cs:ip. */
void machine_set_return_frame(pc_t *pc, uint16_t cs, uint16_t ip)
{
    cpu386_t *c = &pc->cpu;
    uint16_t sp = (uint16_t)c->r[REG_SP];
    mem_write16(pc, c->sreg[SREG_SS], sp, ip);
    mem_write16(pc, c->sreg[SREG_SS], (uint16_t)(sp + 2), cs);
}

/* ------------------------------------------------------------------ */
/* I/O ports                                                           */
/* ------------------------------------------------------------------ */

int io_port_in(pc_t *pc, uint16_t port, int size, uint32_t *value)
{
    (void)size;
    *value = 0;
    switch (port) {
        case 0x20: *value = pc->pic.irr; return 1;
        case 0x21: *value = pc->pic.imr; return 1;
        case 0x40: case 0x41: case 0x42: *value = 0xFFFF; return 1;
        case 0x43: *value = 0; return 1;
        case 0x60: {
            int k = kbd_pop_key(pc);
            if (k < 0) k = pc->port60;
            *value = (uint32_t)k;
            if (!kbd_has_key(pc)) pc->port64 &= (uint8_t)~0x01u;
            return 1;
        }
        case 0x61: *value = 0; return 1;
        case 0x64: *value = pc->port64; return 1;
        case 0x70: *value = 0; return 1;
        case 0x71: *value = pc->cmos.ram[0x0D]; return 1;
        case 0x80: case 0x81: case 0x82: case 0x83:
        case 0x84: case 0x85: case 0x86: case 0x87:
        case 0x88: case 0x89: case 0x8A: case 0x8B:
        case 0x8C: case 0x8D: case 0x8E: case 0x8F: *value = 0xFF; return 1; /* POST */
        case 0x92: *value = pc->port92; return 1;
        case 0x3DA: {
            static int flip = 0;
            flip ^= 1;
            *value = flip ? 0x09 : 0x00;   /* retrace bits toggle */
            return 1;
        }
        case 0x3D4: case 0x3D5: case 0x3D8: case 0x3D9:
        case 0x3C0: case 0x3C1: *value = 0; return 1;
        case 0x3F8: case 0x3F9: case 0x3FA: case 0x3FB:
        case 0x3FC: case 0x3FD: case 0x3FE: case 0x3FF: *value = 0; return 1;
        case 0x1F0: case 0x1F1: case 0x1F2: case 0x1F3:
        case 0x1F4: case 0x1F5: case 0x1F6: case 0x1F7: *value = 0; return 1;
        default: return 0;
    }
}

int io_port_out(pc_t *pc, uint16_t port, int size, uint32_t value)
{
    (void)size;
    switch (port) {
        case 0x20:
            if ((value & 0x10) == 0) {
                if (value == 0x20) {           /* non-specific EOI */
                    for (int i = 7; i >= 0; i--) {
                        if (pc->pic.isr & (1u << i)) { pc->pic.isr &= (uint8_t)~(1u << i); break; }
                    }
                }
            }
            return 1;
        case 0x21:
            if (pc->pic.vector_base == 0) pc->pic.vector_base = (uint8_t)value;
            else pc->pic.imr = (uint8_t)value;
            return 1;
        case 0x40: case 0x41: case 0x42: return 1;
        case 0x43: return 1;
        case 0x60: pc->port60 = (uint8_t)value; return 1;
        case 0x64: pc->port64 = (uint8_t)value; return 1;
        case 0x61: pc->speaker_div = (int)(value & 3); return 1;
        case 0x70: return 1;
        case 0x71: return 1;
        case 0x92: pc->port92 = value; return 1;
        case 0x3D4: case 0x3D5: case 0x3D8: case 0x3D9:
        case 0x3C0: case 0x3C1: return 1;
        case 0x3F8: case 0x3F9: case 0x3FA: case 0x3FB:
        case 0x3FC: case 0x3FD: case 0x3FE: case 0x3FF: return 1;
        default: return 0;
    }
}
