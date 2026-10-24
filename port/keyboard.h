#ifndef KEYBOARD_H
#define KEYBOARD_H
#include <stdint.h>
#include "main.h"
void initKeyboard(void);
void getKeys(uint16_t *keys);
void getAnalogInput(int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry);
#endif
