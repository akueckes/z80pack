/*
 * audio_dac.c
 *
 * Emulation of game controllers used in multiple devices
 *
 * Copyright (C) 2026 by Ansgar Kueckes
 *
 * Note: Optional axis & button re-mapping is currently implemented via the
 *       mapping function of the joystick driver, and therefore not available
 *       with SDL. The button IDs used for the mapping are those defined
 *       in input-event-codes.h in the Linux kernel source tree.
 *
 * If stats are enabled, the joysticks found are identified with their names,
 * number of axis, number of buttons and current event mappings during start-up.
 *
 * You can define your own mapping in the system.conf file with the matching
 * property:
 *
 *   <property-name> <joystick-id> <mapping>
 *
 *   where
 *
 *       <property-name> is the name for the device property (e.g. d7a_axis_map or
 *       d7a_button_map)
 *
 *       <joystick-id> is the ID of the controller device which shall be re-mapped
 *       (1 for the first controller, 2 for the second, and so on)
 *
 *       <mapping> is a comma separated sequence of numbers, describing the new
 *	 mapping for the device.
 *
 *  Example with 4 axis and 8 buttons:
 *
 *       d7a_axis_map	1  1,0,2,3,4
 *       d7a_button_map	1  1,0,2,3,4,5,6,7,8
 *
 *   swaps axis 0 and 1 as well as button 0 and 1.
 *
 * History:
 * 14-MAR-2026	1.0	Initial Release
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "sim.h"
#include "simdefs.h"
#include "simglb.h"

#include "joystick.h"

// #define LOG_LOCAL_LEVEL LOG_DEBUG
#include "log.h"
static const char *TAG = "JOYSTICK";

#ifdef WANT_SDL
#include <SDL.h>
#include "simsdl.h"
#else
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/joystick.h>
#endif

static int num_joysticks = 0;			/* actual number of joysticks */
static Joystick *joystick = NULL;

#ifndef WANT_SDL
static int joystick_fd[MAX_JOYSTICKS];
#endif

static uint8_t joystick_axis_map[MAX_JOYSTICKS][ABS_CNT];
static uint16_t joystick_button_map[MAX_JOYSTICKS][KEY_MAX - BTN_MISC + 1];


#ifdef WANT_SDL
static int client_id = -1;

/* SDL2 joystick event handler */
static void sdl_process_event(SDL_Event *event)
{
	if (joystick == NULL) return;

	switch(event->type) {
		case SDL_JOYAXISMOTION:
			switch(event->jaxis.axis) {
			case 0:
				joystick[event->jdevice.which].x_axis = event->jaxis.value;
				break;
			case 1:
				joystick[event->jdevice.which].y_axis = -event->jaxis.value;
				break;
			default:;
			}
			break;
		case SDL_JOYHATMOTION:
			break;
		case SDL_JOYBUTTONDOWN:
			if (event->jbutton.button < 8)
				joystick[event->jdevice.which].buttons |= (1 << event->jbutton.button);
			break;
		case SDL_JOYBUTTONUP:
			if (event->jbutton.button < 8)
				joystick[event->jdevice.which].buttons &= ~(1 << event->jbutton.button);
			break;
		case SDL_JOYDEVICEADDED:
			// TODO
			break;
		case SDL_JOYDEVICEREMOVED:
			// TODO
			break;
		default:;
	}
}

static client_funcs_t joystick_funcs = {
	NULL,
	NULL,
	sdl_process_event,
	NULL
};

#endif	/* WANT_SDL */
/*
    Initialize joystick
    
    Set stats to non-zero for some statistics output
    
    Returns number of joysticks found or negative value in case of error
*/
int joystick_init(Joystick *joysticks, int stats)
{
    int i;
    
    if (joysticks == NULL) return 0;

    joystick = joysticks;

    /* initialize joysticks status */
    for(i=0; i<MAX_JOYSTICKS; i++) {
    	joysticks[i].x_axis = joysticks[i].y_axis = joysticks[i].buttons = 0;
    }

#ifdef WANT_SDL
    /* unused parameters */
    (void) stats;

    client_id = simsdl_create(&joystick_funcs);

    /* check for joysticks */
    num_joysticks = SDL_NumJoysticks();
    if (num_joysticks > MAX_JOYSTICKS) num_joysticks = MAX_JOYSTICKS;
    for (i=0; i<num_joysticks; i++) {
	if (joystick[i].axis_map) LOGW(TAG, "Axis remapping not supported with SDL2\n");
	if (joystick[i].button_map) LOGW(TAG, "Button remapping not supported with SDL2\n");
	if (SDL_JoystickOpen(i) == NULL) {
    		LOGE(TAG, "SDL: error reading joystick %d\n", i);
    		return -1;
    	}
    }
#else	/* !WANT_SDL */
    int j, fd;
    char dev_path[] = "/dev/input/js0";
    char buf[256] = "Unknown controller";
    uint8_t num_axis, num_buttons;
    uint8_t my_axis_map[ABS_CNT];
    uint16_t my_button_map[KEY_MAX - BTN_MISC + 1];
        
    /* scan /dev/input/js* for joysticks */
    num_joysticks = 0;
    for (j=0; j<MAX_JOYSTICKS; j++) {
    	dev_path[13] = j + 48;
	fd = open(dev_path, O_RDONLY | O_NONBLOCK);
	if (fd < 0) continue;
	ioctl(fd, JSIOCGAXES, &num_axis);
	ioctl(fd, JSIOCGBUTTONS, &num_buttons);
	/* apply axis & button mappings */
	if (joystick[j].axis_map) {
		/* get current mapping */
		ioctl(fd, JSIOCGAXMAP, &joystick_axis_map[j]);
		memcpy(&my_axis_map, &joystick_axis_map[j], num_axis * sizeof(uint8_t));
		LOG(TAG, "Source %d axis mapping: ", j+1);
		for (i=0; i<num_axis; i++) {
			if (joystick[j].axis_map[i] < 0) break;
			LOG(TAG, "%d,",  joystick[j].axis_map[i]);
			my_axis_map[i] = joystick_axis_map[j][joystick[j].axis_map[i]];
		}
		LOG(TAG, "\n");
		ioctl(fd, JSIOCSAXMAP, my_axis_map);
	}
	if (joystick[j].button_map) {
		ioctl(fd, JSIOCGBTNMAP, &joystick_button_map[j]);
		memcpy(&my_button_map, &joystick_button_map[j], num_buttons * sizeof(uint16_t));
		LOG(TAG, "Source %d button mapping: ", j+1);
		for (i=0; i<num_buttons; i++) {
			if (joystick[j].button_map[i] < 0) break;
			LOG(TAG, "%d,",  joystick[j].button_map[i]);
			my_button_map[i] = joystick_button_map[j][joystick[j].button_map[i]];
		}
		LOG(TAG, "\n");
		ioctl(fd, JSIOCSBTNMAP, my_button_map);
	}
	if (stats) {
		ioctl(fd, JSIOCGNAME(sizeof(buf)), buf);
		LOG(TAG, "Joystick %d found \"%s\" (%d axis, %d buttons)\n", j+1, buf, num_axis, num_buttons);

		ioctl(fd, JSIOCGAXMAP, my_axis_map);
		LOG(TAG, "Joystick %d axis mapping: ", j+1);
		for (i=0; i<num_axis; i++)
			LOG(TAG, "%d,", my_axis_map[i]);
		LOG(TAG, "\b \b\n");
		ioctl(fd, JSIOCGBTNMAP, my_button_map);
		LOG(TAG, "Joystick %d button mapping: ", j+1);
		for (i=0; i<num_buttons; i++)
			LOG(TAG, "%d,", my_button_map[i]);
		LOG(TAG, "\b \b\n");
	}

	joystick_fd[num_joysticks++] = fd;
    }
#endif
    return num_joysticks;
}

void joystick_off(void)
{
#ifndef WANT_SDL
	for (int i=0; i<num_joysticks; i++) {
		/* restore mappings */
		if (joystick[i].axis_map)
			ioctl(joystick_fd[i], JSIOCSAXMAP, joystick_axis_map[i]);
		if (joystick[i].button_map)
			ioctl(joystick_fd[i], JSIOCSBTNMAP, joystick_button_map[i]);

		/* close device */
		if (joystick_fd[i] >= 0) close(joystick_fd[i]);
	}
#endif
    joystick = NULL;
}

/*
    Get current status for all joysticks
    
    Returns number of connected joysticks
*/
void joystick_query()
{
    if (joystick == NULL) return;

    /* nothing to do for SDL (status will be updated automatically/event driven) */
#ifndef WANT_SDL
    /* query event device */
    struct js_event event;
    int i, count;
    bool quit = false;

    if (num_joysticks > 0) {
	while (!quit) {
		for (i=0; i<num_joysticks; i++) {
			count = read(joystick_fd[i], &event, sizeof(event));
			if (count < 0) {
		            if (errno == EAGAIN || errno == EWOULDBLOCK) {
		                // No data available
		                quit = true;
		                break;
		            } else if (errno == ENODEV) {
		            	LOGE(TAG, "Joystick: Controller not found\r\n");
		            	quit = true;
		            	break;
		            } else {
		                // Other read error
		                LOGE(TAG, "Joystick: Read error: %s (%d)\r\n", strerror(errno), errno);
		                quit = true;
		                break;
		            }
		        }
			if (count == sizeof(event)) {
				switch(event.type) {
		                case JS_EVENT_BUTTON:
		                    if (event.number < 8) {
			                    if (event.value) joystick[i].buttons |= 1 << event.number;
			                    else joystick[i].buttons &= ~(1 << event.number);
			            }
		                    break;
		                case JS_EVENT_AXIS:
		                    if (event.number == ABS_X) joystick[i].x_axis = event.value;
		                    else if (event.number == ABS_Y) joystick[i].y_axis = -event.value;
		                    break;
		                case JS_EVENT_INIT:
		                    break;
		                default:;
		                }
		        }
		}
	}
    }
#endif
}