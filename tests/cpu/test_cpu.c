/* Munt386 -- instruction-level CPU tests. */
#include "../test_util.h"

void test_cpu(void);

void test_cpu(void)
{
    /* MOV r16, imm16 */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        uint8_t c[] = { 0xB8, 0x34, 0x12 };              /* mov ax,0x1234 */
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 1);
        CHECK_EQ(AX(&pc->cpu), 0x1234);
        CHECK_EQ(pc->cpu.eip, 3);
        freepc(pc);
    }
    /* MOV r8, imm8 (AH) */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        pc->cpu.r[REG_AX] = 0x00FF;
        uint8_t c[] = { 0xB4, 0xAB };                    /* mov ah,0xAB */
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 1);
        CHECK_EQ(AX(&pc->cpu), 0xABFF);
        freepc(pc);
    }
    /* MOV r/m16, r16 into memory [BX+disp16] */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        pc->cpu.r[REG_BX] = 0x1000;
        pc->cpu.r[REG_AX] = 0xBEEF;
        uint8_t c[] = { 0x89, 0x87, 0x00, 0x01 };        /* mov [bx+0x100],ax */
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 1);
        CHECK_EQ(mem_read16(pc, 0x3000, 0x1100), 0xBEEF);
        freepc(pc);
    }
    /* MOV r16, r/m16 from memory [BX] */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        pc->cpu.r[REG_BX] = 0x0200;
        mem_write16(pc, 0x3000, 0x0200, 0x1234);
        uint8_t c[] = { 0x8B, 0x07 };                    /* mov ax,[bx] */
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 1);
        CHECK_EQ(AX(&pc->cpu), 0x1234);
        freepc(pc);
    }
    /* Segment override: mov ax, es:[bx] */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        pc->cpu.r[REG_BX] = 0x0200;
        mem_write16(pc, 0x4000, 0x0200, 0x5678);
        uint8_t c[] = { 0x26, 0x8B, 0x07 };              /* mov ax,es:[bx] */
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 1);
        CHECK_EQ(AX(&pc->cpu), 0x5678);
        freepc(pc);
    }
    /* ADD r/m16, r16 */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        pc->cpu.r[REG_AX] = 0x0001; pc->cpu.r[REG_BX] = 0x0002;
        uint8_t c[] = { 0x01, 0xD8 };                    /* add ax,bx */
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 1);
        CHECK_EQ(AX(&pc->cpu), 0x0003);
        freepc(pc);
    }
    /* ADD AL, imm8 */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        pc->cpu.r[REG_AX] = 0x0040;
        uint8_t c[] = { 0x04, 0x40 };                    /* add al,0x40 */
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 1);
        CHECK_EQ(AL(&pc->cpu), 0x80);
        CHECK(pc->cpu.eflags & FLAG_OF);
        freepc(pc);
    }
    /* INC/DEC r16 preserve CF */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        pc->cpu.r[REG_AX] = 0xFFFF;
        pc->cpu.eflags |= FLAG_CF;
        uint8_t c[] = { 0x40 };                          /* inc ax */
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 1);
        CHECK_EQ(AX(&pc->cpu), 0x0000);
        CHECK(pc->cpu.eflags & FLAG_CF);
        CHECK(pc->cpu.eflags & FLAG_ZF);
        freepc(pc);
    }
    /* PUSH/POP round trip */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        uint16_t sp0 = SP(&pc->cpu);
        uint8_t c[] = { 0xB8, 0x34, 0x12, 0x50, 0xB8, 0x00, 0x00, 0x58 };
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 4);
        CHECK_EQ(AX(&pc->cpu), 0x1234);
        CHECK_EQ(SP(&pc->cpu), sp0);
        freepc(pc);
    }
    /* CALL/RET */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        uint8_t c[] = {
            0xB8, 0x11, 0x11,   /* mov ax,0x1111 */
            0xE8, 0x02, 0x00,   /* call 0x1008   */
            0xF4,               /* hlt           */
            0x90,               /* nop           */
            0xB8, 0x22, 0x22,   /* mov ax,0x2222 */
            0xC3                /* ret           */
        };
        load(pc, 0x1000, 0, c, sizeof c);
        step(pc, 4);
        CHECK_EQ(AX(&pc->cpu), 0x2222);
        CHECK_EQ(pc->cpu.eip, 0x0006);   /* EIP is a 16-bit offset */
        freepc(pc);
    }
    /* Conditional jump: JZ taken (ZF=1 from XOR) and not taken. */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        uint8_t c[] = { 0x31, 0xC0, 0x74, 0x01, 0xF4, 0xF4 }; /* xor ax,ax; jz +1 */
        load(pc, 0x1000, 0, c, sizeof c);
        step(pc, 2);
        CHECK(pc->cpu.eflags & FLAG_ZF);
        CHECK_EQ(pc->cpu.eip, 0x0005);
        freepc(pc);
    }
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        uint8_t c[] = { 0xB8, 0x01, 0x00, 0x3D, 0x02, 0x00, 0x74, 0x01, 0xF4, 0xF4 };
        /* mov ax,1; cmp ax,2; jz +1 (not taken) */
        load(pc, 0x1000, 0, c, sizeof c);
        step(pc, 3);
        CHECK(!(pc->cpu.eflags & FLAG_ZF));
        CHECK_EQ(pc->cpu.eip, 0x0008);
        freepc(pc);
    }
    /* CMP + JE semantics */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        pc->cpu.r[REG_AX] = 0x1234; pc->cpu.r[REG_BX] = 0x1234;
        uint8_t c[] = { 0x39, 0xD8 };                    /* cmp ax,bx */
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 1);
        CHECK(pc->cpu.eflags & FLAG_ZF);
        freepc(pc);
    }
    /* LEA */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        pc->cpu.r[REG_BX] = 0x1234;
        uint8_t c[] = { 0x8D, 0x87, 0x10, 0x00 };        /* lea ax,[bx+0x10] */
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 1);
        CHECK_EQ(AX(&pc->cpu), 0x1244);
        freepc(pc);
    }
    /* MUL r/m16 */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        pc->cpu.r[REG_AX] = 0x0010; pc->cpu.r[REG_CX] = 0x0010;
        uint8_t c[] = { 0xF7, 0xE1 };                    /* mul cx */
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 1);
        CHECK_EQ(AX(&pc->cpu), 0x0100);
        CHECK_EQ(DX(&pc->cpu), 0x0000);
        CHECK(!(pc->cpu.eflags & FLAG_CF));
        freepc(pc);
    }
    /* DIV r/m16 */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        pc->cpu.r[REG_AX] = 0x0007; pc->cpu.r[REG_DX] = 0x0000;
        pc->cpu.r[REG_CX] = 0x0002;
        uint8_t c[] = { 0xF7, 0xF1 };                    /* div cx */
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 1);
        CHECK_EQ(AX(&pc->cpu), 0x0003);
        CHECK_EQ(DX(&pc->cpu), 0x0001);
        freepc(pc);
    }
    /* Shift: SHL AX,1 */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        pc->cpu.r[REG_AX] = 0x8001;
        uint8_t c[] = { 0xD1, 0xE0 };                    /* shl ax,1 */
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 1);
        CHECK_EQ(AX(&pc->cpu), 0x0002);
        CHECK(pc->cpu.eflags & FLAG_CF);
        freepc(pc);
    }
    /* Shift by CL */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        pc->cpu.r[REG_AX] = 0x0001; pc->cpu.r[REG_CX] = 0x0004;
        uint8_t c[] = { 0xD3, 0xE0 };                    /* shl ax,cl */
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 1);
        CHECK_EQ(AX(&pc->cpu), 0x0010);
        freepc(pc);
    }
    /* LOOP */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        pc->cpu.r[REG_CX] = 3;
        uint8_t c[] = { 0xE2, 0xFE };                    /* loop $ */
        load(pc, 0x1000, 0, c, sizeof c); step(pc, 3);
        CHECK_EQ(CX(&pc->cpu), 0);
        freepc(pc);
    }
    /* LAHF/SAHF round trip */
    {
        pc_t *pc = mkpc(); setup(pc, 0x1000, 0);
        pc->cpu.eflags |= (FLAG_CF | FLAG_ZF | FLAG_SF);
        uint8_t c[] = { 0x9F, 0x9E };                    /* lahf; sahf */
        load(pc, 0x1000, 0, c, sizeof c);
        step(pc, 2);
        CHECK(pc->cpu.eflags & FLAG_CF);
        CHECK(pc->cpu.eflags & FLAG_ZF);
        CHECK(pc->cpu.eflags & FLAG_SF);
        freepc(pc);
    }
}
