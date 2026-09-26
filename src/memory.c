/*
 * Munt386 -- virtual PC memory subsystem (addressing layer).
 *
 * The guest sees real-mode 20-bit physical addresses formed as
 * (segment << 4) + offset, masked to 20 bits so addresses wrap exactly like
 * real x86 (FFFF:0010 -> 0000:0000).  A 32-bit access that straddles the
 * 1 MiB boundary wraps too.
 *
 * The PHYSICAL backing is swappable: the host links src/memflat.c (one flat
 * allocation); the CSE links firmware/cse/cse_mem.c (banked page cache).
 * This file never touches the backing pointer directly, only through the
 * mem_pread / mem_pwrite interface both backends implement.
 */
#include "munt386.h"
#include <string.h>

uint32_t phys20(uint16_t seg, uint16_t off)
{
    return (((uint32_t)seg << 4) + (uint32_t)off) & X86_PHYS_MASK;
}

uint8_t mem_read8(pc_t *pc, uint16_t seg, uint16_t off)
{
    return mem_pread8(pc, phys20(seg, off));
}

uint16_t mem_read16(pc_t *pc, uint16_t seg, uint16_t off)
{
    return mem_pread16(pc, phys20(seg, off));
}

uint32_t mem_read32(pc_t *pc, uint16_t seg, uint16_t off)
{
    return mem_pread32(pc, phys20(seg, off));
}

void mem_write8(pc_t *pc, uint16_t seg, uint16_t off, uint8_t v)
{
    mem_pwrite8(pc, phys20(seg, off), v);
}

void mem_write16(pc_t *pc, uint16_t seg, uint16_t off, uint16_t v)
{
    mem_pwrite16(pc, phys20(seg, off), v);
}

void mem_write32(pc_t *pc, uint16_t seg, uint16_t off, uint32_t v)
{
    mem_pwrite32(pc, phys20(seg, off), v);
}

/* ------------------------------------------------------------------ */
/* Linear-address accessors                                            */
/*                                                                       */
/* Linear == physical while CR0.PG is clear.  With paging enabled the     */
/* address is translated through CR3; a page fault raises #PF(14).       */
/* ------------------------------------------------------------------ */

uint8_t mem_lread8(pc_t *pc, uint32_t lin)
{
    if (!(pc->cpu.cr0 & CR0_PG)) return mem_pread8(pc, lin);
    uint32_t err;
    uint32_t phys = paging_translate(pc, lin, 0, 0, &err);
    if (phys == 0xFFFFFFFFu) {
        pc->cpu.cr2 = lin;
        cpu_raise_exception(pc, EXC_PF, err | 0u);
        return 0;
    }
    return mem_pread8(pc, phys);
}

uint16_t mem_lread16(pc_t *pc, uint32_t lin)
{
    if (!(pc->cpu.cr0 & CR0_PG)) return mem_pread16(pc, lin & 0xFFFFFFFEu);
    uint32_t err;
    uint32_t phys = paging_translate(pc, lin, 0, 0, &err);
    if (phys == 0xFFFFFFFFu) {
        pc->cpu.cr2 = lin;
        cpu_raise_exception(pc, EXC_PF, err);
        return 0;
    }
    if ((lin & 0xFFF) != 0xFFF) return mem_pread16(pc, phys);
    /* Split access across two pages. */
    uint16_t lo = mem_lread8(pc, lin);
    uint16_t hi = mem_lread8(pc, (lin & ~0xFFFu) + 0x1000u);
    return lo | (uint16_t)(hi << 8);
}

uint32_t mem_lread32(pc_t *pc, uint32_t lin)
{
    if (!(pc->cpu.cr0 & CR0_PG)) return mem_pread32(pc, lin);
    uint32_t err;
    uint32_t phys = paging_translate(pc, lin, 0, 0, &err);
    if (phys == 0xFFFFFFFFu) {
        pc->cpu.cr2 = lin;
        cpu_raise_exception(pc, EXC_PF, err);
        return 0;
    }
    if ((lin & 0xFFF) <= 0xFFC) return mem_pread32(pc, phys);
    uint32_t r = 0;
    for (int i = 0; i < 4; i++)
        r |= (uint32_t)mem_lread8(pc, lin + (uint32_t)i) << (8 * i);
    return r;
}

void mem_lwrite8(pc_t *pc, uint32_t lin, uint8_t v)
{
    if (!(pc->cpu.cr0 & CR0_PG)) { mem_pwrite8(pc, lin, v); return; }
    uint32_t err;
    uint32_t phys = paging_translate(pc, lin, 1, 0, &err);
    if (phys == 0xFFFFFFFFu) {
        pc->cpu.cr2 = lin;
        cpu_raise_exception(pc, EXC_PF, err | 2u);
        return;
    }
    mem_pwrite8(pc, phys, v);
}

void mem_lwrite16(pc_t *pc, uint32_t lin, uint16_t v)
{
    if (!(pc->cpu.cr0 & CR0_PG)) { mem_pwrite16(pc, lin & 0xFFFFFFFEu, v); return; }
    uint32_t err;
    uint32_t phys = paging_translate(pc, lin, 1, 0, &err);
    if (phys == 0xFFFFFFFFu) {
        pc->cpu.cr2 = lin;
        cpu_raise_exception(pc, EXC_PF, err | 2u);
        return;
    }
    if ((lin & 0xFFF) != 0xFFF) { mem_pwrite16(pc, phys, v); return; }
    mem_lwrite8(pc, lin, (uint8_t)(v & 0xFF));
    mem_lwrite8(pc, (lin & ~0xFFFu) + 0x1000u, (uint8_t)(v >> 8));
}

void mem_lwrite32(pc_t *pc, uint32_t lin, uint32_t v)
{
    if (!(pc->cpu.cr0 & CR0_PG)) { mem_pwrite32(pc, lin, v); return; }
    uint32_t err;
    uint32_t phys = paging_translate(pc, lin, 1, 0, &err);
    if (phys == 0xFFFFFFFFu) {
        pc->cpu.cr2 = lin;
        cpu_raise_exception(pc, EXC_PF, err | 2u);
        return;
    }
    if ((lin & 0xFFF) <= 0xFFC) { mem_pwrite32(pc, phys, v); return; }
    for (int i = 0; i < 4; i++)
        mem_lwrite8(pc, lin + (uint32_t)i, (uint8_t)(v >> (8 * i)));
}
