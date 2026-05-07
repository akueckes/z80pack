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

typedef struct client_funcs2 {
	void (*open)(int handle);
	void (*close)(int handle);
	void (*event)(int handle, SDL_Event *e);
	void (*draw)(int handle, bool tick);
} client_funcs2_t;

extern int simsdl_create(client_funcs_t *funcs);
extern int simsdl_create2(int handle, client_funcs2_t *funcs);
extern void simsdl_destroy(int i);

extern int sdl_num_joysticks;

#endif /* !SIMSDL_INC */
