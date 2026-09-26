/* Munt386 -- protected-mode test suite.
 *
 * Status: PROTECTED MODE IS NOT IMPLEMENTED YET (see docs/ROADMAP.md, Phase 3).
 * The register file already carries CR0/CR2/CR3/GDTR/IDTR, and the CPU core is
 * structured for 32-bit growth, but descriptor parsing, segment translation and
 * privilege checks are future work.  This suite intentionally contains only the
 * invariants that hold today so it does not report false passes; it will be
 * expanded as Phase 3 lands.
 */
#include "../test_util.h"

void test_protected(void);

void test_protected(void)
{
    /* System registers exist and start cleared. */
    pc_t *pc = mkpc();
    CHECK_EQ(pc->cpu.cr0, 0);
    CHECK_EQ(pc->cpu.cr2, 0);
    CHECK_EQ(pc->cpu.cr3, 0);
    CHECK_EQ(pc->cpu.gdtr_base, 0);
    CHECK_EQ(pc->cpu.idtr_limit, 0);
    /* CR0.PE is not yet honoured: document that the CPU runs in real mode. */
    CHECK(!(pc->cpu.cr0 & 1));
    freepc(pc);
}
