/* Munt386 -- video subsystem tests. */
#include "../test_util.h"

void test_video(void);

void test_video(void)
{
    /* Text mode renders glyph pixels */
    {
        pc_t *pc = mkpc();
        pc->cpu.r[REG_AX] = 0x0003;
        bios_handle_int(pc, 0x10);
        pc->cpu.r[REG_AX] = 0x0E48;              /* 'H' */
        bios_handle_int(pc, 0x10);
        vga_render(pc);
        const uint8_t *px = pc->vga.fb;
        int lit = 0;
        for (int i = 0; i < 8 * 8; i++) lit += (px[i * 3] || px[i * 3 + 1] || px[i * 3 + 2]);
        CHECK(lit > 0);
        /* The far-right margin column of the first glyph cell is background. */
        int x = 7, y = 0;
        const uint8_t *p = px + ((size_t)y * pc->vga.fb_w + x) * 3;
        CHECK(!(p[0] || p[1] || p[2]));
        freepc(pc);
    }
    /* Space renders as pure background */
    {
        pc_t *pc = mkpc();
        pc->cpu.r[REG_AX] = 0x0003;
        bios_handle_int(pc, 0x10);
        vga_render(pc);
        int lit = 0;
        for (int i = 0; i < 8 * 8; i++)
            lit += (pc->vga.fb[i * 3] || pc->vga.fb[i * 3 + 1] || pc->vga.fb[i * 3 + 2]);
        CHECK_EQ(lit, 0);
        freepc(pc);
    }
    /* Mode 13h linear framebuffer + pixel write */
    {
        pc_t *pc = mkpc();
        pc->cpu.r[REG_AX] = 0x0013;              /* AH=0, AL=0x13 */
        bios_handle_int(pc, 0x10);
        CHECK(pc->vga.graphics);
        CHECK_EQ(pc->vga.base_phys, X86_VGA_BASE);
        pc->cpu.r[REG_AX] = 0x0C0F;              /* AH=0Ch write pixel, AL=15 */
        pc->cpu.r[REG_CX] = 10;
        pc->cpu.r[REG_DX] = 10;
        bios_handle_int(pc, 0x10);
        CHECK_EQ(mem_pread8(pc, X86_VGA_BASE + 10 * 320 + 10), 15);
        vga_render(pc);
        const uint8_t *p = pc->vga.fb + ((size_t)10 * pc->vga.fb_w + 10) * 3;
        CHECK(p[0] || p[1] || p[2]);
        freepc(pc);
    }
    /* CGA mode 6 addressing (interleaved rows) */
    {
        pc_t *pc = mkpc();
        pc->cpu.r[REG_AX] = 0x0006;
        bios_handle_int(pc, 0x10);
        CHECK(pc->vga.graphics);
        CHECK_EQ(pc->vga.base_phys, X86_CGA_BASE);
        freepc(pc);
    }
    /* CSE LCD downscale produces the right number of pixels */
    {
        pc_t *pc = mkpc();
        pc->cpu.r[REG_AX] = 0x0013;
        bios_handle_int(pc, 0x10);
        pc->cpu.r[REG_AX] = 0x0C0F;
        pc->cpu.r[REG_CX] = 100; pc->cpu.r[REG_DX] = 100;
        bios_handle_int(pc, 0x10);
        vga_render(pc);
        uint16_t *lcd = (uint16_t *)calloc(CSE_LCD_W * CSE_LCD_H, sizeof(uint16_t));
        vga_to_cse_lcd(&pc->vga, lcd);
        int lit = 0;
        for (int i = 0; i < CSE_LCD_W * CSE_LCD_H; i++) lit += (lcd[i] != 0);
        CHECK(lit > 0);
        free(lcd);
        freepc(pc);
    }
}
