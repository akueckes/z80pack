/*
 * newtech-musicboard.c
 *
 * Emulation of the Newtech Model 6 Musicboard
 *
 * Copyright (C) 2026 by Ansgar Kueckes
 *
 * History:
 * 22-MAR-2026	1.0	Initial Release
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "sim.h"

#ifdef HAS_MUSICBOARD

#include "simdefs.h"
#include "simglb.h"

#ifdef HAS_NETSERVER
#include "netsrv.h"
#endif
#include "audio.h"
#include "newtech-musicboard.h"

// #define LOG_LOCAL_LEVEL LOG_DEBUG
#include "log.h"
static const char *TAG = "MUISCBOARD";

#define PORT_COUNT  2

#define DEFAULT_SAMPLE_RATE	22050		/* requested audio sample rate in Hz */
#define DEFAULT_BUFFER_SIZE	64		/* requested audio buffer size in samples per channel, defines audio delay */
//#define DEFAULT_SYNC_ADJUST	1.0247		/* default fine tuning for audio sync adjustment */
#define DEFAULT_RECORDING_LIMIT 10000000	/* size of wave buffer for file output & debug purposes */

/* parameters configurable in system.conf */
unsigned long musicboard_sample_rate = DEFAULT_SAMPLE_RATE;
unsigned long musicboard_buffer_size = DEFAULT_BUFFER_SIZE;
//double musicboard_sync_adjust = DEFAULT_SYNC_ADJUST;
double musicboard_sync_adjust = 0.0;
unsigned long musicboard_recording_limit = DEFAULT_RECORDING_LIMIT;
char *musicboard_soundfile = NULL;
bool musicboard_stats = false;

static BYTE inPort[PORT_COUNT];
static BYTE outPort[PORT_COUNT];

static int audio_device;

#ifdef HAS_NETSERVER
static void newtech_musicboard_callback(BYTE *data)
{
	int i;

	inPort[0] = *data++;
	for (i = 1; i < PORT_COUNT; i++)
		inPort[i] = (*data++) - 128;
}
#endif

/*
 *	Initialize Newtech Music Board
 */
void newtech_musicboard_init(void)
{
#ifdef HAS_NETSERVER
	if (n_flag)
		net_device_service(DEV_NMB, newtech_musicboard_callback);
#endif

    audio_device = audio_init(
    	&musicboard_sample_rate,		/* requested sample rate in samples/s, returns actual sample rate */
    	&musicboard_buffer_size,		/* requested audio buffer size, returns actual buffer size */
    	musicboard_sync_adjust,			/* sync adjust (DAC only) */
    	musicboard_stats,			/* output statistics for fine tuning (DAC only) */
    	musicboard_soundfile,			/* wanted recording sound file */
    	musicboard_recording_limit,		/* wanted recording limit in bytes */
    	NULL,					/* callback (NULL for using integrated DAC) */
    	NULL);					/* user data (NULL for using integrated DAC) */

    if (audio_device >= 0) {
    	LOG(TAG, "Music Board initialized and ready to use\n");
    }
    else {
    	LOGE(TAG, "Failed to initialize Music Board\n");
    }
}

/*
 *	Shutdown Newtech Music Board
 */
void newtech_musicboard_off(void)
{
    audio_off(audio_device);
    
    if (musicboard_soundfile) free(musicboard_soundfile);
}

/*
 *	Process port data from CPU
 */
static void newtech_musicboard_out(BYTE port, BYTE data)
{
    outPort[port] = data;

    LOGD(TAG, "Musicboard: Output %d on port %d", data, port);

#ifdef HAS_NETSERVER
    if (n_flag) {
	// if (net_device_alive(DEV_MUSICBOARD)) {
	net_device_send(DEV_NMB, (char *) &data, 1);
	// }
    }
#endif

    /* assign port 1 to channel 0, and port 3 to channel 1 */
    switch(port) {
    	case 1: audio_record(audio_device, 0, data - 128); break;
    	case 2: audio_record(audio_device, 1, data - 128); break;
    	default: ;
    }
}

/* Newtech Model 6 Music Board ports */
void newtech_musicboard1_out(BYTE data) { newtech_musicboard_out(1, data); }
void newtech_musicboard2_out(BYTE data) { newtech_musicboard_out(2, data); }

#endif /* HAS_MUISCBOARD */