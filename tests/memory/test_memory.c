/* Munt386 -- virtual memory and 20-bit address calculation tests. */
#include "../test_util.h"

void test_memory(void);

void test_memory(void)
{
    /* Physical address calculation (real-mode segment:offset). */
    CHECK_EQ(phys20(0x0000, 0x0000), 0x00000);
    CHECK_EQ(phys20(0x0000, 0x0010), 0x00010);
    CHECK_EQ(phys20(0x1000, 0x0000), 0x10000);
    CHECK_EQ(phys20(0xFFFF, 0x0000), 0xFFFF0);
    CHECK_EQ(phys20(0xF000, 0xFFF0), 0xFFFF0);
    /* Wraparound past 1 MiB. */
    CHECK_EQ(phys20(0xFFFF, 0x0010), 0x00000);
    CHECK_EQ(phys20(0xFFFF, 0x0020), 0x00010);
    CHECK_EQ(phys20(0xFFFF, 0xFFFF), 0xFFEF);   /* 0x10FFEF & 0xFFFFF */

    pc_t *pc = mkpc();

    /* Byte round-trip through a segment. */
    mem_write8(pc, 0x1234, 0x5678, 0xA5);
    CHECK_EQ(mem_read8(pc, 0x1234, 0x5678), 0xA5);
    CHECK_EQ(pc->mem[0x179B8], 0xA5);

    /* 16-bit little-endian. */
    mem_write16(pc, 0x2000, 0x0100, 0xBEEF);
    CHECK_EQ(mem_read8 (pc, 0x2000, 0x0100), 0xEF);
    CHECK_EQ(mem_read8 (pc, 0x2000, 0x0101), 0xBE);
    CHECK_EQ(mem_read16(pc, 0x2000, 0x0100), 0xBEEF);

    /* 32-bit little-endian. */
    mem_write32(pc, 0x2000, 0x0200, 0xDEADBEEF);
    CHECK_EQ(mem_read32(pc, 0x2000, 0x0200), 0xDEADBEEF);

    /* 16-bit read across the 1 MiB boundary wraps to address 0. */
    CHECK_EQ(phys20(0xFFFF, 0x000F), 0xFFFFF);
    mem_write8(pc, 0x0000, 0x0000, 0x11);
    mem_write8(pc, 0xFFFF, 0x000F, 0x22);   /* physical 0xFFFFF */
    CHECK_EQ(mem_read8(pc, 0xFFFF, 0x000F), 0x22);
    CHECK_EQ(mem_read16(pc, 0xFFFF, 0x000F), 0x1122);

    /* 32-bit read across the boundary. */
    mem_write8(pc, 0x0000, 0x0001, 0x33);
    mem_write8(pc, 0x0000, 0x0002, 0x44);
    CHECK_EQ(mem_read32(pc, 0xFFFF, 0x000F), 0x44331122u);

    /* Physical accessors mask to 20 bits. */
    mem_pwrite8(pc, 0x100000, 0x99);
    CHECK_EQ(mem_pread8(pc, 0x000000), 0x99);

    freepc(pc);
}
