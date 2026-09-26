/* Munt386 -- BIOS compatibility tests. */
#include "../test_util.h"

void test_bios(void);

void test_bios(void)
{
    /* INT 10h AH=00h set video mode */
    {
        pc_t *pc = mkpc();
        pc->cpu.r[REG_AX] = 0x0003;
        CHECK(bios_handle_int(pc, 0x10));
        CHECK_EQ(pc->vga.mode, 3);
        CHECK_EQ(pc->vga.cols, 80);
        freepc(pc);
    }
    /* INT 10h AH=0Eh teletype output writes to video memory and advances cursor */
    {
        pc_t *pc = mkpc();
        pc->cpu.r[REG_AX] = 0x0E41;              /* AH=0Eh, AL='A' */
        bios_handle_int(pc, 0x10);
        CHECK_EQ(mem_pread8(pc, X86_CGA_BASE + 0), 'A');
        CHECK_EQ(mem_pread8(pc, X86_CGA_BASE + 1), 0x07);
        CHECK_EQ(pc->vga.cursor_col, 1);
        freepc(pc);
    }
    /* INT 10h AH=0Eh handles CR/LF */
    {
        pc_t *pc = mkpc();
        pc->cpu.r[REG_AX] = 0x0E0D;              /* CR */
        bios_handle_int(pc, 0x10);
        CHECK_EQ(pc->vga.cursor_col, 0);
        pc->cpu.r[REG_AX] = 0x0E0A;              /* LF */
        bios_handle_int(pc, 0x10);
        CHECK_EQ(pc->vga.cursor_row, 1);
        freepc(pc);
    }
    /* INT 10h AH=03h get cursor, AH=02h set cursor */
    {
        pc_t *pc = mkpc();
        pc->cpu.r[REG_DX] = 0x0514;              /* DH=5, DL=20 */
        pc->cpu.r[REG_AX] = 0x0200;
        bios_handle_int(pc, 0x10);
        CHECK_EQ(pc->vga.cursor_row, 5);
        CHECK_EQ(pc->vga.cursor_col, 20);
        pc->cpu.r[REG_AX] = 0x0300;
        pc->cpu.r[REG_DX] = 0;
        bios_handle_int(pc, 0x10);
        CHECK_EQ(DX(&pc->cpu), 0x0514);
        freepc(pc);
    }
    /* INT 10h AH=0Fh get video state */
    {
        pc_t *pc = mkpc();
        pc->cpu.r[REG_AX] = 0x0F00;
        bios_handle_int(pc, 0x10);
        CHECK_EQ(AL(&pc->cpu), 3);
        CHECK_EQ(AH(&pc->cpu), 80);
        freepc(pc);
    }
    /* INT 12h base memory */
    {
        pc_t *pc = mkpc();
        pc->cpu.r[REG_AX] = 0;
        bios_handle_int(pc, 0x12);
        CHECK_EQ(AX(&pc->cpu), 640);
        freepc(pc);
    }
    /* INT 11h equipment */
    {
        pc_t *pc = mkpc();
        bios_handle_int(pc, 0x11);
        CHECK(AX(&pc->cpu) & 0x0001);
        freepc(pc);
    }
    /* INT 13h AH=02h read sector */
    {
        pc_t *pc = mkpc();
        uint8_t *img = (uint8_t *)calloc(1, 18 * 512);
        img[0] = 0x5A; img[512 * 3] = 0xC3;
        CHECK_EQ(disk_attach(&pc->disk[0], img, 18 * 512, 0, 0), 0);
        pc->cpu.sreg[SREG_ES] = 0x2000;
        pc->cpu.r[REG_BX] = 0x0000;
        pc->cpu.r[REG_AX] = 0x0201;              /* read, AL=1 sector */
        pc->cpu.r[REG_CX] = 0x0001;              /* cyl 0, sector 1 */
        pc->cpu.r[REG_DX] = 0x0000;              /* head 0, drive 0 */
        bios_handle_int(pc, 0x13);
        CHECK(!(pc->cpu.eflags & FLAG_CF));
        CHECK_EQ(mem_read8(pc, 0x2000, 0x0000), 0x5A);
        /* read sector 4 */
        pc->cpu.r[REG_AX] = 0x0201;
        pc->cpu.r[REG_CX] = 0x0004;
        bios_handle_int(pc, 0x13);
        CHECK(!(pc->cpu.eflags & FLAG_CF));
        CHECK_EQ(mem_read8(pc, 0x2000, 0x0000), 0xC3);
        free(img);
        freepc(pc);
    }
    /* INT 16h reads a decoded key */
    {
        pc_t *pc = mkpc();
        kbd_push_scancode(pc, 0x1E);             /* 'a' make code */
        bios_handle_int(pc, 0x09);               /* keyboard IRQ decodes it */
        pc->cpu.r[REG_AX] = 0x0000;
        bios_handle_int(pc, 0x16);
        CHECK_EQ(AL(&pc->cpu), 'a');
        CHECK_EQ(AH(&pc->cpu), 0x1E);
        freepc(pc);
    }
    /* INT 16h AH=01h reports no key with ZF set */
    {
        pc_t *pc = mkpc();
        pc->cpu.r[REG_AX] = 0x0100;
        bios_handle_int(pc, 0x16);
        CHECK(pc->cpu.eflags & FLAG_ZF);
        freepc(pc);
    }
    /* INT 1Ah tick counter advances on the timer IRQ */
    {
        pc_t *pc = mkpc();
        pc->cpu.r[REG_AX] = 0x0000;
        bios_handle_int(pc, 0x1A);
        CHECK_EQ(DX(&pc->cpu), 0);
        bios_handle_int(pc, 0x08);               /* timer tick */
        pc->cpu.r[REG_AX] = 0x0000;
        bios_handle_int(pc, 0x1A);
        CHECK_EQ(DX(&pc->cpu), 1);
        CHECK_EQ(CX(&pc->cpu), 0);
        freepc(pc);
    }
    /* INT 1Ah AH=04h returns a plausible date */
    {
        pc_t *pc = mkpc();
        pc->cpu.r[REG_AX] = 0x0400;
        bios_handle_int(pc, 0x1A);
        CHECK_EQ(CH(&pc->cpu), 0x20);            /* century */
        CHECK_EQ(CL(&pc->cpu), 0x26);            /* year */
        freepc(pc);
    }
    /* PIC masking: raise then deliver */
    {
        pc_t *pc = mkpc();
        pc->pic.imr = 0x00;
        pc->cpu.eflags |= FLAG_IF;
        pc->cpu.sreg[SREG_CS] = 0x1000; pc->cpu.eip = 0x0000;
        pc->cpu.r[REG_SP] = 0x8000; pc->cpu.sreg[SREG_SS] = 0x2000;
        pic_raise(pc, IRQ_TIMER);
        CHECK(pic_deliver(pc));
        /* After delivery CS:IP is the timer stub. */
        CHECK_EQ(pc->cpu.sreg[SREG_CS], 0xF000);
        CHECK_EQ(pc->cpu.eip, 0x1000 + 8 * 4);
        freepc(pc);
    }
}
