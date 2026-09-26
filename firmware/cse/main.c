/*
 * Munt386-CSE -- bare-metal device entry.
 *
 * On the device there is no host process: the Z80 reset vector in startup.s
 * performs DI/IM 1, bank/speed/stack bring-up and calls _cse_startup.  The
 * SDCC runtime (crt0) also expects a C main(), so on the device main() is a
 * shim that keeps interrupts disabled (matching the bootstrap's assumption)
 * and calls the same entry point exactly once.
 *
 * The host simulator builds firmware/cse/startup.c directly and never links
 * this file.
 */
#include "cse_main.h"

int main(void)
{
    /* Interrupts are already disabled by the bootstrap (di, im 1).  The
     * firmware runs single-threaded bare metal; they stay off. */
    __asm__("di");
    (void)cse_startup();
    /* cse_startup() parks internally; this line is unreachable. */
    return 0;
}
