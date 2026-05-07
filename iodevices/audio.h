/**
 * audio.h
 *
 * Emulation of audio used in multiple devices
 *
 * Copyright (C) 2026 by Ansgar Kueckes
 *
 * History:
 * 19-APR-2026	1.0	Initial Release
 */

#ifndef AUDIO_INC
#define AUDIO_INC

#include "sim.h"
#include "simdefs.h"

extern int audio_init(
    	unsigned long *sample_rate,
    	unsigned long *buffer_size,
    	double sync_adjust,
    	bool stats,
    	char *soundfile,
    	unsigned long recording_limit,
    	void *callback,
    	void *userdata);

//extern double audio_calibrate(int calibration_time);
extern void audio_off(int device_handle);
extern void audio_record(int device_handle, int channel, char data);

#endif /* !AUDIO_INC */
