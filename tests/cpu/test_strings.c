/* Munt386 -- string instruction tests (MOVS/STOS/LODS/CMPS/SCAS, REP). */
#include "../test_util.h"

void test_strings(void);

void test_strings(void)
{
    /* MOVSB */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        pc->cpu.r[REG_SI] = 0x0100; pc->cpu.r[REG_DI] = 0x0200;
        mem_write8(pc, 0x3000, 0x0100, 0xAA);
        uint8_t c[] = { 0xA4 };
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 1);
        CHECK_EQ(mem_read8(pc, 0x4000, 0x0200), 0xAA);
        CHECK_EQ(pc->cpu.r[REG_SI], 0x0101);
        CHECK_EQ(pc->cpu.r[REG_DI], 0x0201);
        freepc(pc);
    }
    /* MOVSW advances by two */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        pc->cpu.r[REG_SI] = 0x0100; pc->cpu.r[REG_DI] = 0x0200;
        mem_write16(pc, 0x3000, 0x0100, 0xBEEF);
        uint8_t c[] = { 0xA5 };
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 1);
        CHECK_EQ(mem_read16(pc, 0x4000, 0x0200), 0xBEEF);
        CHECK_EQ(pc->cpu.r[REG_SI], 0x0102);
        freepc(pc);
    }
    /* STOSB */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        pc->cpu.r[REG_DI] = 0x0300;
        SET_AL(&pc->cpu, 0x55);
        uint8_t c[] = { 0xAA };
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 1);
        CHECK_EQ(mem_read8(pc, 0x4000, 0x0300), 0x55);
        CHECK_EQ(pc->cpu.r[REG_DI], 0x0301);
        freepc(pc);
    }
    /* LODSB */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        pc->cpu.r[REG_SI] = 0x0100;
        mem_write8(pc, 0x3000, 0x0100, 0x42);
        uint8_t c[] = { 0xAC };
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 1);
        CHECK_EQ(AL(&pc->cpu), 0x42);
        CHECK_EQ(pc->cpu.r[REG_SI], 0x0101);
        freepc(pc);
    }
    /* CMPSB equal -> ZF */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        pc->cpu.r[REG_SI] = 0x0100; pc->cpu.r[REG_DI] = 0x0100;
        mem_write8(pc, 0x3000, 0x0100, 0x11);
        mem_write8(pc, 0x4000, 0x0100, 0x11);
        uint8_t c[] = { 0xA6 };
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 1);
        CHECK(pc->cpu.eflags & FLAG_ZF);
        freepc(pc);
    }
    /* SCASB match */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        pc->cpu.r[REG_DI] = 0x0100;
        SET_AL(&pc->cpu, 0x77);
        mem_write8(pc, 0x4000, 0x0100, 0x77);
        uint8_t c[] = { 0xAE };
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 1);
        CHECK(pc->cpu.eflags & FLAG_ZF);
        freepc(pc);
    }
    /* REP MOVSB copies CX bytes */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        pc->cpu.r[REG_SI] = 0x0100; pc->cpu.r[REG_DI] = 0x0200; pc->cpu.r[REG_CX] = 4;
        for (int i = 0; i < 4; i++) mem_write8(pc, 0x3000, (uint16_t)(0x0100 + i), (uint8_t)(0xA0 + i));
        uint8_t c[] = { 0xF3, 0xA4 };
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 1);
        for (int i = 0; i < 4; i++) CHECK_EQ(mem_read8(pc, 0x4000, (uint16_t)(0x0200 + i)), 0xA0 + i);
        CHECK_EQ(CX(&pc->cpu), 0);
        freepc(pc);
    }
    /* REPNE SCASB stops at the first match */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        pc->cpu.r[REG_DI] = 0x0100; pc->cpu.r[REG_CX] = 8;
        SET_AL(&pc->cpu, 0x33);
        mem_write8(pc, 0x4000, 0x0103, 0x33);
        uint8_t c[] = { 0xF2, 0xAE };
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 1);
        CHECK(pc->cpu.eflags & FLAG_ZF);
        CHECK_EQ(CX(&pc->cpu), 4);
        CHECK_EQ(pc->cpu.r[REG_DI], 0x0104);
        freepc(pc);
    }
    /* Direction flag: STD makes SI/DI decrement */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        pc->cpu.r[REG_DI] = 0x0300;
        SET_AL(&pc->cpu, 0x99);
        uint8_t c[] = { 0xFD, 0xAA };   /* std; stosb */
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 2);
        CHECK_EQ(mem_read8(pc, 0x4000, 0x0300), 0x99);
        CHECK_EQ(pc->cpu.r[REG_DI], 0x02FF);
        freepc(pc);
    }
}
