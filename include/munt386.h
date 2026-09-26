/*
 * Munt386 -- experimental replacement firmware for the TI-84 Plus CSE.
 *
 * Goal: a virtual 386-class PC (BIOS -> DOS -> Windows 3.1) implemented on top
 * of the calculator hardware, independent of TI-OS.
 *
 * This header is the contract shared by the portable host emulator (used for
 * development and automated testing) and the eventual Z80 firmware port.
 *
 * DESIGN NOTES
 * ------------
 *  * The virtual x86 address space is COMPLETELY independent of any host
 *    address space.  All guest memory access goes through the mem_* helpers.
 *  * The register file is 32-bit (EAX..EDI / EIP / EFLAGS) so the 286/386
 *    protected-mode phases can be layered on without rewriting register access.
 *    Phase 2 exercises the 16-bit real-mode subset only.
 *  * Nothing here knows about the CSE LCD: the video backend renders into an
 *    abstract framebuffer that the platform layer scales to 320x240.
 */
#ifndef MUNT386_H
#define MUNT386_H

#include <stdint.h>
#include <stddef.h>
#include "platform.h"   /* pacing + config shared by all backends */

/* ------------------------------------------------------------------ */
/* Sizes                                                               */
/* ------------------------------------------------------------------ */

#define X86_PHYS_MASK  0x000FFFFFu   /* 20-bit real-mode physical address   */
#define X86_MEM_SIZE   0x00100000u   /* 1 MiB virtual PC address space      */
#define X86_LOW_MEM    0x000A0000u   /* < 640 KiB conventional RAM          */
#define X86_VGA_BASE   0x000A0000u   /* VGA linear framebuffer (VBE)         */
#define X86_CGA_BASE   0x000B8000u   /* CGA color text/graphics              */
#define X86_MONO_BASE  0x000B0000u   /* MDA text                             */
#define X86_BIOS_BASE  0x000F0000u   /* BIOS ROM                             */
#define X86_BIOS_SIZE  0x00010000u

/* Guest RAM we actually back on the host.  The CSE cannot hold 1 MiB of RAM;
 * see docs/MEMORY_MAP.md for the banked/paged backing used on hardware. */
#define X86_BACKED_RAM X86_MEM_SIZE

/* Render targets. Native CGA is 640x200; the CSE LCD is 320x240. */
#define VGA_FB_MAX_W   640
#define VGA_FB_MAX_H   480
#define CSE_LCD_W      320
#define CSE_LCD_H      240

/* ------------------------------------------------------------------ */
/* EFLAGS bits (match the real x86 layout)                             */
/* ------------------------------------------------------------------ */

#define FLAG_CF  0x00000001u
#define FLAG_PF  0x00000004u
#define FLAG_AF  0x00000010u
#define FLAG_ZF  0x00000040u
#define FLAG_SF  0x00000080u
#define FLAG_TF  0x00000100u
#define FLAG_IF  0x00000200u
#define FLAG_DF  0x00000400u
#define FLAG_OF  0x00000800u
#define FLAG_IOPL 0x00003000u
#define FLAG_NT  0x00004000u
#define FLAG_RF  0x00010000u
#define FLAG_VM  0x00020000u
#define FLAG_AC  0x00040000u
#define FLAG_FIXED (0x00000002u | 0xFFF80000u)  /* bit1 always 1, resp. bits */

/* CR0 bits */
#define CR0_PE  0x00000001u   /* protection enable            */
#define CR0_MP  0x00000002u
#define CR0_EM  0x00000004u
#define CR0_TS  0x00000008u
#define CR0_ET  0x00000010u
#define CR0_NE  0x00000020u
#define CR0_WP  0x00010000u
#define CR0_AM  0x00040000u
#define CR0_NW  0x20000000u
#define CR0_CD  0x40000000u
#define CR0_PG  0x80000000u   /* paging enable                */

/* Exceptions */
#define EXC_DE  0
#define EXC_DB  1
#define EXC_NMI 2
#define EXC_BP  3
#define EXC_OF  4
#define EXC_BR  5
#define EXC_UD  6
#define EXC_NM  7
#define EXC_DF  8
#define EXC_TS 10
#define EXC_NP 11
#define EXC_SS 12
#define EXC_GP 13
#define EXC_PF 14

/* Descriptor / gate types */
#define DESC_LDT        0x02
#define DESC_TSS_AVAIL  0x09
#define DESC_TSS_BUSY   0x0B
#define DESC_CALL_GATE  0x0C
#define DESC_TASK_GATE  0x05
#define DESC_INT_GATE16 0x06
#define DESC_TRAP_GATE16 0x07
#define DESC_INT_GATE32 0x0E
#define DESC_TRAP_GATE32 0x0F

/* ------------------------------------------------------------------ */
/* Register indices -- MUST match ModR/M encoding order                */
/* ------------------------------------------------------------------ */

enum {
    REG_AX = 0, REG_CX, REG_DX, REG_BX,   /* EAX ECX EDX EBX */
    REG_SP,     REG_BP, REG_SI, REG_DI    /* ESP EBP ESI EDI */
};

enum { SREG_ES = 0, SREG_CS, SREG_SS, SREG_DS, SREG_FS, SREG_GS };

/* Segment override prefixes (decoded) */
enum { SEG_NONE = 0, SEG_ES, SEG_CS, SEG_SS, SEG_DS, SEG_FS, SEG_GS };

/* Operand size: 0 = 16-bit (real mode default), 1 = 32-bit */
enum { OPSZ_16 = 0, OPSZ_32 = 1 };

/* ------------------------------------------------------------------ */
/* CPU                                                                 */
/* ------------------------------------------------------------------ */

/* Cached translation of one segment register.  In real mode base = sel<<4
 * and limit = 0xFFFF; in protected mode it comes from the descriptor. */
typedef struct {
    uint32_t base;
    uint32_t limit;
    uint8_t  ar;        /* access-rights byte as stored in the descriptor  */
    uint8_t  flags;     /* granularity (G), default size (D/B)             */
    int      valid;
} seg_cache_t;

typedef struct {
    uint32_t r[8];       /* EAX ECX EDX EBX ESP EBP ESI EDI */
    uint16_t sreg[6];    /* ES CS SS DS FS GS               */
    seg_cache_t sc[6];   /* descriptor cache per segment    */
    uint32_t eip;
    uint32_t eflags;

    /* 286/386 system registers. */
    uint32_t cr0, cr2, cr3;
    uint32_t gdtr_base, idtr_base;
    uint16_t gdtr_limit, idtr_limit;
    uint16_t ldtr, tr;           /* selectors                              */
    seg_cache_t ldtr_cache, tr_cache;

    uint64_t insn_count;
    int      halted;
    int      fault;          /* set on illegal/unimplemented opcode */
    int      in_protected;   /* derived from CR0.PE; updated on changes */
    uint8_t  last_opcode;
    uint32_t fault_eip;
    char     fault_msg[64];
} cpu386_t;

/* ------------------------------------------------------------------ */
/* Virtual hardware                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    uint64_t ticks;          /* BIOS tick counter (18.2 Hz)   */
    uint32_t base_freq;      /* PIT input clock (1193182 Hz)  */
    uint16_t reload[3];
    double   frac;           /* host accumulation for IRQ0    */
} pit_t;

typedef struct {
    uint8_t  irr, imr, isr;
    uint8_t  vector_base;    /* ICW2; master defaults to 0x08 */
    int      in_service;
} pic_t;

#define KBD_QUEUE_SIZE 32
typedef struct {
    uint8_t  queue[KBD_QUEUE_SIZE];
    unsigned head, tail;
    uint8_t  shift_flags;    /* BIOS INT 16h AH=02 state      */
    uint8_t  last_scancode;
    uint8_t  led_status;
} kbd_t;

/* CMOS / RTC registers used by BIOS and by Windows setup. */
typedef struct {
    uint8_t  ram[128];
} cmos_t;

typedef struct {
    uint16_t width, height;  /* current video mode geometry   */
    uint16_t mode;           /* BIOS mode number              */
    uint32_t base_phys;
    int      cols, rows;
    int      graphics;
    uint8_t  page;
    uint8_t  cursor_row, cursor_col;
    uint8_t  cursor_start, cursor_end;
    int      enabled_cursor;
    uint8_t  attr;           /* current draw attribute        */
    uint16_t vga_mem_size;   /* VBE linear framebuffer size   */
    uint8_t  *fb;            /* host framebuffer, RGB888       */
    uint32_t fb_w, fb_h;     /* framebuffer dimensions        */
    uint8_t  dirty;
} vga_t;

#define DISK_SECTOR_SIZE 512

typedef struct {
    int      present;
    int      readonly;
    int      is_hdd;
    uint16_t cylinders;
    uint8_t  heads;
    uint8_t  sectors;
    uint8_t  *image;
    uint32_t image_size;
} disk_t;

typedef struct pc {
    cpu386_t cpu;
    uint8_t  *mem;
    uint32_t mem_size;   /* guest RAM size; >= 1 MiB. May exceed 1 MiB for
                          * protected-mode/paging tests. */
    pace_t   pace;       /* IRQ0 pacing (replaces a function static) */

    pit_t    pit;
    pic_t    pic;
    kbd_t    kbd;
    cmos_t   cmos;
    vga_t    vga;
    disk_t   disk[4];        /* 0x00/0x01 floppy, 0x80/0x81 HDD */

    int      running;
    int      exit_code;
    int      int_pending;    /* software-injected interrupt, -1 none */

    /* I/O port backends */
    uint8_t  port60;         /* last keyboard data byte */
    uint8_t  port64;         /* keyboard status */
    uint32_t port92;         /* A20 / fast reset */
    int      speaker_div;
    int      trace_enabled;
} pc_t;

/* ------------------------------------------------------------------ */
/* Memory subsystem (independent of the host address space)            */
/* ------------------------------------------------------------------ */

uint32_t phys20(uint16_t seg, uint16_t off);

/* Linear-address accessors.  In real mode linear == physical.  When CR0.PG is
 * set the address is translated through CR3 page tables (see src/protected.c). */
uint8_t  mem_lread8 (pc_t *pc, uint32_t lin);
uint16_t mem_lread16(pc_t *pc, uint32_t lin);
uint32_t mem_lread32(pc_t *pc, uint32_t lin);
void     mem_lwrite8 (pc_t *pc, uint32_t lin, uint8_t v);
void     mem_lwrite16(pc_t *pc, uint32_t lin, uint16_t v);
void     mem_lwrite32(pc_t *pc, uint32_t lin, uint32_t v);

uint8_t  mem_read8 (pc_t *pc, uint16_t seg, uint16_t off);
uint16_t mem_read16(pc_t *pc, uint16_t seg, uint16_t off);
uint32_t mem_read32(pc_t *pc, uint16_t seg, uint16_t off);
void     mem_write8 (pc_t *pc, uint16_t seg, uint16_t off, uint8_t v);
void     mem_write16(pc_t *pc, uint16_t seg, uint16_t off, uint16_t v);
void     mem_write32(pc_t *pc, uint16_t seg, uint16_t off, uint32_t v);

uint8_t  mem_pread8 (pc_t *pc, uint32_t phys);
uint16_t mem_pread16(pc_t *pc, uint32_t phys);
uint32_t mem_pread32(pc_t *pc, uint32_t phys);
void     mem_pwrite8 (pc_t *pc, uint32_t phys, uint8_t v);
void     mem_pwrite16(pc_t *pc, uint32_t phys, uint16_t v);
void     mem_pwrite32(pc_t *pc, uint32_t phys, uint32_t v);

/* ------------------------------------------------------------------ */
/* CPU core                                                            */
/* ------------------------------------------------------------------ */

void cpu_reset(cpu386_t *cpu);
/* Execute exactly one instruction. 0 = ok, non-zero = fault. */
int  cpu_step(pc_t *pc);
/* Inject an interrupt (used by the PIC for hardware IRQs). */
void cpu_interrupt(pc_t *pc, uint8_t vector);
uint64_t cpu_run(pc_t *pc, uint64_t max_steps);

/* --- Protected mode (src/protected.c) -------------------------------- */
/* Load a segment register from a selector honouring the current mode.
 * Returns 0 on success, or the exception vector on failure (#GP/#SS/#NP). */
int  seg_load(pc_t *pc, int sreg, uint16_t selector);
/* Recompute segment caches after a CR0.PE transition. */
void cpu_update_mode(pc_t *pc);
/* Deliver an interrupt/exception through IDT (protected) or IVT (real). */
void cpu_deliver_interrupt(pc_t *pc, uint8_t vector, int is_exception,
                           uint32_t error_code);
/* Raise an exception (page faults record CR2). */
void cpu_raise_exception(pc_t *pc, uint8_t vector, uint32_t error_code);
/* Parse one 8-byte descriptor. Returns 0 if usable. */
int  desc_parse(uint8_t *d, seg_cache_t *out);
/* Linear -> physical through the paging hierarchy; returns 0xFFFFFFFF on fault
 * and fills *err_out with the #PF error code. */
uint32_t paging_translate(pc_t *pc, uint32_t lin, int write, int user,
                          uint32_t *err_out);

/* 16-bit register views */
static inline uint16_t AX(cpu386_t *c) { return (uint16_t)c->r[REG_AX]; }
static inline uint16_t CX(cpu386_t *c) { return (uint16_t)c->r[REG_CX]; }
static inline uint16_t DX(cpu386_t *c) { return (uint16_t)c->r[REG_DX]; }
static inline uint16_t BX(cpu386_t *c) { return (uint16_t)c->r[REG_BX]; }
static inline uint16_t SP(cpu386_t *c) { return (uint16_t)c->r[REG_SP]; }
static inline uint16_t BP(cpu386_t *c) { return (uint16_t)c->r[REG_BP]; }
static inline uint16_t SI(cpu386_t *c) { return (uint16_t)c->r[REG_SI]; }
static inline uint16_t DI(cpu386_t *c) { return (uint16_t)c->r[REG_DI]; }

static inline void SET_AX(cpu386_t *c, uint16_t v) { c->r[REG_AX] = v; }
static inline void SET_CX(cpu386_t *c, uint16_t v) { c->r[REG_CX] = v; }
static inline void SET_DX(cpu386_t *c, uint16_t v) { c->r[REG_DX] = v; }
static inline void SET_BX(cpu386_t *c, uint16_t v) { c->r[REG_BX] = v; }
static inline void SET_SP(cpu386_t *c, uint16_t v) { c->r[REG_SP] = v; }
static inline void SET_BP(cpu386_t *c, uint16_t v) { c->r[REG_BP] = v; }
static inline void SET_SI(cpu386_t *c, uint16_t v) { c->r[REG_SI] = v; }
static inline void SET_DI(cpu386_t *c, uint16_t v) { c->r[REG_DI] = v; }

static inline uint8_t  AL(cpu386_t *c) { return (uint8_t)c->r[REG_AX]; }
static inline uint8_t  CL(cpu386_t *c) { return (uint8_t)c->r[REG_CX]; }
static inline uint8_t  DL(cpu386_t *c) { return (uint8_t)c->r[REG_DX]; }
static inline uint8_t  BL(cpu386_t *c) { return (uint8_t)c->r[REG_BX]; }
static inline uint8_t  AH(cpu386_t *c) { return (uint8_t)(c->r[REG_AX] >> 8); }
static inline uint8_t  CH(cpu386_t *c) { return (uint8_t)(c->r[REG_CX] >> 8); }
static inline uint8_t  DH(cpu386_t *c) { return (uint8_t)(c->r[REG_DX] >> 8); }
static inline uint8_t  BH(cpu386_t *c) { return (uint8_t)(c->r[REG_BX] >> 8); }

static inline void SET_AL(cpu386_t *c, uint8_t v) { c->r[REG_AX] = (c->r[REG_AX] & 0xff00u) | v; }
static inline void SET_CL(cpu386_t *c, uint8_t v) { c->r[REG_CX] = (c->r[REG_CX] & 0xff00u) | v; }
static inline void SET_DL(cpu386_t *c, uint8_t v) { c->r[REG_DX] = (c->r[REG_DX] & 0xff00u) | v; }
static inline void SET_BL(cpu386_t *c, uint8_t v) { c->r[REG_BX] = (c->r[REG_BX] & 0xff00u) | v; }
static inline void SET_AH(cpu386_t *c, uint8_t v) { c->r[REG_AX] = (c->r[REG_AX] & 0x00ffu) | ((uint32_t)v << 8); }
static inline void SET_CH(cpu386_t *c, uint8_t v) { c->r[REG_CX] = (c->r[REG_CX] & 0x00ffu) | ((uint32_t)v << 8); }
static inline void SET_DH(cpu386_t *c, uint8_t v) { c->r[REG_DX] = (c->r[REG_DX] & 0x00ffu) | ((uint32_t)v << 8); }
static inline void SET_BH(cpu386_t *c, uint8_t v) { c->r[REG_BX] = (c->r[REG_BX] & 0x00ffu) | ((uint32_t)v << 8); }

/* Generic operand access by ModR/M register index, 8- or 16-bit. */
uint16_t cpu_get_r16(cpu386_t *c, int idx);
void     cpu_set_r16(cpu386_t *c, int idx, uint16_t v);
uint8_t  cpu_get_r8 (cpu386_t *c, int idx);
void     cpu_set_r8 (cpu386_t *c, int idx, uint8_t v);

/* Stack */
void     cpu_push16(pc_t *pc, uint16_t v);
uint16_t cpu_pop16(pc_t *pc);

/* Flag helpers (exposed for tests) */
int  parity_even(uint8_t v);
void set_flags_add8 (cpu386_t *c, uint8_t a, uint8_t b, uint8_t r);
void set_flags_add16(cpu386_t *c, uint16_t a, uint16_t b, uint16_t r);
void set_flags_sub8 (cpu386_t *c, uint8_t a, uint8_t b, uint8_t r);
void set_flags_sub16(cpu386_t *c, uint16_t a, uint16_t b, uint16_t r);
void set_flags_logic8 (cpu386_t *c, uint8_t r);
void set_flags_logic16(cpu386_t *c, uint16_t r);
void set_flags_inc8 (cpu386_t *c, uint8_t a, uint8_t r);
void set_flags_dec8 (cpu386_t *c, uint8_t a, uint8_t r);
void set_flags_inc16(cpu386_t *c, uint16_t a, uint16_t r);
void set_flags_dec16(cpu386_t *c, uint16_t a, uint16_t r);

/* ------------------------------------------------------------------ */
/* Machine / devices                                                   */
/* ------------------------------------------------------------------ */

void machine_init(pc_t *pc);
void machine_reset(pc_t *pc);
void machine_free(pc_t *pc);
void machine_boot(pc_t *pc);
/* Patch the pending interrupt-return frame (used by INT 19h and DOS exit). */
void machine_set_return_frame(pc_t *pc, uint16_t cs, uint16_t ip);

#define IRQ_TIMER    0
#define IRQ_KEYBOARD 1
#define IRQ_CASCADE  2
#define IRQ_COM2     3
#define IRQ_COM1     4
#define IRQ_LPT2     5
#define IRQ_FLOPPY   6
#define IRQ_LPT1     7

void pic_raise(pc_t *pc, int irq);
void pic_lower(pc_t *pc, int irq);
int  pic_deliver(pc_t *pc);

void pit_advance(pc_t *pc);
void machine_timer_tick(pc_t *pc);

/* Keyboard */
void kbd_push_scancode(pc_t *pc, uint8_t sc);
int  kbd_has_key(pc_t *pc);
int  kbd_pop_key(pc_t *pc);

/* I/O port access (0..65535). Return 0 if unhandled. */
int  io_port_in (pc_t *pc, uint16_t port, int size, uint32_t *value);
int  io_port_out(pc_t *pc, uint16_t port, int size, uint32_t value);

/* BIOS / DOS service dispatch */
int  bios_handle_int(pc_t *pc, uint8_t vector);
int  dos_handle_int(pc_t *pc, uint8_t vector);
/* Decode a keyboard scancode into the BIOS ring buffer (used by INT 09h). */
void kbd_decode_scancode(pc_t *pc, uint8_t sc);
/* Write one character in teletype fashion (used by DOS console output). */
void bios_teletype(pc_t *pc, uint8_t ch);

/* ------------------------------------------------------------------ */
/* Video                                                               */
/* ------------------------------------------------------------------ */

void vga_init(pc_t *pc, uint32_t fb_w, uint32_t fb_h);
void vga_reset(vga_t *v);
void vga_set_mode(pc_t *pc, uint8_t mode);
void vga_render(pc_t *pc);
/* Bulk-convert one RGB888 row to RGB565, sampling every step-th pixel. */
void vga_rgb_row_to_rgb565(const uint8_t *row, uint32_t src_w,
                           uint16_t *dst, uint32_t dst_w, uint32_t step);
/* Downscale the virtual framebuffer to the CSE 320x240 RGB565 LCD buffer. */
void vga_to_cse_lcd(const vga_t *v, uint16_t *lcd_rgb565);

/* ------------------------------------------------------------------ */
/* Disk                                                                */
/* ------------------------------------------------------------------ */

int  disk_attach(disk_t *d, uint8_t *image, uint32_t size, int readonly, int is_hdd);
void disk_detach(disk_t *d);
int  disk_get_geometry(disk_t *d, uint16_t *cylinders, uint8_t *heads, uint8_t *sectors);
int  disk_read_sector (disk_t *d, uint32_t lba, uint8_t *buf);
int  disk_write_sector(disk_t *d, uint32_t lba, const uint8_t *buf);

/* ------------------------------------------------------------------ */
/* Fonts                                                               */
/* ------------------------------------------------------------------ */

/* 8x8 bitmap font for code points 0x20..0x7E (95 glyphs, MSB-first). */
extern const uint8_t munt386_font8x8[95][8];

#endif /* MUNT386_H */
