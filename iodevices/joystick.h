/**
 * audio.h
 *
 * Emulation of game controllers used in multiple devices
 *
 * Copyright (C) 2026 by Ansgar Kueckes
 *
 * History:
 * 14-MAR-2026	1.0	Initial Release
 */

#ifndef JOYSTICK_INC
#define JOYSTICK_INC

#include "sim.h"
#include "simdefs.h"

#define MAX_JOYSTICKS	4	/* max number of joystick devices */

typedef struct {
    int16_t x_axis;		/* analog x-axis -32768 to 32767 */
    int16_t y_axis;		/* analog y-axis -32768 to 32767 */
    uint8_t buttons;		/* up to 8 buttons, 1 = button pressed, 0 = button released */
    int *axis_map;		/* optional axis mapping */
    int *button_map;		/* optional button mapping */
} Joystick;

extern int joystick_init(Joystick *joysticks, int stats);
extern void joystick_off(void);
extern void joystick_query();

#endif /* !JOYSTICK_INC */
