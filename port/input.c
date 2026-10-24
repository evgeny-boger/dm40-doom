/*
 * DM40 input: the side keys and the touch screen as the gamepad bits of
 * boards.h. (First milestone: no input; the title screen and the demos run.)
 */
#include "keyboard.h"

void input_init(void) {}
uint16_t input_keys(void) { return 0; }

void initKeyboard(void) {}
void getKeys(uint16_t *keys) { *keys = input_keys(); }
void getAnalogInput(int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry)
{
    *lx = *ly = *rx = *ry = 0;
}
