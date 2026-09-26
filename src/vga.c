/*
 * Munt386 -- virtual VGA/CGA video subsystem.
 *
 * The video backend renders the guest's video memory into an abstract RGB
 * framebuffer.  The CPU core never learns where that framebuffer goes: on the
 * host it is blitted to a window, on the CSE it is downscaled to the 320x240
 * LCD by vga_to_cse_lcd().
 *
 * Implemented modes (the set DOS and early Windows actually use):
 *   0/1  40x25 text, 16 colour
 *   2/3  80x25 text, 16 colour
 *   7    80x25 text, monochrome
 *   4/5  320x200 4-colour CGA graphics
 *   6    640x200 2-colour CGA graphics
 *   13h  320x200 256-colour linear
 */
#include "munt386.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>

static const uint32_t cga16[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

void vga_init(pc_t *pc, uint32_t fb_w, uint32_t fb_h)
{
    vga_t *v = &pc->vga;
    memset(v, 0, sizeof(*v));
    if (fb_w > VGA_FB_MAX_W) fb_w = VGA_FB_MAX_W;
    if (fb_h > VGA_FB_MAX_H) fb_h = VGA_FB_MAX_H;
    v->fb_w = fb_w ? fb_w : VGA_FB_MAX_W;
    v->fb_h = fb_h ? fb_h : VGA_FB_MAX_H;
    v->fb = (uint8_t *)platform_memory_alloc((size_t)v->fb_w * v->fb_h * 3);
    if (v->fb) memset(v->fb, 0, (size_t)v->fb_w * v->fb_h * 3);
    v->vga_mem_size = 0xFFFF;   /* ~64 KiB window at A0000 */
    vga_reset(v);
}

void vga_reset(vga_t *v)
{
    if (v->fb) memset(v->fb, 0, v->fb_w * v->fb_h * 3);
    v->mode = 3;
    v->width = 640; v->height = 200;
    v->base_phys = X86_CGA_BASE;
    v->cols = 80; v->rows = 25;
    v->graphics = 0;
    v->page = 0;
    v->cursor_row = 0; v->cursor_col = 0;
    v->cursor_start = 6; v->cursor_end = 7;
    v->enabled_cursor = 1;
    v->attr = 0x07;
    v->dirty = 1;
}

void vga_set_mode(pc_t *pc, uint8_t mode)
{
    vga_t *v = &pc->vga;
    v->mode = mode;
    v->page = 0;
    v->graphics = 0;
    v->base_phys = X86_CGA_BASE;
    v->cols = 80; v->rows = 25;
    v->width = 640; v->height = 200;

    switch (mode) {
        case 0x00: case 0x01: v->cols = 40; v->rows = 25; v->width = 320; break;
        case 0x02: case 0x03: v->cols = 80; v->rows = 25; v->width = 640; break;
        case 0x04: case 0x05: v->graphics = 1; v->width = 320; v->height = 200; break;
        case 0x06: v->graphics = 1; v->width = 640; v->height = 200; break;
        case 0x07: v->base_phys = X86_MONO_BASE; v->cols = 80; v->rows = 25; v->width = 640; break;
        case 0x0D: v->graphics = 1; v->width = 320; v->height = 200; break;
        case 0x10: case 0x12: v->graphics = 1; v->width = 640; v->height = 480; break; /* VBE-ish */
        case 0x13: v->graphics = 1; v->base_phys = X86_VGA_BASE; v->width = 320; v->height = 200; break;
        default: v->mode = 3; v->cols = 80; v->rows = 25; v->width = 640; break;
    }

    /* Clear video memory for the mode. */
    uint32_t bytes = v->graphics
        ? (mode == 0x13 ? 320u * 200u : (mode == 0x06 ? 640u * 200u / 8u : (mode == 0x10 || mode == 0x12 ? 640u * 480u : 320u * 200u / 4u)))
        : (uint32_t)(v->cols * v->rows * 2);
    if (mode == 0x03 || mode == 0x02 || mode == 0x07) {
        /* Fill text with attribute 0x07 cells (space). */
        for (uint32_t i = 0; i < bytes; i += 2) {
            mem_pwrite8(pc, v->base_phys + i, ' ');
            mem_pwrite8(pc, v->base_phys + i + 1, 0x07);
        }
    } else {
        for (uint32_t i = 0; i < bytes; i++) mem_pwrite8(pc, v->base_phys + i, 0);
    }

    /* BIOS current video mode + columns. */
    mem_pwrite8(pc, 0x449, mode);
    mem_pwrite16(pc, 0x44A, (uint16_t)v->cols);
    mem_pwrite8(pc, 0x484, (uint8_t)(v->rows - 1));
    v->dirty = 1;
}

/* ------------------------------------------------------------------ */
/* Rendering                                                           */
/* ------------------------------------------------------------------ */

static void put_px(vga_t *v, int x, int y, uint32_t rgb)
{
    if (x < 0 || y < 0 || (uint32_t)x >= v->fb_w || (uint32_t)y >= v->fb_h) return;
    uint8_t *p = v->fb + ((size_t)y * v->fb_w + (size_t)x) * 3;
    p[0] = (uint8_t)(rgb >> 16);
    p[1] = (uint8_t)(rgb >> 8);
    p[2] = (uint8_t)(rgb);
}

static uint32_t dac256(unsigned idx)
{
    /* 6-bit VGA DAC expanded to 8-bit, standard 256-colour ramp. */
    static uint8_t cot[256][3];
    static int init = 0;
    if (!init) {
        for (int i = 0; i < 256; i++) {
            int r = (i >> 5) & 7, g = (i >> 2) & 7, b = i & 3;
            cot[i][0] = (uint8_t)((r * 255) / 7);
            cot[i][1] = (uint8_t)((g * 255) / 7);
            cot[i][2] = (uint8_t)((b * 255) / 3);
        }
        init = 1;
    }
    return ((uint32_t)cot[idx][0] << 16) | ((uint32_t)cot[idx][1] << 8) | cot[idx][2];
}

static void render_text(pc_t *pc, vga_t *v)
{
    int cell_h = 8;
    for (int row = 0; row < v->rows; row++) {
        for (int col = 0; col < v->cols; col++) {
            uint32_t cell = v->base_phys + (uint32_t)(row * v->cols + col) * 2;
            uint8_t ch = mem_pread8(pc, cell);
            uint8_t attr = mem_pread8(pc, cell + 1);
            uint8_t fg = attr & 0x0F;
            uint8_t bg = (attr >> 4) & 0x0F;
            if (v->mode == 0x07) { fg = (attr & 0x08) ? 15 : 7; bg = (attr & 0x80) ? 0 : 0; }
            uint32_t fgrgb = cga16[fg & 15], bgrgb = cga16[bg & 15];
            int px = col * 8, py = row * cell_h;
            const uint8_t *glyph = NULL;
            if (ch >= 0x20 && ch <= 0x7E) glyph = munt386_font8x8[ch - 0x20];
            for (int gy = 0; gy < cell_h; gy++) {
                uint8_t bits = 0;
                if (glyph) bits = glyph[gy < 8 ? gy : 7];
                for (int gx = 0; gx < 8; gx++) {
                    put_px(v, px + gx, py + gy, (bits & (0x80 >> gx)) ? fgrgb : bgrgb);
                }
            }
        }
    }
    /* Cursor */
    if (v->enabled_cursor && v->cursor_row < v->rows && v->cursor_col < v->cols) {
        int px = v->cursor_col * 8, py = v->cursor_row * cell_h;
        uint32_t cell = v->base_phys + (uint32_t)(v->cursor_row * v->cols + v->cursor_col) * 2;
        uint8_t attr = mem_pread8(pc, cell + 1);
        uint32_t rgb = cga16[(attr & 0x0F) & 15];
        for (int gy = v->cursor_start; gy <= v->cursor_end && gy < cell_h; gy++)
            for (int gx = 0; gx < 8; gx++) put_px(v, px + gx, py + gy, rgb);
    }
}

static void render_mode4(pc_t *pc, vga_t *v)
{
    /* 320x200, 2 bits/pixel, rows interleaved in 8 KiB halves. */
    for (int y = 0; y < 200; y++) {
        uint32_t rowbase = v->base_phys + (uint32_t)((y & 1) ? 0x2000 : 0) + (uint32_t)(y / 2) * 80;
        for (int xb = 0; xb < 80; xb++) {
            uint8_t b = mem_pread8(pc, rowbase + (uint32_t)xb);
            for (int k = 0; k < 4; k++) {
                int val = (b >> (6 - k * 2)) & 3;
                uint32_t rgb = (val == 0) ? 0 : cga16[val == 1 ? 10 : (val == 2 ? 12 : 15)];
                put_px(v, xb * 4 + k, y, rgb);
            }
        }
    }
}

static void render_mode6(pc_t *pc, vga_t *v)
{
    for (int y = 0; y < 200; y++) {
        uint32_t rowbase = v->base_phys + (uint32_t)((y & 1) ? 0x2000 : 0) + (uint32_t)(y / 2) * 80;
        for (int xb = 0; xb < 80; xb++) {
            uint8_t b = mem_pread8(pc, rowbase + (uint32_t)xb);
            for (int k = 0; k < 8; k++)
                put_px(v, xb * 8 + k, y, (b & (0x80 >> k)) ? 0xFFFFFF : 0x000000);
        }
    }
}

static void render_mode13(pc_t *pc, vga_t *v)
{
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 320; x++)
            put_px(v, x, y, dac256(mem_pread8(pc, X86_VGA_BASE + (uint32_t)(y * 320 + x))));
}

void vga_render(pc_t *pc)
{
    vga_t *v = &pc->vga;
    if (!v->fb) return;
    if (!v->graphics) render_text(pc, v);
    else if (v->mode == 0x13) render_mode13(pc, v);
    else if (v->mode == 0x06) render_mode6(pc, v);
    else render_mode4(pc, v);
    v->dirty = 0;
}

/* ------------------------------------------------------------------ */
/* CSE LCD downscaler                                                  */
/* ------------------------------------------------------------------ */

/* Bulk-convert one RGB888 row to RGB565, sampling every `step`-th pixel.
 * Single pass, no per-pixel function calls — this is the inner loop the CSE
 * video backend uses for its blit. */
void vga_rgb_row_to_rgb565(const uint8_t *row, uint32_t src_w,
                           uint16_t *dst, uint32_t dst_w, uint32_t step)
{
    uint32_t x;
    if (!row || !dst || step == 0) return;
    for (x = 0; x < dst_w; x++) {
        uint32_t sx = x * step;
        if (sx >= src_w) sx = src_w - 1;
        {
            const uint8_t *p = row + (size_t)sx * 3;
            uint16_t r5 = (uint16_t)(p[0] >> 3);
            uint16_t g6 = (uint16_t)(p[1] >> 2);
            uint16_t b5 = (uint16_t)(p[2] >> 3);
            dst[x] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
        }
    }
}

void vga_to_cse_lcd(const vga_t *v, uint16_t *lcd_rgb565)
{
    uint32_t step = v->fb_w ? (v->fb_w + CSE_LCD_W - 1) / CSE_LCD_W : 1;
    if (!v->fb) return;
    for (uint32_t y = 0; y < CSE_LCD_H; y++) {
        uint32_t sy = (uint32_t)((uint64_t)y * v->fb_h / CSE_LCD_H);
        if (sy >= v->fb_h) sy = v->fb_h - 1;
        vga_rgb_row_to_rgb565(v->fb + (size_t)sy * v->fb_w * 3, v->fb_w,
                              lcd_rgb565 + (size_t)y * CSE_LCD_W, CSE_LCD_W,
                              step);
    }
}
