/* Munt386 -- shared test harness. */
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include "munt386.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int g_tests;
extern int g_fails;

#define CHECK(cond) do { \
    g_tests++; \
    if (!(cond)) { g_fails++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_EQ(a, b) do { \
    g_tests++; \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { \
        g_fails++; \
        printf("  FAIL %s:%d: %s (0x%llX) != %s (0x%llX)\n", \
               __FILE__, __LINE__, #a, _a, #b, _b); \
    } \
} while (0)

static inline pc_t *mkpc(void)
{
    pc_t *pc = (pc_t *)malloc(sizeof(pc_t));
    machine_init(pc);
    return pc;
}

static inline void freepc(pc_t *pc)
{
    machine_free(pc);
    free(pc);
}

static inline void load(pc_t *pc, uint16_t seg, uint16_t off,
                        const uint8_t *code, size_t n)
{
    for (size_t i = 0; i < n; i++)
        mem_write8(pc, seg, (uint16_t)(off + i), code[i]);
}

/* Point the CPU at a fresh code segment with a sane stack and data segments. */
static inline void setup(pc_t *pc, uint16_t cs, uint16_t ip)
{
    pc->cpu.sreg[SREG_CS] = cs;
    pc->cpu.eip = ip;
    pc->cpu.sreg[SREG_SS] = 0x2000;
    pc->cpu.r[REG_SP] = 0xFFFE;
    pc->cpu.sreg[SREG_DS] = 0x3000;
    pc->cpu.sreg[SREG_ES] = 0x4000;
    pc->cpu.halted = 0;
    pc->cpu.fault = 0;
    pc->running = 1;
}

static inline void step(pc_t *pc, int n)
{
    for (int i = 0; i < n && !pc->cpu.fault; i++) cpu_step(pc);
}

#endif /* TEST_UTIL_H */
