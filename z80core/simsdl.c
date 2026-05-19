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

#define MAX_CLIENTS 8

static int sim_thread_func(void *data);

typedef struct args {
	int argc;
	char **argv;
} args_t;

typedef struct client {
	bool in_use;
	bool is_new;
	bool quit;
	client_funcs_t *funcs;
	client_funcs2_t *funcs2;
} client_t;

static client_t client[MAX_CLIENTS];
static int handle_map[MAX_CLIENTS];
static bool sim_finished;	/* simulator thread finished flag */

int sdl_num_joysticks = 0;

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

/* Global or localized audio pointers for context */
Mix_Music *myMusic = NULL;

int main(int argc, char *argv[])
{
	SDL_Event event;
	bool quit = false, tick;
	SDL_Thread *sim_thread;
	uint64_t t1, t2;
	int i, status;
	args_t args = {argc, argv};
	
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0) {
		fprintf(stderr, "Can't initialize SDL: %s\n", SDL_GetError());
		return EXIT_FAILURE;
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

	for (i=0; i<MAX_CLIENTS; i++) {
		client[i].in_use = false;
		handle_map[i] = -1;
	}

	tick = true;
	t1 = SDL_GetTicks64() + 1000;
	while (!quit) {
		if (sim_finished) quit = true;

		/* process event queue */
		while (SDL_PollEvent(&event) != 0) {
			switch(event.type) {
			case SDL_QUIT:
				quit = true;
				break;
			default:
				/* event etc. */
				for (i = 0; i < MAX_CLIENTS; i++)
					if (client[i].in_use) {
						if (handle_map[i] < 0) {
							if (client[i].funcs->event) (*client[i].funcs->event)(&event);
						}
						else {
							if (client[i].funcs2->event) (*client[i].funcs2->event)(handle_map[i], &event);
						}
					}
			}
		}

		/* open/close/draw clients/windows */
		for (i = 0; i < MAX_CLIENTS; i++)
			if (client[i].in_use) {
				if (client[i].quit) {
					if (handle_map[i] < 0) {				
						if (client[i].funcs->close) (*client[i].funcs->close)();
					}
					else {
						if (client[i].funcs2->close) (*client[i].funcs2->close)(handle_map[i]);
					}
					client[i].in_use = false;
					handle_map[i] = -1;
				} else {
					if (client[i].is_new) {
						if (handle_map[i] < 0) {				
							if (client[i].funcs->open) (*client[i].funcs->open)();
						}
						else {
							if (client[i].funcs2->open) (*client[i].funcs2->open)(handle_map[i]);
						}
						client[i].is_new = false;
					}
					if (handle_map[i] < 0) {				
						if (client[i].funcs->draw) (*client[i].funcs->draw)(tick);
					}
					else {
						if (client[i].funcs2->draw) (*client[i].funcs2->draw)(handle_map[i], tick);
					}
				}
			}

		/* update seconds tick */
		t2 = SDL_GetTicks64();
		if ((tick = (t2 >= t1)))
			t1 = t2 + 1000;
	}

	SDL_WaitThread(sim_thread, &status);

	for (i = 0; i < MAX_CLIENTS; i++)
		if (client[i].in_use) {
			if (handle_map[i] < 0) {				
				if (client[i].funcs->close) (*client[i].funcs->close)();
			}
			else {
				if (client[i].funcs2->close) (*client[i].funcs2->close)(handle_map[i]);
			}
		}

#ifdef FRONTPANEL
	Mix_CloseAudio();
	Mix_Quit();
	IMG_Quit();
#endif
	SDL_Quit();

	return status;
}

/* this is called from the simulator thread */
int simsdl_create(client_funcs_t *funcs)
{
	int i;

	for (i = 0; i < MAX_CLIENTS; i++)
		if (!client[i].in_use) {
			client[i].is_new = true;
			client[i].quit = false;
			client[i].funcs = funcs;
			client[i].in_use = true;
			break;
		}

	if (i == MAX_CLIENTS) {
		fprintf(stderr, "No more client slots left\n");
		i = -1;
	}

	return i;
}

int simsdl_create2(int handle, client_funcs2_t *funcs)
{
	int i;

	for (i = 0; i < MAX_CLIENTS; i++)
		if (!client[i].in_use) {
			client[i].is_new = true;
			client[i].quit = false;
			client[i].funcs2 = funcs;
			client[i].in_use = true;
			handle_map[i] = handle;
			break;
		}

	if (i == MAX_CLIENTS) {
		fprintf(stderr, "No more client slots left\n");
		i = -1;
	}

	return i;
}


/* this is called from the simulator thread */
void simsdl_destroy(int i)
{
	if (i >= 0 && i < MAX_CLIENTS)
		client[i].quit = true;
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
