/*
 * Munt386-CSE -- CSE hardware access layer interface.
 */
#ifndef CSE_PORTS_H
#define CSE_PORTS_H

#include <stdint.h>

/* Port I/O primitives (real or simulated, depending on the build). */
uint8_t cse_port_in(uint8_t port);
void    cse_port_out(uint8_t port, uint8_t value);

/* LCD register protocol (index written twice, then 16-bit data H,L). */
void cse_lcd_write_reg(uint8_t reg, uint16_t value);

/* Keypad scan primitives. */
void    cse_keypad_start(void);
uint8_t cse_keypad_read_group(uint8_t group_mask);
void    cse_keypad_end(void);

/* Timer tick fed by the interrupt path; microsecond conversion. */
void     cse_timer_interrupt(void);
uint32_t cse_timer_ticks(void);

/* Boot-time helpers. */
void cse_hw_early_init(void);
void cse_backlight(int on);

/* --- host-test simulator hooks (MUNT386_HW_EMULATE builds only) ------- */
void     cse_sim_reset(void);
void     cse_sim_key_event(int group_index, uint8_t col_bit);
uint16_t cse_sim_last_pixel(uint32_t index);

#endif /* CSE_PORTS_H */
