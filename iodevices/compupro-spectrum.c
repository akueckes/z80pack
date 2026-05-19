/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Common I/O devices used by various simulated machines
 *
 * Copyright (C) 2026 Ansgar Kueckes
 *
 * Emulation of a CompuPro Spectrum board
 *
 * History:
 * 08-MAY-2026 First version
 */
 
/*
 *	Timing:
 *	
 *	  14.318 MHz system clock
 *	  7.875 MHz pixel clock
 *	  15.699 KHz line frequency
 *	  60 Hz vertical frequency (interlaced)
 *        4.9 us horizontal sync period
 *        1.6 ms vertical sync period
 *        12 scanlines/character (alpha/semigraphics mode 32 x 16 / 64 x 32)
 *        4 scanlines/pixel (CG1 low resolution 4 color graphics mode, 64 x 64)
 *	  3 scanlines/pixel (RG1 medium resolution bilevel graphics mode, 128 x 64)
 *	  3 scanlines/pixel (CG2 medium resolution 4 color graphics mode, 128 x 64)
 *	  2 scanlines/pixel (RG2 medium resolution bilevel graphics mode, 128 x 96)
 *	  2 scanlines/pixel (CG3 medium resolution 4 color graphics mode, 128 x 96)
 *	  1 scanline/pixel (RG3 high resolution bilevel graphics mode, 128 x 192)
 *	  1 scanline/pixel (CG6 high resolution 4 color graphics mode, 128 x 192)
 *	  1 scanline/pixel (RG6 high resolution bilevel graphics mode, 256 x 192)
 *	  384 scanlines per full frame (active field)
 *	  192 scanlines per field (active field, interlaced)
 *        25 scanlines upper and lower border each
 *	
 *	The CompuPro Spectrum board is based on the Motorola MC6847 Video Display
 *      Generator (VDG) using its own 8K RAM, of which only up to 6K are used for
 *      video memory, the remaining 2 kbytes can be used for other purposes.
 *
 *	The display window is organized as an array of screen pixels width
 *	size HSIZE by VSIZE. Each pixel of the emulated video hardware is
 *	mapped to a square area of psize screen pixels, depending on the
 *	emulated graphics mode. The graphics mode with the highest resolution
 *	is mapped to square areas of edge length psize. Graphics modes with
 *	lower resolution are mapped to larger square areas accordingly.
 *
 *	Since the Spectrum hardware works without interlacing, always a full
 *	row of pixels with be rendered at once.

        Semigraphics mode supports all 8 colors at once. Color assignment is
        done for the whole character area. Semigraphics 4 mode sets/resets
        pixels according to the lower 4 bits of each character data, whereas
        the area color is selected with bits 4-6:
        
             +---+---+---+---+---+---+---+---+
         Bit | 7 | 6 | 7 | 4 | 3 | 2 | 1 | 0 |
             +---+---+---+---+---+---+---+---+
               |   |   |   |   |   |   |   |
               |   |   |   |   |   |   |   +--- lower right quadrant
               |   |   |   |   |   |   +--- lower left quadrant
               |   |   |   |   |   +--- upper right quadrant
               |   |   |   |   +--- upper left quadrant
               |   +---+---+--- color select
               +---alpha/semigraphics select
        
        The 6847 semigraphics 6 mode is not supported on the Spectrum
        (INT/EXT line is connected to ground).
        
        The two color sets combine the following colors for the graphic modes:
        
        CSS 0: Green, Yellow, Blue, Red          (with green border)
        CSS 1: Buff, Cyan, Magenta, Orange       (with buff border)
 
        The control register will control whether the Spectrum is operating as
        a plain 8K RAM extension board with graphics disabled (RAM mode),
        as well as alpha/semigraphics vs. color/bilevel graphics, graphics
        resolution and base color set/palette:

             +---+---+---+---+---+---+---+---+
         Bit | 7 | 6 | 7 | 4 | 3 | 2 | 1 | 0 |
             +---+---+---+---+---+---+---+---+
               |   |   |   |   |   |   |   |
               |   |   |   |   |   |   |   +--- bilevel/color graphics select
               |   |   |   |   |   +---+--- graphics resolution
               |   |   |   |   +--- alpha/semigraphics graphics select
               |   |   |   +--- color/palette set select (CSS)
               |   |   +--- frame buffer enable/disable
               +---+--- unused
         
         The control registers reflects horizontal and vertical synchronization
         together with handshake signals for the parallel I/O port:

         The status registers reflects horizontal and vertical synchronization
         together with handshake signals for the parallel I/O port:

             +---+---+---+---+---+---+---+---+
         Bit | 7 | 6 | 7 | 4 | 3 | 2 | 1 | 0 |
             +---+---+---+---+---+---+---+---+
               |   |   |   |   |   |   |   |
               |   |   |   |   |   |   |   +------ DAV (parallel I/O port)
               |   |   |   |   |   |   +---DNT (parallel I/O port)
               |   |   +---+---+---+--- unused
               |   +--- horizontal sync (video)
               +--- vertical sync (video)

         The parallel I/O port is not emulated.
         
         Color artifacts can be created when using the 256 x 192 mode with black
         and white raster together with a composite monitor. Typically, the four
         colors black, white, blue and orange can be generated with vertical
         stripes, a kind of alternative to the existing 128 x 192 mode adding
         a third palette.
         
         Utilizing four stripes in a row can be used to simulate a 64 x 192 mode
         with up to 16 colors, using the 4-bit patterns
         
           0000 Black
           0001 Dark Blue/Red
           0010 Dark Green
           0011 Medium Blue
           0100 Brown/Dark Red
           0101 Solid Red (or Blue)
           0110 Orange/Brown
           0111 Light Pink/Cyan
           1000 Dark Blue
           1001 Purple/Violet
           1010 Solid Blue (or Red)
           1011 Sky Blue
           1100 Yellow/Green
           1101 Light Orange
           1110 White/Cyan
           1111 White
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "sim.h"

#ifdef HAS_COMPUPRO_SPECTRUM

#include "simdefs.h"
#include "simglb.h"
#include "simcfg.h"
#include "simmem.h"
#include "simport.h"
#include "simcore.h"
#include "fb_video.h"
#ifdef WANT_SDL
#include "simsdl.h"
#endif

#include "compupro-spectrum.h"

enum VideoMode {
	RAM = 0,	/* RAM mode/disabled */
	ALPHA = 1,	/* alphanumeric 32 x 16 */
	CG1 = 2,	/* 4 color 64 x 64 */
	RG1 = 3,	/* bilevel 128 x 64 */
	CG2 = 4,	/* 4 color 128 x 64 */
	RG2 = 5,	/* bilevel 128 x 96 */
	CG3 = 6,	/* 4 color 128 x 96 */
	RG3 = 7,	/* bilevel 128 x 192 */
	CG6 = 8,	/* 4 color 128 x 192 */
	RG6 = 9		/* bilevel 256 x 192 */
};

static int graphic_modes[8][3] = {
	{ 4, 64, 64 },		/* CG1 4 color 64 x 64 */
	{ 2, 128, 64 },		/* RG1 bilevel 128 x 64 */
	{ 4, 128, 64 },		/* CG2 4 color 128 x 64 */
	{ 2, 128, 96 },		/* RG2 bilevel 128 x 96 */
	{ 4, 128, 96 },		/* CG3 4 color 128 x 96 */
	{ 2, 128, 192 },	/* RG3 bilevel 128 x 192 */	
	{ 4, 128, 192 },	/* CG6 4 color 128 x 192 */
	{ 2, 256, 192 }		/* RG6 bilevel 256 x 192 */
};

int spectrum_address = 0xa000;
int spectrum_border = true;
int spectrum_interlaced = false;
int spectrum_frame_sync = false;
int spectrum_artifacts = false;
int spectrum_stats = false;

#define HSIZE 512		/* active area 512 x 384 */
#define VSIZE 384

/* Spectrum stuff */
typedef struct {
	int scanline;		/* current scanline */
	int mode;		/* current video mode */
	int css;		/* current color set */
	BYTE status;		/* current status */
	WORD addr;		/* current address in video buffer */
} Spectrum_registers;

static Spectrum_registers spectrum = { 0, RAM, 0, 0xfc, 0x0000 };

static int frames = 0;
static int *field;
static bool *vblank;
static int *timer_id;
static uint64_t start_time;
static int fb_video_device = -1;
static int border = 0;

/* color shades from MAME */
static uint8_t spectrum_colors[32][3] = {
	/* standard colors */
	{ 0x00, 0xd0, 0x00 }, // 0 GREEN
	{ 0xd0, 0xd0, 0x00 }, // 1 YELLOW
	{ 0x00, 0x00, 0xd0 }, // 2 BLUE
	{ 0xd0, 0x00, 0x00 }, // 3 RED
	{ 0xd0, 0xd0, 0xd0 }, // 4 BUFF
	{ 0x00, 0xd0, 0xd0 }, // 5 CYAN
	{ 0xd0, 0x00, 0xd0 }, // 6 MAGENTA
	{ 0xff, 0x7f, 0x00 }, // 7 ORANGE
        
	/* 2 color graphics mode colors */
	{ 0x26, 0x30, 0x16 }, // 8 BLACK
	{ 0x30, 0xd2, 0x00 }, // 9 GREEN
	{ 0x26, 0x30, 0x16 }, // 10 BLACK
	{ 0xbf, 0xc8, 0xad }, // 11 BUFF
        
	/* alpha mode colors */
	{ 0x00, 0x7c, 0x00 }, // 12 ALPHANUMERIC DARK GREEN
	{ 0x30, 0xd2, 0x00 }, // 13 ALPHANUMERIC BRIGHT GREEN
	{ 0x6b, 0x27, 0x00 }, // 14 ALPHANUMERIC DARK ORANGE
	{ 0xff, 0xb7, 0x00 }, // 15 ALPHANUMERIC BRIGHT ORANGE

	/* artifact colors */
	{ 0x00, 0x00, 0x00 }, // 16 Black		own:	0x0 -> black
	{ 0x00, 0x00, 0x40 }, // 17 Dark Blue/Red		0x1 -> blue
	{ 0x00, 0x40, 0x00 }, // 18 Dark Green			0x2 -> yellow-green/black
	{ 0x00, 0x00, 0x80 }, // 19 Medium Blue                 0x3 -> orange/violett
	{ 0x40, 0x20, 0x00 }, // 20 Brown/Dark Red              0x4 -> blue
	{ 0x80, 0x00, 0x00 }, // 21 Solid Red (or Blue)         0x5 -> solid blue
	{ 0x40, 0x20, 0x00 }, // 22 Orange/Brown                0x6 -> grey-orange/brown
	{ 0xa0, 0x00, 0xa0 }, // 23 Light Pink/Cyan             0x7 -> violett/blue
	{ 0x00, 0x00, 0x40 }, // 24 Dark Blue                   0x8 -> yellow-green/black (same as 0x2)
	{ 0x80, 0x00, 0x80 }, // 25 Purple/Violet               0x9 -> grey/dark red
	{ 0x00, 0x00, 0x80 }, // 26 Solid Blue (or Red)         0xa -> solid green
	{ 0x80, 0x80, 0xd0 }, // 27 Sky Blue                    0xb -> yellow/green
	{ 0xd0, 0xe0, 0x00 }, // 28 Yellow/Green                0xc -> violett/yellow
	{ 0xd0, 0x80, 0x00 }, // 29 Light Orange                0xd -> violett/blue (same as 0x7)
	{ 0xd0, 0xd0, 0xe0 }, // 30 White/Cyan                  0xe -> yellow/green (same as 0x2)
	{ 0xd0, 0xd0, 0xd0 }  // 31 White                       0xf -> solid buff
};

static uint8_t spectrum_fontdata8x12[] =
{
	0x00, 0x1C, 0x22, 0x02, 0x1A, 0x26, 0x22, 0x1C, 0x00, 0x00, 0x00, 0x00, /* @ */
	0x00, 0x08, 0x14, 0x22, 0x22, 0x3E, 0x22, 0x22, 0x00, 0x00, 0x00, 0x00, /* A */
	0x00, 0x3C, 0x12, 0x12, 0x1C, 0x12, 0x12, 0x3C, 0x00, 0x00, 0x00, 0x00, /* B */
	0x00, 0x1C, 0x22, 0x20, 0x20, 0x20, 0x22, 0x1C, 0x00, 0x00, 0x00, 0x00, /* C */
	0x00, 0x3C, 0x12, 0x12, 0x12, 0x12, 0x12, 0x3C, 0x00, 0x00, 0x00, 0x00, /* D */
	0x00, 0x3E, 0x20, 0x20, 0x38, 0x20, 0x20, 0x3E, 0x00, 0x00, 0x00, 0x00, /* E */
	0x00, 0x3E, 0x20, 0x20, 0x38, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00, /* F */
	0x00, 0x1E, 0x20, 0x20, 0x26, 0x22, 0x22, 0x1E, 0x00, 0x00, 0x00, 0x00, /* G */
	0x00, 0x22, 0x22, 0x22, 0x3E, 0x22, 0x22, 0x22, 0x00, 0x00, 0x00, 0x00, /* H */
	0x00, 0x1C, 0x08, 0x08, 0x08, 0x08, 0x08, 0x1C, 0x00, 0x00, 0x00, 0x00, /* I */
	0x00, 0x02, 0x02, 0x02, 0x02, 0x22, 0x22, 0x1C, 0x00, 0x00, 0x00, 0x00, /* J */
	0x00, 0x22, 0x24, 0x28, 0x30, 0x28, 0x24, 0x22, 0x00, 0x00, 0x00, 0x00, /* K */
	0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x3E, 0x00, 0x00, 0x00, 0x00, /* L */
	0x00, 0x22, 0x36, 0x2A, 0x2A, 0x22, 0x22, 0x22, 0x00, 0x00, 0x00, 0x00, /* M */
	0x00, 0x22, 0x22, 0x32, 0x2A, 0x26, 0x22, 0x22, 0x00, 0x00, 0x00, 0x00, /* N */
	0x00, 0x1C, 0x22, 0x22, 0x22, 0x22, 0x22, 0x1C, 0x00, 0x00, 0x00, 0x00, /* O */
	0x00, 0x3C, 0x22, 0x22, 0x3C, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00, /* P */
	0x00, 0x1C, 0x22, 0x22, 0x22, 0x2A, 0x24, 0x1A, 0x00, 0x00, 0x00, 0x00, /* Q */
	0x00, 0x3C, 0x22, 0x22, 0x3C, 0x28, 0x24, 0x22, 0x00, 0x00, 0x00, 0x00, /* R */
	0x00, 0x1C, 0x22, 0x20, 0x1C, 0x02, 0x22, 0x1C, 0x00, 0x00, 0x00, 0x00, /* S */
	0x00, 0x3E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, /* T */
	0x00, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x1C, 0x00, 0x00, 0x00, 0x00, /* U */
	0x00, 0x22, 0x22, 0x22, 0x14, 0x14, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, /* V */
	0x00, 0x22, 0x22, 0x22, 0x22, 0x2A, 0x36, 0x22, 0x00, 0x00, 0x00, 0x00, /* W */
	0x00, 0x22, 0x22, 0x14, 0x08, 0x14, 0x22, 0x22, 0x00, 0x00, 0x00, 0x00, /* X */
	0x00, 0x22, 0x22, 0x14, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, /* Y */
	0x00, 0x3E, 0x02, 0x04, 0x08, 0x10, 0x20, 0x3E, 0x00, 0x00, 0x00, 0x00, /* Z */
	0x00, 0x1C, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1C, 0x00, 0x00, 0x00, 0x00, /* [ */
	0x00, 0x00, 0x20, 0x10, 0x08, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, /* \ */
	0x00, 0x1C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1C, 0x00, 0x00, 0x00, 0x00, /* ] */
	0x00, 0x08, 0x1C, 0x2A, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, /* up arrow */
	0x00, 0x00, 0x08, 0x10, 0x3E, 0x10, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, /* left arrow */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* space */
	0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, /* ! */
	0x00, 0x14, 0x14, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* " */
	0x00, 0x14, 0x14, 0x3E, 0x00, 0x3E, 0x14, 0x14, 0x00, 0x00, 0x00, 0x00, /* # */
	0x00, 0x08, 0x1E, 0x20, 0x1C, 0x02, 0x3C, 0x08, 0x00, 0x00, 0x00, 0x00, /* $ */
	0x00, 0x30, 0x32, 0x04, 0x08, 0x10, 0x26, 0x06, 0x00, 0x00, 0x00, 0x00, /* % */
	0x00, 0x10, 0x28, 0x28, 0x10, 0x2A, 0x24, 0x1A, 0x00, 0x00, 0x00, 0x00, /* & */
	0x00, 0x18, 0x18, 0x10, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ' */
	0x00, 0x04, 0x08, 0x10, 0x10, 0x10, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, /* ( */
	0x00, 0x10, 0x08, 0x04, 0x04, 0x04, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00, /* ) */
	0x00, 0x00, 0x08, 0x2A, 0x1C, 0x1C, 0x2A, 0x08, 0x00, 0x00, 0x00, 0x00, /* * */
	0x00, 0x00, 0x08, 0x08, 0x3E, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, /* + */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x10, 0x20, 0x00, 0x00, /* , */
	0x00, 0x00, 0x00, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* - */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, /* . */
	0x00, 0x00, 0x02, 0x04, 0x08, 0x10, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, /* / */
	0x00, 0x1C, 0x22, 0x26, 0x2A, 0x32, 0x22, 0x1C, 0x00, 0x00, 0x00, 0x00, /* 0 */
	0x00, 0x08, 0x18, 0x08, 0x08, 0x08, 0x08, 0x1C, 0x00, 0x00, 0x00, 0x00, /* 1 */
	0x00, 0x1C, 0x22, 0x02, 0x1C, 0x20, 0x20, 0x3E, 0x00, 0x00, 0x00, 0x00, /* 2 */
	0x00, 0x1C, 0x22, 0x02, 0x0C, 0x02, 0x22, 0x1C, 0x00, 0x00, 0x00, 0x00, /* 3 */
	0x00, 0x04, 0x0C, 0x14, 0x24, 0x3E, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00, /* 4 */
	0x00, 0x3E, 0x20, 0x3C, 0x02, 0x02, 0x22, 0x1C, 0x00, 0x00, 0x00, 0x00, /* 5 */
	0x00, 0x0C, 0x10, 0x20, 0x3C, 0x22, 0x22, 0x1C, 0x00, 0x00, 0x00, 0x00, /* 6 */
	0x00, 0x3E, 0x02, 0x04, 0x08, 0x10, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00, /* 7 */
	0x00, 0x1C, 0x22, 0x22, 0x1C, 0x22, 0x22, 0x1C, 0x00, 0x00, 0x00, 0x00, /* 8 */
	0x00, 0x1C, 0x22, 0x22, 0x1E, 0x02, 0x04, 0x18, 0x00, 0x00, 0x00, 0x00, /* 9 */
	0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, /* : */
	0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x10, 0x20, 0x00, 0x00, /* ; */
	0x00, 0x04, 0x08, 0x10, 0x20, 0x10, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, /* < */
	0x00, 0x00, 0x00, 0x3E, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* = */
	0x00, 0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00, /* > */
	0x00, 0x1C, 0x22, 0x02, 0x04, 0x08, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, /* ? */
};

/*
 *	Draw a single scanline
 */
static void draw_scanline(int scanline, WORD *addr)
{
	BYTE data;
	WORD row_addr;
	int i, col, num_cols, num_rows, num_colors, pwidth, pheight, char_code;
	uint8_t (*fg)[3], (*bg)[3];

	if (spectrum_interlaced) {
		if (((*field == ODD) && (scanline % 2 == 1)) ||
		    ((*field == EVEN) && (scanline % 2 == 0))) {
		    	/* draw a black line */
			fill_rect(fb_video_device, 0, scanline, HSIZE + border * 2, 1, &spectrum_colors[8]);
			
			/* update current memory address */
			if (spectrum.mode == ALPHA) {
				if ((scanline - border + 1) % 24 == 0) *addr = *addr + 32;
			}
			else if (spectrum.mode != RAM) {
				num_colors = graphic_modes[spectrum.mode - CG1][0];
				num_cols = graphic_modes[spectrum.mode - CG1][1];
				num_rows = graphic_modes[spectrum.mode - CG1][2];
				pheight = VSIZE / num_rows;
				if ((scanline - border + 1) % pheight == 0) {
					if (num_colors == 4)
						*addr = *addr + num_cols / 4;
					else
						*addr = *addr + num_cols / 8;
				}
			}
			return;
		}
	}

	switch (spectrum.mode) {
	case RAM:
		if ((scanline < border) || (scanline > VSIZE+border))
			fill_rect(fb_video_device, 0, scanline, HSIZE + border * 2, 1, &spectrum_colors[8]);
		else {
			fill_rect(fb_video_device, 0, scanline, border, 1, &spectrum_colors[8]);
			fill_rect(fb_video_device, border, scanline, HSIZE, 1, &spectrum_colors[7]);
			fill_rect(fb_video_device, HSIZE + border , scanline, border, 1, &spectrum_colors[8]);
		}
		break;
	case ALPHA:	/* alphanumeric & semigraphics 32 x 16 */
		num_cols = 32;
		num_rows = 16;
		pwidth = HSIZE / num_cols;
		pheight = VSIZE / num_rows;

		if ((scanline < border) || (scanline >= VSIZE+border))
			fill_rect(fb_video_device, 0, scanline, HSIZE + border * 2, 1, &spectrum_colors[8]);
		else {
			fill_rect(fb_video_device, 0, scanline, border, 1, &spectrum_colors[8]);
			row_addr = *addr;
			for (col=0; col<num_cols; col++) {
				data = dma_read((*addr)++);
				if (data & 0x80) {	/* SG4 semigraphics mode with all 8 colors */
					if ((scanline - border) % 24 < 12) {
						/* upper left quadrant */
						if (data & 0x08)
							fill_rect(fb_video_device, border + col * pwidth, scanline, pwidth / 2, 1, &spectrum_colors[(data & 0x70) >> 4]);
						else
							fill_rect(fb_video_device, border + col * pwidth, scanline, pwidth / 2, 1, &spectrum_colors[8]);
	
						/* upper right quadrant */
						if (data & 0x04)
							fill_rect(fb_video_device, border + col * pwidth + pwidth / 2, scanline, pwidth / 2, 1, &spectrum_colors[(data & 0x70) >> 4]);
						else
							fill_rect(fb_video_device, border + col * pwidth + pwidth / 2, scanline, pwidth / 2, 1, &spectrum_colors[8]);
	
						/* lower left quadrant */
					}
					else {
						if (data & 0x02)
							fill_rect(fb_video_device, border + col * pwidth, scanline, pwidth / 2, 1, &spectrum_colors[(data & 0x70) >> 4]);
						else
							fill_rect(fb_video_device, border + col * pwidth, scanline, pwidth / 2, 1, &spectrum_colors[8]);
	
						/* lower right quadrant */
						if (data & 0x01)
							fill_rect(fb_video_device, border + col * pwidth + pwidth / 2, scanline, pwidth / 2, 1, &spectrum_colors[(data & 0x70) >> 4]);
						else
							fill_rect(fb_video_device, border + col * pwidth + pwidth / 2, scanline, pwidth / 2, 1, &spectrum_colors[8]);
					}
				}
				else {		/* alphanumerics with 64 characters */
					char_code = data & 0x3f;

					if (data & 0x40) {
						fg = &spectrum_colors[13 + spectrum.css * 2];	/* inverse */
						bg = &spectrum_colors[12 + spectrum.css * 2];	/* inverse */
					}
					else {
						fg = &spectrum_colors[12 + spectrum.css * 2];
						bg = &spectrum_colors[13 + spectrum.css * 2];
					}
					i = ((scanline - border) % 24) / 2;
					draw_pattern(fb_video_device, border + col * pwidth, scanline, 2, spectrum_fontdata8x12[char_code * 12 + i], fg, bg); 
				}
			}
			if ((scanline - border + 1) % pheight) *addr = row_addr;
			fill_rect(fb_video_device, HSIZE + border , scanline, border, 1, &spectrum_colors[8]);
		}
		break;
	default:	/* graphics */
		num_colors = graphic_modes[spectrum.mode - CG1][0];
		num_cols = graphic_modes[spectrum.mode - CG1][1];
		num_rows = graphic_modes[spectrum.mode - CG1][2];
		pwidth = HSIZE / num_cols;
		pheight = VSIZE / num_rows;

		if ((scanline < border) || (scanline >= VSIZE+border))
			fill_rect(fb_video_device, 0, scanline, HSIZE + border * 2, 1, &spectrum_colors[9 + spectrum.css * 2]);
		else {
			fill_rect(fb_video_device, 0, scanline, border, 1, &spectrum_colors[9 + spectrum.css * 2]);
			row_addr = *addr;
			if ((spectrum.mode == RG6) && spectrum_artifacts && spectrum.css) {
				/* 256 x 192 black & white with artifacts */
				pwidth = 8;
				for (col=0; col<32; col++) {
					data = dma_read((*addr)++);
					fill_rect(fb_video_device, border + col * pwidth * 2, scanline, pwidth, 1, &spectrum_colors[(16 + ((data & 0xf0) >> 4))]);
					fill_rect(fb_video_device, border + col * pwidth * 2 + pwidth, scanline, pwidth, 1, &spectrum_colors[16 + (data & 0xf)]);
				}
			}
			else {
				for (col=0; col<num_cols; col++) {
					if (num_colors == 2) {
						if (col % 8 == 0) data = dma_read((*addr)++);
						if (data & 0x80)
							fill_rect(fb_video_device, border + col * pwidth, scanline, pwidth, 1, &spectrum_colors[spectrum.css * 4]);
						else
							fill_rect(fb_video_device, border + col * pwidth, scanline, pwidth, 1, &spectrum_colors[8]);
						data <<= 1;
					}
					else if (num_colors == 4) {
						if (col % 4 == 0) data = dma_read((*addr)++);
						fill_rect(fb_video_device, border + col * pwidth, scanline, pwidth, 1, &spectrum_colors[((data & 0xc0) >> 6) + (spectrum.css * 4)]);
						data <<= 2;
					}
				}
			}
			if ((scanline - border + 1) % pheight) *addr = row_addr;
			fill_rect(fb_video_device, HSIZE + border , scanline, border, 1, &spectrum_colors[9 + spectrum.css * 2]);
		}
	}
}

#ifdef CPU_TIMER
/*
 *	Render a scanline with the use of a CPU timer for near perfect synchronization
 *	with emulated CPU (called from simulator thread)
 */
void compupro_spectrum_timer_callback(void *user_data)
{
	Spectrum_registers *spectrum = (Spectrum_registers *)user_data;

	if (!spectrum) return;

	/* start vscan */
	if (spectrum->scanline == 0) {
		*vblank = false;
		spectrum->status |= 0x80;
		frames++;
	}

	if (spectrum->scanline < 384 + (2 * border)) {
		if (spectrum->status & 0x40) {
			/* start hblank */
			spectrum->status &= 0xbf;
	
			/* re-schedule CPU timer for end of horizontal blank */
			set_cpu_timer(*timer_id, T + (f_value * 10));
		}
		else {
			/* start hscan */
			spectrum->status |= 0x40;
		
			/* draw scaline */
			draw_scanline(spectrum->scanline, &spectrum->addr);
			spectrum->scanline++;

			/* re-schedule CPU timer for end of horizontal scan */
			set_cpu_timer(*timer_id, T + (f_value * 54));

		}
	}
	else {
		/* field done, signal vertical blank */
		spectrum->addr = spectrum_address;
		spectrum->scanline = 0;
		spectrum->status &= 0x7f;
		*vblank = true;

		/* re-schule CPU timer for 1.6 ms vertical blank  */
		set_cpu_timer(*timer_id, T + (f_value * 1600));
	}
}

#else

/*
 *	Draw scanlines for a full frame
 */
static void draw_frame()
{
	WORD addr = spectrum_address;
	int scanline;

	spectrum.status |= 0x80;		/* starting vscan */

	/* make sure everything is initialized */
	if (fb_video_device < 0) return;

	for (scanline=0; scanline<VSIZE+border*2; scanline++) {
		spectrum.status |= 0x40;	/* starting hscan */
		draw_scanline(scanline, &addr);
		spectrum.status &= 0xbf;	/* starting hblank */
	}

	spectrum.status &= 0x7f;		/* starting vblank */
	frames++;
}

#endif

/*
 *	Initialize CompuPro Spectrum
 */
void compupro_spectrum_init()
{
	frames = 0;
	start_time = get_clock_us();
	
	if (spectrum_border) border = 50;
		
#ifdef CPU_TIMER
	fb_video_device = fb_video_init(HSIZE + border * 2, VSIZE + border * 2, "CompuPro Spectrum",
		spectrum_stats, spectrum_frame_sync, spectrum_interlaced, 15174, 1460, NULL, NULL, compupro_spectrum_timer_callback, &spectrum, &vblank, &field, &timer_id);
#else
	fb_video_device = fb_video_init(HSIZE + border * 2, VSIZE + border * 2, "CompuPro Spectrum",
		spectrum_stats, spectrum_frame_sync, spectrum_interlaced, 15174, 1460, NULL, &draw_frame, NULL, NULL, &vblank, &field, &timer_id);
#endif
}

/*
 *	Switch CompuPro Spectrum off from front panel
 */
void compupro_spectrum_off(void)
{
	if (fb_video_device >= 0)
		fb_video_off(fb_video_device);
	fb_video_device = -1;

	double delta = (get_clock_us() - start_time) / 1000000.0;

	if (spectrum_stats)
		printf("Spectrum: delta=%fs, frames/s=%f\r\n", delta, frames / delta);
}

/*
 *
 */
void compupro_spectrum_out(BYTE data)
{
	if ((data & 0x20) == 0) spectrum.mode = RAM;
	else {
		spectrum.css = data & 0x10 ? 1 : 0;
		if ((data & 0x08) == 0) spectrum.mode = ALPHA;
		else spectrum.mode = CG1 + (data & 0x7);
	}
}

/*
 *
 */
BYTE compupro_spectrum_in(void)
{
	return spectrum.status;
}

#endif /* HAS_COMPUPRO_SPECTRUM */
