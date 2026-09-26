/*
 * Munt386-CSE -- interface between the CSE backend and the startup.
 */
#ifndef CSE_MAIN_H
#define CSE_MAIN_H

#include <stdint.h>

/* MUNT386_DEVICE marks the bare-metal Z80 build (SDCC).  It is distinct from
 * MUNT386_CSE, which also covers the host simulator that exercises the same
 * backend sources with emulated hardware. */
#ifdef MUNT386_DEVICE
#define MUNT386_CSE 1
#endif

struct pc;

/* Startup -> backend: register the diagnostic line renderer so
 * platform_debug_print() reaches the LCD. */
void cse_platform_set_diag(void (*fn)(const char *msg));

/* Backend -> startup: provided by cse_video.c (one LCD text line). */
void cse_video_print(const char *msg);

/* Backend: soft reboot trampoline (no flash writes). */
void cse_reboot(void);

/* Startup: C entry point called from startup.s after Z80 bring-up. */
int  cse_startup(void);

#endif /* CSE_MAIN_H */
