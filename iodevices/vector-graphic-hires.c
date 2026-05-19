/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Common I/O devices used by various simulated machines
 *
 * Copyright (C) 2015-2019 by Udo Munk
 * Copyright (C) 2018 David McNaughton
 * Copyright (C) 2026 Ansgar Kueckes
 *
 * Emulation of a Vector Graphic High Resolution Graphics board
 *
 * History:
 * 11-OCT-2024 first version
 * 19-APR-2026 added window scaling
 * 03-MAY-2026 moved all non device specific code to fb_video.c
 */
 
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "sim.h"

#ifdef HAS_VECTOR_GRAPHIC_HIRES

#include "simdefs.h"
#include "simglb.h"
#include "simcfg.h"
#include "simmem.h"
#include "simport.h"
#include "fb_video.h"
#ifdef WANT_SDL
#include "simsdl.h"
#endif

#include "vector-graphic-hires.h"

enum VideoMode {
	BILEVEL = 0,
	GRADIENT = 1
};

int vector_graphic_hires_mode = GRADIENT;
int vector_graphic_hires_address = 0xe000;
uint8_t vector_graphic_hires_fg_color[3] = {0, 255, 0};

#define HSIZE 512
#define VSIZE 480

static uint8_t hires_colors[2][3];
static uint8_t hires_gradients[16][3];

static int frames = 0;
static uint64_t start_time;
static int fb_video_device = -1;

/*
 *	Draw scanlines for a full frame
 *
 *	Timing:
 *	
 *	  14.318 MHz system clock
 *	  7.875 MHz pixel clock
 *	  15.75 KHz line frequency
 *	  60 Hz vertical frequency (non interlaced)
 *	  2 scanlines/pixel (medium resolution gradient mode, 128x120)
 *	  1 scanline/pixel (high resolution bilevel mode, 256x240)
 *	  240 scanlines per frame
 *	
 *	The Vector Graphic HiRes board uses its own 8K RAM, of which only
 *	7.5K are used for video memory, the remaining 512 bytes can be
 *	used for other purposes.
 *
 *	The display window is organized as an array of screen pixels width
 *	size HSIZE by VSIZE. Each pixel of the emulated video hardware is
 *	mapped to a square area of psize screen pixels, depending on the
 *	emulated graphics mode. The graphics mode with the highest resolution
 *	is mapped to square areas of edge length psize. Graphics modes with
 *	lower resolution are mapped to larger square areas accordingly.
 *
 *	Since the HiRes hardware works without interlacing, always a full
 *	row of pixels with be rendered at once.
 *	
 */
static void draw_frame()
{
	int bytepos, row, psize;
	WORD addr = vector_graphic_hires_address;
	BYTE i, data;
	int current_row;		/* current row within datablock */
	int num_rows;			/* rows of current pixel resolution */
	int rows_per_datablock;		/* vertical coverage of a datablock */
	uint8_t (*fg)[3];
	
	/* make sure everything is initialized */
	if (fb_video_device < 0) return;

	current_row = 0;
	frames++;

	/* select foreground color for bilevel mode */
	if (vector_graphic_hires_mode == BILEVEL) {
		psize = 2;
		rows_per_datablock = 2;
		num_rows = 240;
		fg = &hires_colors[1];
	}
	else {
		psize = 4;
		rows_per_datablock = 1;
		num_rows = 120;
		fg = &hires_gradients[15];
	}

	/* now draw the frame */
	for (row=0; row<num_rows; row++) {
		
		/* read data from video memory & write into X pixmap */
		for (bytepos=0; bytepos<64; bytepos++) {

			data = dma_read(addr + bytepos);
			if (vector_graphic_hires_mode == BILEVEL) {
				/* render pixels */
				i = data;
				if (row % 2 == 0) {
					/* first subrow */
					if (i & 0x80)
						fill_rect(fb_video_device, bytepos * 4 * psize, row * 2, psize, 2, fg);
					if (i & 0x40)
						fill_rect(fb_video_device, (bytepos * 4 + 1) * psize, row * 2, psize, 2, fg);
					if (i & 0x08)
						fill_rect(fb_video_device, (bytepos * 4 + 2) * psize, row * 2, psize, 2, fg);
					if (i & 0x04)
						fill_rect(fb_video_device, (bytepos * 4 + 3) * psize, row * 2, psize, 2, fg);
				} else {
					/* second subrow */
					if (i & 0x20)
						fill_rect(fb_video_device, bytepos * 4 * psize, row * 2, psize, 2, fg);
					if (i & 0x10)
						fill_rect(fb_video_device, (bytepos * 4 + 1) * psize, row * 2, psize, 2, fg);
					if (i & 0x02)
						fill_rect(fb_video_device, (bytepos * 4 + 2) * psize, row * 2, psize, 2, fg);
					if (i & 0x01)
						fill_rect(fb_video_device, (bytepos * 4 + 3) * psize, row * 2, psize, 2, fg);
				}
			}
			else {	/* nibble mode */
				/* first pixel */
				i = (data & 0xf0) >> 4;

				/* gradient */
				fill_rect(fb_video_device, bytepos * 2 * psize, row * psize, psize * 2, psize, &hires_gradients[i]);			

				/* second pixel */
				i = data & 0x0f;

				/* gradient */
				fill_rect(fb_video_device, (bytepos * 2 + 1) * psize, row * psize, psize * 2, psize, &hires_gradients[i]);
			}
		}

		current_row++;
		if (current_row == rows_per_datablock) {
			/* next datablock */
			addr += 64;
			current_row = 0;		
		}
	}
}


/*
 *	Initialize Vector Graphic HiRes Graphics
 */
void vector_graphic_hires_init()
{
	int i;
	float r,g,b;

	/* setup color tables */
	r = vector_graphic_hires_fg_color[0] / 255.0;
	g = vector_graphic_hires_fg_color[1] / 255.0;
	b = vector_graphic_hires_fg_color[2] / 255.0;
	for (i=0; i<16; i++) {
		hires_gradients[i][0] = (uint8_t) i*0x11*r;
		hires_gradients[i][1] = (uint8_t) i*0x11*g;
		hires_gradients[i][2] = (uint8_t) i*0x11*b;
	}
	hires_colors[1][0] = r * 255; 
	hires_colors[1][1] = g * 255; 
	hires_colors[1][2] = b * 255; 

	frames = 0;
	start_time = get_clock_us();

	fb_video_device = fb_video_init(HSIZE, VSIZE, "Vecor Graphic HiRes",
		0, 0, 0, 15174, 1460, NULL, &draw_frame, NULL, NULL, NULL, NULL, NULL);
}

/*
 *	Switch Vector Graphic HiRes Graphics off from front panel
 */
void vector_graphic_hires_off(void)
{
	if (fb_video_device)
		fb_video_off(fb_video_device);
	fb_video_device = -1;

	double delta = (get_clock_us() - start_time) / 1000000.0;
	printf("HiRes: delta=%fs, frames/s=%f\r\n", delta, frames / delta);
}

#endif /* HAS_VECTOR_GRAPHIC_HIRES */
