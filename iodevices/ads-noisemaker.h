/**
 * ads-noisemaker.h
 * 
 * Emulation of the ADS Noisemaker sound hardware 
 *
 * Copyright (C) 2020 by David McNaughton
 * Copyright (C) 2024 by Ansgar Kueckes
 * 
 * History:
 * 24-SEP-24    1.0     Initial Release
 */

extern void ads_noisemaker_init(void);
extern void ads_noisemaker_off(void);

extern void ads_noisemaker_0_out(BYTE);
extern void ads_noisemaker_1_out(BYTE);
extern void ads_noisemaker_2_out(BYTE);
extern void ads_noisemaker_3_out(BYTE);

extern unsigned long noisemaker_sample_rate;		/* audio sample rate in Hz */
extern unsigned long noisemaker_buffer_size;		/* requested audio buffer size in samples per channel, defines audio delay */
extern double noisemaker_sync_adjust;			/* default fine tuning for audio sync adjustment */
extern unsigned long noisemaker_recording_limit;	/* size of wave buffer for file output & debug purposes */
extern char *noisemaker_soundfile;			/* filename of output WAV file if recording is requested */

