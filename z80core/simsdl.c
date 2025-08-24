/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 2025 Thomas Eberhardt
 *
 * SDL joystick support added by Ansgar Kueckes
 */

/*
 *	This module contains the SDL2 integration for the simulator.
 */

#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>
#include <SDL_main.h>

#include "sim.h"
#include "simdefs.h"
#include "simsdl.h"
#include "simmain.h"

#ifdef FRONTPANEL
#include <SDL_image.h>
#include <SDL_mixer.h>
#endif

#define MAX_WINDOWS 5

static int sim_thread_func(void *data);

typedef struct args {
	int argc;
	char **argv;
} args_t;

typedef struct window {
	bool in_use;
	bool is_new;
	bool quit;
	win_funcs_t *funcs;
} window_t;

int sdl_num_joysticks = 0;
int sdl_joystick_0_x_axis = 0;
int sdl_joystick_0_y_axis = 0;
int sdl_joystick_1_x_axis = 0;
int sdl_joystick_1_y_axis = 0;
BYTE sdl_joystick_0_buttons = 0;
BYTE sdl_joystick_1_buttons = 0;

static window_t win[MAX_WINDOWS];
static bool sim_finished;	/* simulator thread finished flag */

int main(int argc, char *argv[])
{
	SDL_Event event;
	bool quit = false, tick;
	SDL_Thread *sim_thread;
	uint64_t t1, t2;
	int i, status;
	args_t args = {argc, argv};

	SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
		fprintf(stderr, "Can't initialize SDL: %s\n", SDL_GetError());
		return EXIT_FAILURE;
	}

	/* check for joysticks */
	sdl_num_joysticks = SDL_NumJoysticks();
	for (i=0; i<sdl_num_joysticks; i++) {
        	if (SDL_JoystickOpen(i) == NULL)
            		fprintf(stderr, "SDL: error reading joystick %d\n", i);
        }

#ifdef FRONTPANEL
	i = IMG_INIT_JPG | IMG_INIT_PNG;
	if ((IMG_Init(i) & i) == 0) {
		fprintf(stderr, "Can't initialize SDL_image: %s\n", IMG_GetError());
		SDL_Quit();
		return EXIT_FAILURE;
	}
	if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
		fprintf(stderr, "Can't initialize SDL_mixer: %s\n", Mix_GetError());
		IMG_Quit();
		SDL_Quit();
		return EXIT_FAILURE;
	}
#endif

	sim_finished = false;
	sim_thread = SDL_CreateThread(sim_thread_func, "Simulator", &args);
	if (sim_thread == NULL) {
		fprintf(stderr, "Can't create simulator thread: %s\n", SDL_GetError());
#ifdef FRONTPANEL
		Mix_CloseAudio();
		Mix_Quit();
		IMG_Quit();
#endif
		SDL_Quit();
		return EXIT_FAILURE;
	}

	tick = true;
	t1 = SDL_GetTicks64() + 1000;
	while (!quit) {
		/* process event queue */
		while (SDL_PollEvent(&event) != 0) {
			switch(event.type) {
			case SDL_JOYAXISMOTION:
				switch(event.jdevice.which) {
					case 0:
						switch(event.jaxis.axis) {
							case 0:
								sdl_joystick_0_x_axis = event.jaxis.value;
								break;
							case 1:
								sdl_joystick_0_y_axis = event.jaxis.value;
								break;
							default:;
						}
						break;
					case 1:
						switch(event.jaxis.axis) {
							case 0:
								sdl_joystick_1_x_axis = event.jaxis.value;
								break;
							case 1:
								sdl_joystick_1_y_axis = event.jaxis.value;
								break;
							default:;
						}
						break;
					default:;
				}
				break;
			case SDL_JOYHATMOTION:
				break;
			case SDL_JOYBUTTONDOWN:
				switch(event.jdevice.which) {
					case 0:
						sdl_joystick_0_buttons |= 1 << event.jbutton.button;
						break;
					case 1:
						sdl_joystick_1_buttons |= 1 << event.jbutton.button;
						break;
					default:;
				}	
				break;
			case SDL_JOYBUTTONUP:
				switch(event.jdevice.which) {
					case 0:
						sdl_joystick_0_buttons &= ~(1 << event.jbutton.button);
						break;
					case 1:
						sdl_joystick_1_buttons &= ~(1 << event.jbutton.button);
						break;
					default:;
				}	
				break;
			case SDL_JOYDEVICEADDED:
				break;
			case SDL_JOYDEVICEREMOVED:
				break;
			case SDL_QUIT:
				quit = true;
				break;
			default:;
			}

			for (i = 0; i < MAX_WINDOWS; i++)
				if (win[i].in_use)
					(*win[i].funcs->event)(&event);
		}

		/* open/close/draw windows */
		for (i = 0; i < MAX_WINDOWS; i++)
			if (win[i].in_use) {
				if (win[i].quit) {
					(*win[i].funcs->close)();
					win[i].in_use = false;
				} else {
					if (win[i].is_new) {
						(*win[i].funcs->open)();
						win[i].is_new = false;
					}
					(*win[i].funcs->draw)(tick);
				}
			}

		/* update seconds tick */
		t2 = SDL_GetTicks64();
		if ((tick = (t2 >= t1)))
			t1 = t2 + 1000;

		if (sim_finished)
			quit = true;
	}

	SDL_WaitThread(sim_thread, &status);

	for (i = 0; i < MAX_WINDOWS; i++)
		if (win[i].in_use)
			(*win[i].funcs->close)();

#ifdef FRONTPANEL
	Mix_CloseAudio();
	Mix_Quit();
	IMG_Quit();
#endif
	SDL_Quit();

	return status;
}

/* this is called from the simulator thread */
int simsdl_create(win_funcs_t *funcs)
{
	int i;

	for (i = 0; i < MAX_WINDOWS; i++)
		if (!win[i].in_use) {
			win[i].is_new = true;
			win[i].quit = false;
			win[i].funcs = funcs;
			win[i].in_use = true;
			break;
		}

	if (i == MAX_WINDOWS) {
		fprintf(stderr, "No more window slots left\n");
		i = -1;
	}

	return i;
}

/* this is called from the simulator thread */
void simsdl_destroy(int i)
{
	if (i >= 0 && i < MAX_WINDOWS)
		win[i].quit = true;
}

/* this thread runs the simulator */
static int sim_thread_func(void *data)
{
	args_t *args = (args_t *) data;
	int status;

	status = sim_main(args->argc, args->argv);
	sim_finished = true;

	return status;
}
