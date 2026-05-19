/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Common I/O devices used by various simulated machines
 *
 * Copyright (C) 2015-2019 by Udo Munk
 * Copyright (C) 2018 David McNaughton
 * Copyright (C) 2024 Ansgar Kueckes (Vector Graphic High Resoution Graphics board emulation)
 *
 * Emulation of a CompuPro Spectrum board
 *
 * History:
 * 08-MAY-2026 First version
 */

#ifndef COMPUPRO_SPECTRUM_INC
#define COMPUPRO_SPECTRUM_INC

#include "sim.h"
#include "simdefs.h"

extern int spectrum_interlaced;
extern int spectrum_frame_sync;
extern int spectrum_border;
extern int spectrum_artifacts;
extern int spectrum_address;
extern int spectrum_stats;

void compupro_spectrum_init(void);
void compupro_spectrum_off(void);
void compupro_spectrum_out(BYTE data);
BYTE compupro_spectrum_in(void);

#endif /* !COMPUPRO_SPECTRUM_INC_INC */
