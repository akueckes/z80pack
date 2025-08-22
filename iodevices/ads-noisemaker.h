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

extern long noisemaker_sample_rate;
extern long noisemaker_recording_limit;
extern char *noisemaker_soundfile;

