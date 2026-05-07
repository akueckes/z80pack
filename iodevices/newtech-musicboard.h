/**
 * newtech-musicboard.h
 *
 * Emulation of the Newtech Model 6 Musicboard
 *
 * Copyright (C) 2026 by Ansgar Kueckes
 *
 * History:
 * 22-MAR-2026	1.0	Initial Release
 */

#ifndef NEWTECH_MUSICBOARD_INC
#define NEWTECH_MUSICBOARD_INC

#include "sim.h"
#include "simdefs.h"

extern void newtech_musicboard_init(void);
extern void newtech_musicboard_off(void);

extern void newtech_musicboard1_out(uint8_t data);
extern void newtech_musicboard2_out(uint8_t data);

extern unsigned long musicboard_sample_rate;		/* requested audio sample rate in Hz */
extern unsigned long musicboard_buffer_size;    	/* requested audio buffer size in samples per channel, defines audio delay */
extern double musicboard_sync_adjust;           	/* fine tuning for audio sync adjustment */                                  
extern char *musicboard_soundfile;              	/* filename of output WAV file if recording is requested */                          
extern unsigned long musicboard_recording_limit;        /* size of wave buffer for file output & debug purposes */
extern bool musicboard_stats;                   	/* output some stats */                                                              

#endif /* !NEWTECH_MUSICBOARD_INC */
