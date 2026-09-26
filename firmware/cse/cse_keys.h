/*
 * Munt386-CSE -- keypad scan/translation interface.
 */
#ifndef CSE_KEYS_H
#define CSE_KEYS_H

#include <stdint.h>

void cse_keys_reset(void);
void cse_keys_attach(struct pc *pc);
void cse_keys_set_emit(void (*fn)(void *ud, uint8_t sc), void *ud);
void cse_keys_scan(void);
int  cse_keys_on_held(void);

#endif /* CSE_KEYS_H */
