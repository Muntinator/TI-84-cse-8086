/* Munt386 -- whole-machine boot test.
 *
 * Builds a tiny, self-authored boot sector (public domain, written for this
 * project), attaches it as a floppy, resets the machine and runs it.  This
 * exercises the full path: reset vector -> INT 19h -> INT 13h read ->
 * jump to 0000:7C00 -> guest executes -> INT 10h teletype -> halt.
 */
#include "../test_util.h"

void test_pc(void);

static void build_boot_sector(uint8_t *sector)
{
    memset(sector, 0, DISK_SECTOR_SIZE);
    int o = 0;
    sector[o++] = 0xFA;                         /* cli              */
    sector[o++] = 0x31; sector[o++] = 0xC0;     /* xor ax,ax        */
    sector[o++] = 0x8E; sector[o++] = 0xD8;     /* mov ds,ax        */
    sector[o++] = 0x8E; sector[o++] = 0xC0;     /* mov es,ax        */
    sector[o++] = 0x8E; sector[o++] = 0xD0;     /* mov ss,ax        */
    sector[o++] = 0xBC; sector[o++] = 0x00; sector[o++] = 0x7C; /* mov sp,7C00 */
    sector[o++] = 0xFB;                         /* sti              */
    sector[o++] = 0xBE; sector[o++] = 0x1E; sector[o++] = 0x7C; /* mov si,7C1E */
    sector[o++] = 0xB4; sector[o++] = 0x0E;     /* mov ah,0Eh       */
    /* .next (o should be 0x12) */
    sector[o++] = 0xAC;                         /* lodsb            */
    sector[o++] = 0x0A; sector[o++] = 0xC0;     /* or al,al         */
    sector[o++] = 0x74; sector[o++] = 0x04;     /* jz halt          */
    sector[o++] = 0xCD; sector[o++] = 0x10;     /* int 10h          */
    sector[o++] = 0xEB; sector[o++] = 0xF7;     /* jmp .next        */
    /* halt: */
    sector[o++] = 0xF4;                         /* hlt              */
    sector[o++] = 0xEB; sector[o++] = 0xFE;     /* jmp halt         */
    /* message at 0x1E */
    const char *msg = "MUNT386 OK\r\n";
    CHECK_EQ(o, 0x1E);
    for (int i = 0; msg[i]; i++) sector[o++] = (uint8_t)msg[i];
    sector[o++] = 0;
    sector[510] = 0x55;
    sector[511] = 0xAA;
}

void test_pc(void)
{
    uint8_t *img = (uint8_t *)calloc(1, 9 * 2 * 80 * 512);
    build_boot_sector(img);

    pc_t *pc = mkpc();
    CHECK_EQ(disk_attach(&pc->disk[0], img, 9 * 2 * 80 * 512, 0, 0), 0);

    /* Machine reset points at the reset vector (F000:FFF0 = INT 19h). */
    machine_reset(pc);
    CHECK_EQ(pc->cpu.sreg[SREG_CS], 0xF000);
    CHECK_EQ(pc->cpu.eip, 0xFFF0);

    uint64_t steps = cpu_run(pc, 100000);
    CHECK(steps > 0);
    CHECK(!pc->cpu.fault);
    CHECK(pc->cpu.halted);

    /* The guest printed its message via INT 10h into the text buffer. */
    /* Text cells are (char, attr) pairs, so characters are 2 bytes apart. */
    CHECK_EQ(mem_pread8(pc, X86_CGA_BASE + 0), 'M');
    CHECK_EQ(mem_pread8(pc, X86_CGA_BASE + 1), 0x07);
    CHECK_EQ(mem_pread8(pc, X86_CGA_BASE + 2), 'U');
    CHECK_EQ(mem_pread8(pc, X86_CGA_BASE + 4), 'N');
    CHECK_EQ(mem_pread8(pc, X86_CGA_BASE + 6), 'T');
    CHECK_EQ(mem_pread8(pc, X86_CGA_BASE + 8), '3');
    CHECK_EQ(mem_pread8(pc, X86_CGA_BASE + 10), '8');
    CHECK_EQ(mem_pread8(pc, X86_CGA_BASE + 12), '6');
    CHECK_EQ(mem_pread8(pc, X86_CGA_BASE + 16), 'O');
    CHECK_EQ(mem_pread8(pc, X86_CGA_BASE + 18), 'K');

    /* The trailing CR/LF moved the cursor to the start of the next line. */
    CHECK_EQ(pc->vga.cursor_row, 1);
    CHECK_EQ(pc->vga.cursor_col, 0);

    /* The framebuffer is non-empty. */
    vga_render(pc);
    int lit = 0;
    for (int i = 0; i < 200 * 8; i++)
        lit += (pc->vga.fb[i * 3] || pc->vga.fb[i * 3 + 1] || pc->vga.fb[i * 3 + 2]);
    CHECK(lit > 0);

    /* Refuse to boot an image without the 0x55AA signature. */
    {
        uint8_t *bad = (uint8_t *)calloc(1, 512);
        pc_t *pc2 = mkpc();
        disk_attach(&pc2->disk[0], bad, 512, 0, 0);
        machine_reset(pc2);
        cpu_run(pc2, 1000);
        CHECK(!pc2->running);
        free(bad);
        freepc(pc2);
    }

    /* A boot with no media just stops. */
    {
        pc_t *pc3 = mkpc();
        machine_reset(pc3);
        cpu_run(pc3, 1000);
        CHECK(!pc3->running);
        freepc(pc3);
    }

    freepc(pc);
    free(img);
}
