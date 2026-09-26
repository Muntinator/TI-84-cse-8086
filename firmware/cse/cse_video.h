/*
 * Munt386-CSE -- CSE display backend interface.
 */
#ifndef CSE_VIDEO_H
#define CSE_VIDEO_H

#include <stdint.h>
#include "cse_hardware.h"

#define CSE_DIAG_COLS 20
#define CSE_DIAG_ROWS 15

int      cse_video_init(void);
void     cse_video_shutdown(void);
void     cse_video_set_pixel(int x, int y, uint16_t rgb565);
uint16_t cse_video_get_pixel(int x, int y);
int      cse_video_blit(const uint16_t *src);
/* Convert + present an abstract RGB888 framebuffer (the virtual VGA's). */
int      cse_video_update(const uint8_t *fb, uint32_t w, uint32_t h);
/* Render one diagnostic text line and present the panel. */
void     cse_video_print_line(const char *msg);

uint16_t *cse_video_framebuffer(void);
int       cse_video_ready(void);

#endif /* CSE_VIDEO_H */
