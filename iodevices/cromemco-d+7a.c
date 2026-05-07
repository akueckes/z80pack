/*
 * cromemco-d+7a.c
 *
 * Emulation of the Cromemco D+7A I/O
 *
 * Copyright (C) 2020 by David McNaughton
 * Copyright (C) 2026 by Ansgar Kueckes
 *
 * History:
 * 14-JAN-2020	1.0	Initial Release
 * 15-MAR-2026		Added audio and joystick support
 * 19-APR-2026		Complete redesign of the audio emulation
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "sim.h"

#ifdef HAS_D7A

#include "simdefs.h"
#include "simglb.h"

#ifdef HAS_NETSERVER
#include "netsrv.h"
#endif
#include "audio.h"
#include "joystick.h"
#include "cromemco-d+7a.h"

// #define LOG_LOCAL_LEVEL LOG_DEBUG
#include "log.h"
static const char *TAG = "D+7A";

#define PORT_COUNT  8

#define DEFAULT_SAMPLE_RATE	22050		/* default audio sample rate in Hz */
#define DEFAULT_BUFFER_SIZE	64		/* default requested audio buffer size in samples per channel, defines audio delay */
//#define DEFAULT_SYNC_ADJUST	1.0247		/* default fine tuning for audio sync adjustment */
#define DEFAULT_RECORDING_LIMIT 10000000	/* size of wave buffer for file output & debug purposes */

/* parameters configurable in system.conf */
unsigned long d7a_sample_rate = DEFAULT_SAMPLE_RATE;
unsigned long d7a_buffer_size = DEFAULT_BUFFER_SIZE;
//double d7a_sync_adjust = DEFAULT_SYNC_ADJUST;
double d7a_sync_adjust = 0.0;
unsigned long d7a_recording_limit = DEFAULT_RECORDING_LIMIT;
char *d7a_soundfile = NULL;
int d7a_axis_map[2][65];
int d7a_button_map[2][65];
bool d7a_do_map_axis[2] = { false, false };
bool d7a_do_map_buttons[4] = { false, false, false, false };
bool d7a_stats = false;

Joystick joysticks[MAX_JOYSTICKS];

static BYTE inPort[PORT_COUNT];
static BYTE outPort[PORT_COUNT];

static int audio_device;

#ifdef HAS_NETSERVER
static void cromemco_d7a_callback(BYTE *data)
{
	int i;

	inPort[0] = *data++;
	for (i = 1; i < PORT_COUNT; i++)
		inPort[i] = (*data++) - 128;
}
#endif

/*
 *	Initialze Cromemco D+7A
 */
void cromemco_d7a_init(void)
{
	int i;

#ifdef HAS_NETSERVER
	if (n_flag)
		net_device_service(DEV_D7AIO, cromemco_d7a_callback);
#endif

    audio_device = audio_init(
    	&d7a_sample_rate,			/* requested sample rate in samples/s, returns actual sample rate */
    	&d7a_buffer_size,			/* requested audio buffer size, returns actual buffer size */
    	d7a_sync_adjust,			/* sync adjust (DAC only) */
    	d7a_stats,				/* output statistics for fine tuning (DAC only) */
    	d7a_soundfile,				/* wanted recording sound file */
    	d7a_recording_limit,			/* wanted recording limit in bytes */
    	NULL,					/* callback (NULL for using integrated DAC) */
    	NULL);					/* user data (NULL for using integrated DAC) */

    if (audio_device >= 0) {
    	LOG(TAG, "D+7A initialized and ready to use\n");
    }
    else {
    	LOGE(TAG, "Failed to initialize D+7A\n");
    }

    for (i=0; i<2; i++) {
	if (d7a_do_map_axis[i])
    		joysticks[i].axis_map = d7a_axis_map[i];
    	else
    		joysticks[i].axis_map = NULL;
    	if (d7a_do_map_buttons[i])
    		joysticks[i].button_map = d7a_button_map[i];
    	else
    		joysticks[i].button_map = NULL;
    }
    int num_joysticks = joystick_init(joysticks, d7a_stats);

    if (num_joysticks >= 0) {
    	LOG(TAG, "%d joystick(s) connected\n", num_joysticks);
    }	
}

/*
 *	Shutdown Cromemco D+/A
 */
void cromemco_d7a_off(void)
{
    audio_off(audio_device);
    joystick_off();
    
    if (d7a_soundfile) free(d7a_soundfile);
}

/*
 *	Process port data from CPU
 */
static void cromemco_d7a_out(BYTE port, BYTE data)
{
    outPort[port] = data;

    LOGD(TAG, "D+7A: Output %d on port %d", data, port);

#ifdef HAS_NETSERVER
    if (n_flag) {
	// if (net_device_alive(DEV_D7AIO)) {
	net_device_send(DEV_D7AIO, (char *) &data, 1);
	// }
    }
#endif

    /* assign port 1 to channel 0, and port 3 to channel 1 */
    switch(port) {
    	case 1: audio_record(audio_device, 0, data); break;
    	case 3: audio_record(audio_device, 1, data); break;
    	default: ;
    }
}

void cromemco_d7a_D_out (BYTE data) { cromemco_d7a_out(0, data); }
void cromemco_d7a_A1_out(BYTE data) { cromemco_d7a_out(1, data); }
void cromemco_d7a_A2_out(BYTE data) { cromemco_d7a_out(2, data); }
void cromemco_d7a_A3_out(BYTE data) { cromemco_d7a_out(3, data); }
void cromemco_d7a_A4_out(BYTE data) { cromemco_d7a_out(4, data); }
void cromemco_d7a_A5_out(BYTE data) { cromemco_d7a_out(5, data); }
void cromemco_d7a_A6_out(BYTE data) { cromemco_d7a_out(6, data); }
void cromemco_d7a_A7_out(BYTE data) { cromemco_d7a_out(7, data); }

static BYTE cromemco_d7a_in(BYTE port)
{
#ifdef HAS_NETSERVER
	if (n_flag)
		return inPort[port];
	else
#endif
	{
		joystick_query();
		switch(port) {
			case 0: return ~((joysticks[0].buttons & 0xf) | ((joysticks[1].buttons & 0xf) << 4));
			case 1: return joysticks[0].x_axis / 256;
			case 2: return joysticks[0].y_axis / 256;
			case 3: return joysticks[1].x_axis / 256;
			case 4: return joysticks[1].y_axis / 256;
			default: return inPort[port];
		}
	}
}

BYTE cromemco_d7a_D_in (void) { return cromemco_d7a_in(0); };
BYTE cromemco_d7a_A1_in(void) { return cromemco_d7a_in(1); };
BYTE cromemco_d7a_A2_in(void) { return cromemco_d7a_in(2); };
BYTE cromemco_d7a_A3_in(void) { return cromemco_d7a_in(3); };
BYTE cromemco_d7a_A4_in(void) { return cromemco_d7a_in(4); };
BYTE cromemco_d7a_A5_in(void) { return cromemco_d7a_in(5); };
BYTE cromemco_d7a_A6_in(void) { return cromemco_d7a_in(6); };
BYTE cromemco_d7a_A7_in(void) { return cromemco_d7a_in(7); };

#endif /* HAS_D7A */