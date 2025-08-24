/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 2025 Thomas Eberhardt
 */

#ifndef SIMSDL_INC
#define SIMSDL_INC

#include <SDL.h>

#include "sim.h"
#include "simdefs.h"

typedef struct win_funcs {
	void (*open)(void);
	void (*close)(void);
	void (*event)(SDL_Event *e);
	void (*draw)(bool tick);
} win_funcs_t;

extern int simsdl_create(win_funcs_t *funcs);
extern void simsdl_destroy(int i);

extern int sdl_num_joysticks;
extern int sdl_joystick_0_x_axis;
extern int sdl_joystick_0_y_axis;
extern int sdl_joystick_1_x_axis;
extern int sdl_joystick_1_y_axis;
extern BYTE sdl_joystick_0_buttons;
extern BYTE sdl_joystick_1_buttons;

#endif /* !SIMSDL_INC */
