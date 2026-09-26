/*
 * Munt386-CSE -- standalone host simulator for the CSE backend.
 *
 * Links firmware/cse/*.c (compiled with MUNT386_HW_EMULATE + MUNT386_CSE_SIM)
 * plus the portable core (CSE_CORE: no host-only backends), and runs
 * cse_startup() exactly as the Z80 firmware would: diagnostic self-tests,
 * then the guest boot loop.
 *
 * Sim-only services this file provides (never present on the device):
 *   - a timer feed (cse_timer_interrupt) so platform_time_us() advances
 *   - a guard on cse_startup() returning, so the harness can report results
 *   - CHECK/CHECK_EQ counters for the embedded test suites
 */
#include "munt386.h"
#include "cse_ports.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* CHECK/CHECK_EQ counters (normally provided by tests/test_main.c). */
int g_tests = 0;
int g_fails = 0;

int cse_startup(void);
void test_cse(void);

/* Timer feed: platform_time_us() derives from cse_timer_ticks(), which only
 * advances through the crystal-timer interrupt.  On the device that interrupt
 * comes from the im-2 vector table; in the simulator we drive it from a
 * background thread at CSE_TICK_HZ_MIN. */
#ifdef MUNT386_CSE_SIM
#define SIM_HAVE_TIMER_THREAD 1
#endif

#ifdef SIM_HAVE_TIMER_THREAD
#include <pthread.h>
#include <unistd.h>

static void *sim_timer_thread(void *arg)
{
    (void)arg;
    for (;;) {
        cse_timer_interrupt();
        usleep(1000000 / 100);          /* 100 Hz, matches CSE_TICK_HZ_MIN */
    }
    return NULL;
}

/* Harness hook invoked by startup.c's CSE_PARK(reason) sites.  On the
 * device the park is a permanent halt; here we report the reason and hand
 * control back.  "GUEST HALTED" is the success park; anything else means a
 * self-test or memory failure stopped the firmware. */
void cse_sim_park(const char *reason)
{
    int ok = reason && !strcmp(reason, "GUEST HALTED");
    printf("CSE SIM: park: %s (checks=%d failures=%d)\n",
           reason ? reason : "(null)", g_tests, g_fails);
    exit(ok ? 0 : 3);
}
#endif

int main(void)
{
    int rc = 0;
    printf("Munt386 CSE simulator (firmware/cse backend, simulated hardware)\n");

    /* 1. The CSE backend unit suites (keymap, LCD, disk, paged memory,
     *    platform wiring, whole-boot through the paged backend). */
    printf("[cse]\n");
    test_cse();
    printf("  %d checks\n", g_tests);

    /* 2. The firmware boot simulation: diagnostic self-tests + guest run,
     *    exactly as cse_startup() drives them on the device.  Park sites
     *    inside cse_startup() exit via cse_sim_park() with rc != 0 on
     *    failure, 0 when the guest runs to its halt park. */
#ifdef SIM_HAVE_TIMER_THREAD
    pthread_t th;
    pthread_create(&th, NULL, sim_timer_thread, NULL);
    /* Let the timer settle so startup's test_timer() sees progress
     * deterministically (on the device the crystal timer is always live). */
    usleep(150000);
#endif
    rc = cse_startup();

    printf("cse_startup returned %d\n", rc);
    printf("CSE SIM: %d checks, %d failures\n", g_tests, g_fails);
    return (rc != 0 || g_fails) ? 1 : 0;
}
