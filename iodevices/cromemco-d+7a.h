/**
 * cromemco-d+7a.h
 *
 * Emulation of the Cromemco D+7A I/O
 *
 * Copyright (C) 2020 by David McNaughton
 * Copyright (C) 2025 by Ansgar Kueckes
 *
 * History:
 * 14-JAN-2020	1.0	Initial Release
 * 06-JUN-2025		Functional implementation based on SDL_Audio and PortAudio
 */

#ifndef CROMEMCO_DPLUS7A_INC
#define CROMEMCO_DPLUS7A_INC

#include "sim.h"
#include "simdefs.h"

extern void cromemco_d7a_init(void);
extern void cromemco_d7a_off(void);

extern void cromemco_d7a_D_out(BYTE data);
extern void cromemco_d7a_A1_out(BYTE data);
extern void cromemco_d7a_A2_out(BYTE data);
extern void cromemco_d7a_A3_out(BYTE data);
extern void cromemco_d7a_A4_out(BYTE data);
extern void cromemco_d7a_A5_out(BYTE data);
extern void cromemco_d7a_A6_out(BYTE data);
extern void cromemco_d7a_A7_out(BYTE data);

extern unsigned long d7a_sample_rate;		/* default audio sample rate in Hz */
extern unsigned long d7a_buffer_size;		/* default requested audio buffer size in samples per channel, defines audio delay */
extern double d7a_sync_adjust;			/* default fine tuning for audio sync adjustment */
extern char *d7a_soundfile;			/* filename of output WAV file if recording is requested */
extern unsigned long d7a_recording_limit;	/* size of wave buffer for file output & debug purposes */
extern bool d7a_stats;				/* output some stats */
extern bool d7a_do_map_axis[2];			/* axis mapping requested */
extern bool d7a_do_map_buttons[4];			/* button mapping requested */
extern int d7a_axis_map[2][65];			/* game controller custom axis mapping */
extern int d7a_button_map[2][65];		/* game controller custom button mapping */

extern BYTE cromemco_d7a_D_in(void);
extern BYTE cromemco_d7a_A1_in(void);
extern BYTE cromemco_d7a_A2_in(void);
extern BYTE cromemco_d7a_A3_in(void);
extern BYTE cromemco_d7a_A4_in(void);
extern BYTE cromemco_d7a_A5_in(void);
extern BYTE cromemco_d7a_A6_in(void);
extern BYTE cromemco_d7a_A7_in(void);

#endif /* !CROMEMCO_DPLUS7A_INC */
