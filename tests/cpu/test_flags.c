/* Munt386 -- FLAGS behaviour and arithmetic edge cases. */
#include "../test_util.h"

void test_flags(void);

/* Run `code` and return the flags register afterwards. */
static uint32_t run_flags(const uint8_t *code, size_t n)
{
    pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
    load(pc, 0x1000, 0, code, n);
    /* Execute up to 8 instructions. */
    for (int i = 0; i < 8 && !pc->cpu.fault; i++) cpu_step(pc);
    uint32_t f = pc->cpu.eflags;
    freepc(pc);
    return f;
}

void test_flags(void)
{
    /* FFFF + 1 = 0000, CF=1 ZF=1 AF=1 PF=1 OF=0 SF=0 */
    {
        uint8_t c[] = { 0xB8, 0xFF, 0xFF, 0x05, 0x01, 0x00 };
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0); load(pc, 0x1000, 0, c, sizeof c); step(pc, 2);
        CHECK_EQ(AX(&pc->cpu), 0);
        CHECK(pc->cpu.eflags & FLAG_CF);
        CHECK(pc->cpu.eflags & FLAG_ZF);
        CHECK(pc->cpu.eflags & FLAG_AF);
        CHECK(pc->cpu.eflags & FLAG_PF);
        CHECK(!(pc->cpu.eflags & FLAG_OF));
        CHECK(!(pc->cpu.eflags & FLAG_SF));
        freepc(pc);
    }
    /* 7FFF + 1 = 8000, OF=1 SF=1 CF=0 ZF=0 */
    {
        uint8_t c[] = { 0xB8, 0xFF, 0x7F, 0x05, 0x01, 0x00 };
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0); load(pc, 0x1000, 0, c, sizeof c); step(pc, 2);
        CHECK_EQ(AX(&pc->cpu), 0x8000);
        CHECK(pc->cpu.eflags & FLAG_OF);
        CHECK(pc->cpu.eflags & FLAG_SF);
        CHECK(!(pc->cpu.eflags & FLAG_CF));
        CHECK(!(pc->cpu.eflags & FLAG_ZF));
        freepc(pc);
    }
    /* 8000 - 1 = 7FFF, OF=1 SF=0 */
    {
        uint8_t c[] = { 0xB8, 0x00, 0x80, 0x2D, 0x01, 0x00 };
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0); load(pc, 0x1000, 0, c, sizeof c); step(pc, 2);
        CHECK_EQ(AX(&pc->cpu), 0x7FFF);
        CHECK(pc->cpu.eflags & FLAG_OF);
        CHECK(!(pc->cpu.eflags & FLAG_SF));
        CHECK(!(pc->cpu.eflags & FLAG_CF));
        freepc(pc);
    }
    /* 0000 - 1 = FFFF, CF=1 SF=1 AF=1 OF=0 */
    {
        uint8_t c[] = { 0xB8, 0x00, 0x00, 0x2D, 0x01, 0x00 };
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0); load(pc, 0x1000, 0, c, sizeof c); step(pc, 2);
        CHECK_EQ(AX(&pc->cpu), 0xFFFF);
        CHECK(pc->cpu.eflags & FLAG_CF);
        CHECK(pc->cpu.eflags & FLAG_SF);
        CHECK(pc->cpu.eflags & FLAG_AF);
        CHECK(!(pc->cpu.eflags & FLAG_ZF));
        CHECK(!(pc->cpu.eflags & FLAG_OF));
        freepc(pc);
    }
    /* 8-bit overflow: 0x7F + 1 = 0x80, OF=1 */
    {
        uint8_t c[] = { 0xB0, 0x7F, 0x04, 0x01 };
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0); load(pc, 0x1000, 0, c, sizeof c); step(pc, 2);
        CHECK_EQ(AL(&pc->cpu), 0x80);
        CHECK(pc->cpu.eflags & FLAG_OF);
        CHECK(pc->cpu.eflags & FLAG_SF);
        freepc(pc);
    }
    /* ADC with carry in: 0 + 0 + CF = 1 */
    {
        uint8_t c[] = { 0xB8, 0x00, 0x00, 0xF9, 0x15, 0x00, 0x00 };
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0); load(pc, 0x1000, 0, c, sizeof c); step(pc, 3);
        CHECK_EQ(AX(&pc->cpu), 1);
        freepc(pc);
    }
    /* SBB with borrow in: 0 - 0 - CF = FFFF */
    {
        uint8_t c[] = { 0xB8, 0x00, 0x00, 0xF9, 0x1D, 0x00, 0x00 };
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0); load(pc, 0x1000, 0, c, sizeof c); step(pc, 3);
        CHECK_EQ(AX(&pc->cpu), 0xFFFF);
        CHECK(pc->cpu.eflags & FLAG_CF);
        freepc(pc);
    }
    /* Logic clears CF and OF */
    {
        uint8_t c[] = { 0xB8, 0xFF, 0xFF, 0x25, 0x00, 0x00 };
        uint32_t f = run_flags(c, sizeof c);
        CHECK(!(f & FLAG_CF));
        CHECK(!(f & FLAG_OF));
        CHECK(f & FLAG_ZF);
    }
    /* TEST does not modify the operand */
    {
        uint8_t c[] = { 0xB8, 0x0F, 0x00, 0xA9, 0x0F, 0x00 };
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0); load(pc, 0x1000, 0, c, sizeof c); step(pc, 2);
        CHECK_EQ(AX(&pc->cpu), 0x000F);
        CHECK(!(pc->cpu.eflags & FLAG_ZF));
        freepc(pc);
    }
    /* NEG 1 -> FFFF with CF set */
    {
        uint8_t c[] = { 0xB8, 0x01, 0x00, 0xF7, 0xD8 };
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0); load(pc, 0x1000, 0, c, sizeof c); step(pc, 2);
        CHECK_EQ(AX(&pc->cpu), 0xFFFF);
        CHECK(pc->cpu.eflags & FLAG_CF);
        freepc(pc);
    }
    /* ROL */
    {
        uint8_t c[] = { 0xB8, 0x01, 0x80, 0xD1, 0xC0 };   /* mov ax,0x8001; rol ax,1 */
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0); load(pc, 0x1000, 0, c, sizeof c); step(pc, 2);
        CHECK_EQ(AX(&pc->cpu), 0x0003);
        CHECK(pc->cpu.eflags & FLAG_CF);
        freepc(pc);
    }
    /* ROR */
    {
        uint8_t c[] = { 0xB8, 0x01, 0x00, 0xD1, 0xC8 };   /* mov ax,1; ror ax,1 */
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0); load(pc, 0x1000, 0, c, sizeof c); step(pc, 2);
        CHECK_EQ(AX(&pc->cpu), 0x8000);
        CHECK(pc->cpu.eflags & FLAG_CF);
        freepc(pc);
    }
    /* RCL through carry */
    {
        uint8_t c[] = { 0xB8, 0x00, 0x80, 0xF9, 0xD1, 0xD0 };  /* mov ax,0x8000; stc; rcl ax,1 */
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0); load(pc, 0x1000, 0, c, sizeof c); step(pc, 3);
        CHECK_EQ(AX(&pc->cpu), 0x0001);
        CHECK(pc->cpu.eflags & FLAG_CF);
        freepc(pc);
    }
    /* SAR preserves sign */
    {
        uint8_t c[] = { 0xB8, 0x00, 0x80, 0xD1, 0xF8 };   /* mov ax,0x8000; sar ax,1 */
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0); load(pc, 0x1000, 0, c, sizeof c); step(pc, 2);
        CHECK_EQ(AX(&pc->cpu), 0xC000);
        freepc(pc);
    }
    /* Parity: 0x03 has even parity */
    {
        uint8_t c[] = { 0xB0, 0x03, 0xA8, 0x03 };         /* mov al,3; test al,3 */
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0); load(pc, 0x1000, 0, c, sizeof c); step(pc, 2);
        CHECK(pc->cpu.eflags & FLAG_PF);
        freepc(pc);
    }
    /* Parity: 0x07 has odd parity */
    {
        uint8_t c[] = { 0xB0, 0x07, 0xA8, 0x07 };
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0); load(pc, 0x1000, 0, c, sizeof c); step(pc, 2);
        CHECK(!(pc->cpu.eflags & FLAG_PF));
        freepc(pc);
    }
}
