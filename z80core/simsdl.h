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

typedef struct client_funcs {
	void (*open)(void);
	void (*close)(void);
	void (*event)(SDL_Event *e);
	void (*draw)(bool tick);
} client_funcs_t;

extern int simsdl_create(client_funcs_t *funcs);
extern void simsdl_destroy(int i);

extern int sdl_num_joysticks;

#endif /* !SIMSDL_INC */
