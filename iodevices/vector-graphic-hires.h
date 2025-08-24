/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Common I/O devices used by various simulated machines
 *
 * Copyright (C) 2015-2019 by Udo Munk
 * Copyright (C) 2018 David McNaughton
 * Copyright (C) 2024 Ansgar Kueckes (Vector Graphic High Resoution Graphics board emulation)
 *
 * Emulation of a Vector Graphic High Resoultion Graphics board
 *
 * History:
 * 11-OCT-2024 first version
 *
 */

#ifndef VECTOR_GRAPHIC_HIRES_INC
#define VECTOR_GRAPHIC_HIRES_INC

#include "sim.h"
#include "simdefs.h"

extern int vector_graphic_hires_mode;
extern int vector_graphic_hires_address;
extern uint8_t vector_graphic_hires_fg_color[3];

void vector_graphic_hires_init(void);
void vector_graphic_hires_off(void);

#endif /* !VECTOR_GRAPHIC_HIRES_INC */
