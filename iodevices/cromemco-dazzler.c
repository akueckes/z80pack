/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Common I/O devices used by various simulated machines
 *
 * Copyright (C) 2015-2019 by Udo Munk
 * Copyright (C) 2018 David McNaughton
 * Copyright (C) 2025 by Thomas Eberhardt
 * Copyright (C) 2025 Ansgar Kueckes (realtime extensions)
 *
 * Emulation of a Cromemco DAZZLER S100 board
 *
 * History:
 * 24-APR-2015 first version
 * 25-APR-2015 fixed a few things, good enough for a BETA release now
 * 27-APR-2015 fixed logic bugs with on/off state and thread handling
 * 08-MAY-2015 fixed Xlib multithreading problems
 * 26-AUG-2015 implemented double buffering to prevent flicker
 * 27-AUG-2015 more bug fixes
 * 15-NOV-2016 fixed logic bug, display wasn't always clear after
 *	       the device is switched off
 * 06-DEC-2016 added bus request for the DMA
 * 16-DEC-2016 use DMA function for memory access
 * 26-JAN-2017 optimization
 * 15-JUL-2018 use logging
 * 19-JUL-2018 integrate webfrontend
 * 04-NOV-2019 remove fake DMA bus request
 * 04-JAN-2025 add SDL2 support
 * 06-JUN-2025 added support for interlaced video, line flag/busmaster and window resize
*/

#include <stdio.h>
#include <stdlib.h>
#ifdef WANT_SDL
#include <SDL.h>
#else
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <string.h>
#if 0
#include <X11/extensions/Xrender.h>
#endif
#endif

#include "sim.h"
#include "simdefs.h"
#include "simglb.h"
#include "simcfg.h"
#include "simmem.h"
#include "simport.h"
#include "simcore.h"
#ifdef WANT_SDL
#include "simsdl.h"
#endif

#ifdef HAS_DAZZLER

#ifdef HAS_NETSERVER
#include <string.h>
#include "netsrv.h"
#endif

#if !defined(WANT_SDL) || defined(HAS_NETSERVER)
#include <pthread.h>

/* #define LOG_LOCAL_LEVEL LOG_DEBUG */
#include "log.h"
static const char *TAG = "DAZZLER";
#endif

#include "cromemco-dazzler.h"

/* parameters configurable in system.conf */
bool dazzler_interlaced = false;	/* non-interlaced display by default */
bool dazzler_line_sync = false;		/* no line sync by default */
bool dazzler_descrete_scale = false;	/* no decsrete window scaling by default */

/* SDL2/X11 stuff */
#define WSIZE 384
static int canvas_size = WSIZE;
static int window_size = WSIZE;
static int pscale = 1;
static bool window_resized = false;

#ifdef WANT_SDL
static int dazzler_win_id = -1;
static SDL_Window *window;
static SDL_Surface *surface;
static SDL_Renderer *renderer;
static uint8_t colors[16][3] = {
	{ 0x00, 0x00, 0x00 },
	{ 0x80, 0x00, 0x00 },
	{ 0x00, 0x80, 0x00 },
	{ 0x80, 0x80, 0x00 },
	{ 0x00, 0x00, 0x80 },
	{ 0x80, 0x00, 0x80 },
	{ 0x00, 0x80, 0x80 },
	{ 0x80, 0x80, 0x80 },
	{ 0x00, 0x00, 0x00 },
	{ 0xFF, 0x00, 0x00 },
	{ 0x00, 0xFF, 0x00 },
	{ 0xFF, 0xFF, 0x00 },
	{ 0x00, 0x00, 0xFF },
	{ 0xFF, 0x00, 0xFF },
	{ 0x00, 0xFF, 0xFF },
	{ 0xFF, 0xFF, 0xFF }
};
static uint8_t grays[16][3] = {
	{ 0x00, 0x00, 0x00 },
	{ 0x11, 0x11, 0x11 },
	{ 0x22, 0x22, 0x22 },
	{ 0x33, 0x33, 0x33 },
	{ 0x44, 0x44, 0x44 },
	{ 0x55, 0x55, 0x55 },
	{ 0x66, 0x66, 0x66 },
	{ 0x77, 0x77, 0x77 },
	{ 0x88, 0x88, 0x88 },
	{ 0x99, 0x99, 0x99 },
	{ 0xAA, 0xAA, 0xAA },
	{ 0xBB, 0xBB, 0xBB },
	{ 0xCC, 0xCC, 0xCC },
	{ 0xDD, 0xDD, 0xDD },
	{ 0xEE, 0xEE, 0xEE },
	{ 0xFF, 0xFF, 0xFF }
};
#else /* !WANT_SDL */
static Display *display;
static Window window;
static int screen;
static GC gc;
static Window rootwindow;
static XWindowAttributes wa;
static Atom wm_focused, wm_maxhorz, wm_maxvert, wm_hidden;	
static Pixmap pixmap;
static Colormap colormap;
static XColor colors[16];
static XColor grays[16];
static char color0[] =  "#000000";
static char color1[] =  "#800000";
static char color2[] =  "#008000";
static char color3[] =  "#808000";
static char color4[] =  "#000080";
static char color5[] =  "#800080";
static char color6[] =  "#008080";
static char color7[] =  "#808080";
static char color8[] =  "#000000";
static char color9[] =  "#FF0000";
static char color10[] = "#00FF00";
static char color11[] = "#FFFF00";
static char color12[] = "#0000FF";
static char color13[] = "#FF00FF";
static char color14[] = "#00FFFF";
static char color15[] = "#FFFFFF";
static char gray0[] =   "#000000";
static char gray1[] =   "#111111";
static char gray2[] =   "#222222";
static char gray3[] =   "#333333";
static char gray4[] =   "#444444";
static char gray5[] =   "#555555";
static char gray6[] =   "#666666";
static char gray7[] =   "#777777";
static char gray8[] =   "#888888";
static char gray9[] =   "#999999";
static char gray10[] =  "#AAAAAA";
static char gray11[] =  "#BBBBBB";
static char gray12[] =  "#CCCCCC";
static char gray13[] =  "#DDDDDD";
static char gray14[] =  "#EEEEEE";
static char gray15[] =  "#FFFFFF";
#endif /* !WANT_SDL */

/* DAZZLER stuff */
static bool state, last_state;
static WORD dma_addr, addr;
static BYTE line_buffer[32];
static BYTE flags = 0x3f;
static BYTE format;
static int ticks_per_usleep;
static int field;
static int scanline;
#define EVEN	0		/* only even fields */
#define ODD	1		/* only odd fields */
#define FULL	2		/* all fields */

#if !defined(WANT_SDL) || defined(HAS_NETSERVER)
/* UNIX stuff */
static pthread_t thread;
#endif

#ifdef HAS_NETSERVER
static void ws_clear(void);
static BYTE formatBuf = 0;
#endif

/* debug data */
struct {
	int ticks[10][64];
	int gap[10][64];
	int cycle[10];
	int row_index;
	int frame_index;
} row_data;

/* create the SDL2 or X11 window for DAZZLER display */
static void open_display(void)
{
	int i;
	uint64_t t_start;
	
	/* calibrate sleep timer */
	if (dazzler_line_sync) {
		t_start = T;
		for (i=0; i<1000; i++) sleep_for_us(1);
		ticks_per_usleep = (T - t_start) / 1000;
	}
	row_data.frame_index = 0;
	
#ifdef WANT_SDL
	window = SDL_CreateWindow("Cromemco DAzzLER",
				  SDL_WINDOWPOS_UNDEFINED,
				  SDL_WINDOWPOS_UNDEFINED,
				  window_size, window_size, SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE);
	renderer = SDL_CreateRenderer(window, -1, (SDL_RENDERER_ACCELERATED |
						   SDL_RENDERER_PRESENTVSYNC));
	surface = SDL_GetWindowSurface(window);
#else /* !WANT_SDL */
	XSizeHints *size_hints = XAllocSizeHints();
	Atom wm_delete_window;
	
	display = XOpenDisplay(NULL);
	XLockDisplay(display);
	screen = DefaultScreen(display);
	rootwindow = RootWindow(display, screen);
	XGetWindowAttributes(display, rootwindow, &wa);
	window = XCreateSimpleWindow(display, rootwindow, 0, 0,
				     window_size, window_size, 1, 0, 0);
	XStoreName(display, window, "Cromemco DAzzLER");
	size_hints->flags = PSize | PMinSize | PAspect | PResizeInc;
	size_hints->min_width = canvas_size;
	size_hints->min_height = canvas_size;
	size_hints->base_width = canvas_size;
	size_hints->base_height = canvas_size;
	size_hints->min_aspect.x = 1;
	size_hints->min_aspect.y = 1;
	size_hints->max_aspect.x = 1;
	size_hints->max_aspect.y = 1;
	size_hints->width_inc = 10;
	size_hints->height_inc = 10;
	XSetWMNormalHints(display, window, size_hints);
	XFree(size_hints);

	wm_focused = XInternAtom(display, "_NET_WM_STATE_FOCUSED", 1);
    	wm_maxhorz = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", 1);
    	wm_maxvert = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", 1);
    	wm_hidden = XInternAtom(display, "_NET_WM_STATE_HIDDEN", 1);		
	wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(display, window, &wm_delete_window, 1);

	XSelectInput(display, window, StructureNotifyMask | PropertyChangeMask);

	colormap = DefaultColormap(display, 0);
	gc = XCreateGC(display, window, 0, NULL);
	XSetFillStyle(display, gc, FillSolid);
	pixmap = XCreatePixmap(display, rootwindow, window_size, window_size,
			       wa.depth);

	XParseColor(display, colormap, color0, &colors[0]);
	XAllocColor(display, colormap, &colors[0]);
	XParseColor(display, colormap, color1, &colors[1]);
	XAllocColor(display, colormap, &colors[1]);
	XParseColor(display, colormap, color2, &colors[2]);
	XAllocColor(display, colormap, &colors[2]);
	XParseColor(display, colormap, color3, &colors[3]);
	XAllocColor(display, colormap, &colors[3]);
	XParseColor(display, colormap, color4, &colors[4]);
	XAllocColor(display, colormap, &colors[4]);
	XParseColor(display, colormap, color5, &colors[5]);
	XAllocColor(display, colormap, &colors[5]);
	XParseColor(display, colormap, color6, &colors[6]);
	XAllocColor(display, colormap, &colors[6]);
	XParseColor(display, colormap, color7, &colors[7]);
	XAllocColor(display, colormap, &colors[7]);
	XParseColor(display, colormap, color8, &colors[8]);
	XAllocColor(display, colormap, &colors[8]);
	XParseColor(display, colormap, color9, &colors[9]);
	XAllocColor(display, colormap, &colors[9]);
	XParseColor(display, colormap, color10, &colors[10]);
	XAllocColor(display, colormap, &colors[10]);
	XParseColor(display, colormap, color11, &colors[11]);
	XAllocColor(display, colormap, &colors[11]);
	XParseColor(display, colormap, color12, &colors[12]);
	XAllocColor(display, colormap, &colors[12]);
	XParseColor(display, colormap, color13, &colors[13]);
	XAllocColor(display, colormap, &colors[13]);
	XParseColor(display, colormap, color14, &colors[14]);
	XAllocColor(display, colormap, &colors[14]);
	XParseColor(display, colormap, color15, &colors[15]);
	XAllocColor(display, colormap, &colors[15]);

	XParseColor(display, colormap, gray0, &grays[0]);
	XAllocColor(display, colormap, &grays[0]);
	XParseColor(display, colormap, gray1, &grays[1]);
	XAllocColor(display, colormap, &grays[1]);
	XParseColor(display, colormap, gray2, &grays[2]);
	XAllocColor(display, colormap, &grays[2]);
	XParseColor(display, colormap, gray3, &grays[3]);
	XAllocColor(display, colormap, &grays[3]);
	XParseColor(display, colormap, gray4, &grays[4]);
	XAllocColor(display, colormap, &grays[4]);
	XParseColor(display, colormap, gray5, &grays[5]);
	XAllocColor(display, colormap, &grays[5]);
	XParseColor(display, colormap, gray6, &grays[6]);
	XAllocColor(display, colormap, &grays[6]);
	XParseColor(display, colormap, gray7, &grays[7]);
	XAllocColor(display, colormap, &grays[7]);
	XParseColor(display, colormap, gray8, &grays[8]);
	XAllocColor(display, colormap, &grays[8]);
	XParseColor(display, colormap, gray9, &grays[9]);
	XAllocColor(display, colormap, &grays[9]);
	XParseColor(display, colormap, gray10, &grays[10]);
	XAllocColor(display, colormap, &grays[10]);
	XParseColor(display, colormap, gray11, &grays[11]);
	XAllocColor(display, colormap, &grays[11]);
	XParseColor(display, colormap, gray12, &grays[12]);
	XAllocColor(display, colormap, &grays[12]);
	XParseColor(display, colormap, gray13, &grays[13]);
	XAllocColor(display, colormap, &grays[13]);
	XParseColor(display, colormap, gray14, &grays[14]);
	XAllocColor(display, colormap, &grays[14]);
	XParseColor(display, colormap, gray15, &grays[15]);
	XAllocColor(display, colormap, &grays[15]);

	XMapWindow(display, window);
	XUnlockDisplay(display);
#endif /* !WANT_SDL */
}

/* close the SDL or X11 window for DAZZLER display */
static void close_display(void)
{
#ifdef WANT_SDL
	SDL_DestroyRenderer(renderer);
	renderer = NULL;
	SDL_DestroyWindow(window);
	window = NULL;
#else
	XLockDisplay(display);
	XFreePixmap(display, pixmap);
	XFreeGC(display, gc);
	XUnlockDisplay(display);
	XCloseDisplay(display);
	display = NULL;
#endif
}

#if !defined(WANT_SDL) || defined(HAS_NETSERVER)
static void kill_thread(void)
{
	if (thread != 0) {
		sleep_for_ms(50);
		pthread_cancel(thread);
		pthread_join(thread, NULL);
		thread = 0;
	}
}
#endif

/* switch DAZZLER off from front panel */
void cromemco_dazzler_off(void)
{
#if 0
	int frame, row;
#endif

	last_state = state;
	state = false;

#ifdef WANT_SDL
#ifdef HAS_NETSERVER
	if (!n_flag) {
#endif
		if (dazzler_win_id >= 0) {
			simsdl_destroy(dazzler_win_id);
			dazzler_win_id = -1;
		}
#ifdef HAS_NETSERVER
	} else {
		kill_thread();
		ws_clear();
	}
#endif
#else /* !WANT_SDL */
	kill_thread();
	if (display != NULL)
		close_display();
#ifdef HAS_NETSERVER
	if (n_flag)
		ws_clear();
#endif
#endif /* !WANT_SDL */

#if 0
	/* ouput debug data */
	if (dazzler_line_sync) {
		for (frame=0; frame<row_data.frame_index; frame++) {
			for (row=0; row<64; row++) {
				printf("frame=%d row=%d cycle=%d ticks=%d gap=%d\n",
				frame, row, row_data.cycle[frame], row_data.ticks[frame][row], row_data.gap[frame][row]);
			}
		}
	}
#endif
}

#ifdef WANT_SDL

/* process SDL event */
static void process_event(SDL_Event *event)
{
	switch(event->type) {
	case SDL_WINDOWEVENT:
		if ((event->window.event == SDL_WINDOWEVENT_RESIZED) ||
			(event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) ||
			(event->window.event == SDL_WINDOWEVENT_MAXIMIZED) ||
			(event->window.event == SDL_WINDOWEVENT_RESTORED)) {
			window_resized = true;
		}
		break;
	default:;
	}
}

static inline void set_fg_color(int i)
{
	SDL_SetRenderDrawColor(renderer,
			       colors[i][0], colors[i][1], colors[i][2],
			       SDL_ALPHA_OPAQUE);
}

static inline void set_fg_gray(int i)
{
	SDL_SetRenderDrawColor(renderer,
			       grays[i][0], grays[i][1], grays[i][2],
			       SDL_ALPHA_OPAQUE);
}

static inline void fill_rect(int x, int y, int w, int h)
{
	SDL_Rect r = {x, y, w, h};

	SDL_RenderFillRect(renderer, &r);
}

#else /* !WANT_SDL */

static inline void set_fg_color(int i)
{
	XSetForeground(display, gc, colors[i].pixel);
}

static inline void set_fg_gray(int i)
{
	XSetForeground(display, gc, grays[i].pixel);
}

static inline void fill_rect(int x, int y, int w, int h)
{
	XFillRectangle(display, pixmap, gc, x, y, w, h);
}

#endif /* !WANT_SDL */

/*
	Draw scanlines for a full frame (time correct)

	Dazzler timings
	
	  3.579545 MHz hardware clock
	  1.790 MHz pixel clock
	  15.98 KHz line frequency
	  62 Hz vertical frequency (interlaced)
	  Vertical scan 12 ms
	  Vertical blank 4 ms
	  DMA cycle 375 us
	  12 scanlines/pixel (low resolution nibble mode, 32x32)
	  6 scanlines/pixel (medium resolution nibble mode, 64x64)
	  3 scanlines/pixel (high resolution x4 mode, 128x128)
	  384 scanlines per frame
	  192 scanlines per field (interlaced)
	  16 or 32 memory locations per line, depending on the video mode
	
	Parameters
	
	The field value identifies either even field (field=EVEN),
	odd field(field=ODD), or both fields (field = FULL)
	
	How it works
	
	The whole field is divided into DMA cycles, where the Dazzler board
	fetches the display data from the main memory at the memory address
	defined in the address register accessible via I/O port 0xE.
	Depending on the current video mode, the Dazzler fetches either
	16 or 32 bytes per DMA cycle every 375 microseconds.
	
	The data is copied into a 4x64-bit shift register, which operates
	as a cache ("recycle buffer") for up to 64 nibbles, so that the pixel
	data can be streamed for each following scanline without the need
	for re-fetching the data from main memory. Each DMA cycle covers 12
	scanlines in 512 byte mode, and 6 scanlines in 2K byte mode.
	
	For accurate emulation, the host actually should be put into hold mode
	during the DMA fetch, which in the real hardware is slowing down
	processing by roughly 15%. This, however, has not yet been implemented
	yet in the emulation, so the host is always running at full speed.
	
	Implementation

	Just as with the real Dazzler hardware, the scanline counter triggers
	the DMA cycles for "stealing" the pixel data from the video buffer
	memory. The meaning of the fetched pixel data depends on the current
	video mode, which is controlled by the format register accesible
	through I/O port 0xF. The content of both Dazzler registers are always
	effective, which means that they can be altered on-the-fly during
	display refresh.
	
	While normally changes of display memory and registers should be done
	during the vertical blank period (in order not to disturb a stable
	picture), also intentional tweaking is possible (and originally
	intended by the designers, see Dazzler patent documentation).
	
	However, in order to control the display in real time, the host needs
	to know the current position of the CRT beam. The Dazzler offers
	the appropriate reference in the flags register, which can be accessed
	via I/O port 0xF.
	
	The flags register holds one flag called end-of-frame, indicating the
	vertical scan vs. vertical retrace period, and another flag called
	odd-line-even-line, indicating even vs. odd DMA	cycles (rather than
	even vs. odd scanlines):
	
	   +---+---+---+---+---+---+---+---+
	   | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
	   +---+---+---+---+---+---+---+---+
	     |   |   |   |   |   |   |   |
	     |   |   +---+---+---+---+---+--- don't care
	     |   +--- vertical blank
	     +------- odd-line-even-line

	Bit 6 can the used to perform changes on the display data during
	vertical blank without breaking the picture. Bit 7 can be used to
	track the switches between DMA cycles / scanline groups subsequently
	written to the display. Tracking those groups from the start of the
	screen gives the current vertical position of the beam.
	
	The end-of-frame flag also can be used as real time reference, as the 
	flag is changing with 62 Hz. The odd-line-even-line flag is only active
	during the scan period when bit 6 is true.
	
	The reference to "even lines/odd lines" is a bit misleading, since that
	flag actually refers to the DMA cycles, which are matching display
	lines only in the nibble mode, not in the x4 mode, and are especially
	not referring scanlines. Normally the emulator draws a full frame as
	fast as possible, sleeping for the rest of the vertical period for
	being in sync with the vertical frequency. If an application requires
	syncing to the DMA cycles (e.g. for tracking the line flag), you
	can enable line sync by sttting the variable dazzler_line_sync in
	the system.conf file to 1.
	
	The Dazzler hardware is always operating in interlaced mode, where the
	display shows fields of even and odd scanlines alternating with a
	vertical frequency of 62 Hz.
	
	Because X Windows actually can't fully keep up drawing interlaced
	fields, this emulation by default runs in a flickerless	non-interlaced
	mode, which flattens all scanlines into a single frame with 31 Hz
	refresh. You can switch to the more accurate interlaced mode by setting
	the dazzler_interlaced property in the system.conf file to 1.
	
	Most accurate timing therefore can be configured by setting both
	dazzler_interaced and dazzler_line_sync in the sytem.conf file to 1.
	
	Neither Linux nor Windows are offering an accurate sleep function
	for descheduling the current thread for a certain amount of time.
	The z80pack functions sleep_for_ms() and sleep_for_us() both can
	result in almost any delay. Our workaround here is using
	sleep_for_us(1), which is the shortest possible latency, and
	synchronizes with the state clock of the emulated CPU. Not perfect,
	but seems to deliver the best possible results.
*/
static Tstates_t dazzler_busmaster(BYTE bus_ack)
{
	int num_bytes;

	if (!bus_ack) return 0;

	num_bytes = format & 0x20 ? 32 : 16;

#if 0
	/* read DMA memory into line buffer */
	int bytepos, offset;
	for (bytepos=0; bytepos<num_bytes; bytepos++) {
		offset = bytepos % 16;
		if (format & 0x20) {
			/* add quadrant offset */
			if (bytepos > 15) offset += 512;
			if (scanline > 191) offset += 512;
		}
		line_buffer[bytepos] = dma_read(addr + offset);
	}
#endif

	/* simulate bus master activity by returning t-states, slowing down CPU by about 15% */
	return num_bytes * 3;  /* 3 t-states per byte of DMA */
}

static void draw_field(int field)
{
	int bytepos, num_bytes, num_dma, num_lines, current_line, psize, offset, start, step, dma_cycle;
	BYTE i;
	int hires_subrow, vpos;

	Tstates_t T_end_of_row;

	step = (field == FULL) ? 1 : 2;		/* single or dual scanline */
	start = (field == ODD) ? 1 : 0;		/* first scanline, depending on even/odd field */

	addr = dma_addr;

	current_line = 0;			/* relatve scanline within current DMA cycle */
	hires_subrow = 0;			/* sub row for x4 more, either 0 or 1 */
	
	row_data.row_index = 0;
	
	/* clear the line flag */
	flags &= 0x7f;

	/* select foreground color for hires mode */
	if (format & 0x40) {
		i = format & 0x0f;
		if (format & 0x10) 
			set_fg_color(i);
		else
			set_fg_gray(i);
	}

	/* now draw the frame */
	for (scanline=start; scanline<384; scanline+=step) {

		/* register control - can be changed on the fly */
		num_bytes = format & 0x20 ? 32 : 16;			/* bytes per DMA cycle */
		num_dma = format & 0x20 ? 64 : 32;			/* DMA cycles per frame */
		num_lines = 384 / num_dma;				/* scanlines per DMA cycle */
		if (format & 0x40) psize = 192 / num_dma * pscale;	/* hires monochrome (x4 mode) */
		else psize = 384 / num_dma * pscale;			/* color/grayscale (nibble) mode */
		vpos = scanline * pscale;

		if (current_line == 0) {
			hires_subrow = 0;

			/* calculate DMA cycle */
			dma_cycle = (num_lines * f_value * 1000000) / 15980;;
			row_data.cycle[row_data.frame_index] = dma_cycle;
			T_end_of_row = T + dma_cycle;

			/* read data bytes via DMA & write into display pixmap */
			for (bytepos=0; bytepos<num_bytes; bytepos++) {
				/* read DMA memory into line buffer */
				offset = bytepos % 16;
				if (format & 0x20) {
					/* add quadrant offset */
					if (bytepos > 15) offset += 512;
					if (scanline > 191) offset += 512;
				}
				line_buffer[bytepos] = dma_read(addr + offset);
			}

			/* simulate bus master activity */
			if (dazzler_line_sync)
				start_bus_request(BUS_DMA_CONTINUOUS, &dazzler_busmaster);
		}

		for (bytepos=0; bytepos<num_bytes; bytepos++) {

			if (format & 0x40) {	/* x4 mode */
				/* render pixels */
				i = line_buffer[bytepos];
				if (hires_subrow == 0) {
					/* first 3 scanline subrow */
					if (i & 0x01)
						fill_rect(bytepos * 4 * psize, vpos, psize, 1);
					if (i & 0x02)
						fill_rect((bytepos * 4 + 1) * psize, vpos, psize, 1);
					if (i & 0x10)
						fill_rect((bytepos * 4 + 2) * psize, vpos, psize, 1);
					if (i & 0x20)
						fill_rect((bytepos * 4 + 3) * psize, vpos, psize, 1);
				} else {
					/* second 3 scanline subrow */
					if (i & 0x04)
						fill_rect(bytepos * 4 * psize, vpos, psize, 1);
					if (i & 0x08)
						fill_rect((bytepos * 4 + 1) * psize, vpos, psize, 1);
					if (i & 0x40)
						fill_rect((bytepos * 4 + 2) * psize, vpos, psize, 1);
					if (i & 0x80)
						fill_rect((bytepos * 4 + 3) * psize, vpos, psize, 1);
				}
			}
			else {	/* nibble mode */
				/* first pixel */
				i = line_buffer[bytepos] & 0x0f;
				if (format & 0x10) {
					set_fg_color(i);	/* color */
				}
				else {
					set_fg_gray(i);		/* grayscale */
				}
				fill_rect(bytepos * 2 * psize, vpos, psize, 1);				

				/* second pixel */
				i = (line_buffer[bytepos] & 0xf0) >> 4;
				if (format & 0x10) {
					set_fg_color(i);	/* color */
				}
				else {
					set_fg_gray(i);		/* grayscale */
				}
				fill_rect((bytepos * 2 + 1) * psize, vpos, psize, 1);				
			}
		}

		current_line += step;
			
		if (format & 0x40) {
			/* check which subrow we're in */
			hires_subrow = (current_line + start) / 3;
		}

		/* post processing after last line */
		if (current_line >= num_lines) {
			if (dazzler_line_sync) {
				/* collect some debug data */
				if (row_data.frame_index < 10) {
					row_data.ticks[row_data.frame_index][row_data.row_index] = T_end_of_row - T;
				}

				/* wait until end of row */
				while ((T < (T_end_of_row - ticks_per_usleep)) && (cpu_state == ST_CONTIN_RUN))
					sleep_for_us(1);
				
				/* collect some more debug data */
				if (row_data.frame_index < 10) {
					row_data.gap[row_data.frame_index][row_data.row_index] = T_end_of_row - T;
					row_data.row_index++;
					if (row_data.row_index == 64) {
						row_data.row_index = 0;
						row_data.frame_index++;
					}
				}				

				T_end_of_row += dma_cycle;
			}
			addr += 16;		/* new start address */
			current_line = 0;	/* reset scanline counter */
			flags ^= 0x80;		/* toggle line flag */
		}
	}
}

#ifdef HAS_NETSERVER
static uint8_t dblbuf[2048];

static struct {
	uint16_t format;
	uint16_t addr;
	uint16_t len;
	uint8_t buf[2048];
} msg;

static void ws_clear(void)
{
	memset(dblbuf, 0, 2048);

	msg.format = 0;
	msg.addr = 0xFFFF;
	msg.len = 0;
	net_device_send(DEV_DZLR, (char *) &msg, msg.len + 6);
	LOGD(TAG, "Clear the screen.");
}

static void ws_refresh(void)
{
	int len = (format & 32) ? 2048 : 512;
	int addr;
	int i, n, x, la_count;
	bool cont;
	uint8_t val;

	for (i = 0; i < len; i++) {
		addr = i;
		n = 0;
		la_count = 0;
		cont = true;
		while (cont && (i < len)) {
			val = dma_read(dma_addr + i);
			while ((val != dblbuf[i]) && (i < len)) {
				dblbuf[i++] = val;
				msg.buf[n++] = val;
				cont = false;
				val = dma_read(dma_addr + i);
			}
			if (cont)
				break;
			x = 0;
#define LOOKAHEAD 6
			/* look-ahead up to n bytes for next change */
			while ((x < LOOKAHEAD) && !cont && (i < len)) {
				val = dma_read(dma_addr + i++);
				msg.buf[n++] = val;
				la_count++;
				val = dma_read(dma_addr + i);
				if ((i < len) && (val != dblbuf[i])) {
					cont = true;
				}
				x++;
			}
			if (!cont) {
				n -= x;
				la_count -= x;
			}
		}
		if (n || (format != formatBuf)) {
			formatBuf = format;
			msg.format = format;
			msg.addr = addr;
			msg.len = n;
			net_device_send(DEV_DZLR, (char *) &msg, msg.len + 6);
			LOGD(TAG, "BUF update 0x%04X-0x%04X "
			     "len: %d format: 0x%02X l/a: %d",
			     msg.addr, msg.addr + msg.len,
			     msg.len, msg.format, la_count);
		}
	}
}
#endif /* HAS_NETSERVER */

#ifdef WANT_SDL
/* function for updating the display */
static void update_display(bool tick)
{
	UNUSED(tick);
	
	int width, height;
	
	Tstates_t T_end;

	field = dazzler_interlaced ? EVEN : FULL;

	/* handling window resize event */
	if (window_resized) {
		window_resized = false;
		SDL_GetWindowSize(window, &width, &height);
		window_size = width > height ? height : width;
		
		if (dazzler_descrete_scale) {
			/* discrete scaling */
			pscale = window_size / canvas_size;
		}
		else {
			/* smooth scaling */
			SDL_SetWindowSize(window, window_size, window_size);
		       	SDL_RenderSetScale(renderer, window_size / 364.0, window_size / 384.0);
		}
	}

	/* draw one frame dependent on graphics format */
	set_fg_color(0);
	SDL_RenderClear(renderer);
	if (state) {		/* draw frame if on */
		if (dazzler_interlaced)
			field = (field == ODD) ? EVEN : ODD;
	       	draw_field(field);
		SDL_RenderPresent(renderer);

		/* frame done, set frame flag for 4 ms vertical blank */
		flags = 0x3f;
		T_end = T + (f_value * 4000);
		while ((T < T_end) && (cpu_state == ST_CONTIN_RUN)) sleep_for_us(1);
		flags |= 0x40;
	} else
		SDL_RenderPresent(renderer);
}

static win_funcs_t dazzler_funcs = {
	open_display,
	close_display,
	process_event,
	update_display
};
#endif

#if !defined(WANT_SDL) || defined(HAS_NETSERVER)
/* thread for updating the X11 display or web server */
static void *update_thread(void *arg)
{
	Tstates_t T_end;

	UNUSED(arg);
	
	field = dazzler_interlaced ? EVEN : FULL;

	while (true) {	/* do forever or until canceled */

		/* draw one frame dependent on graphics format */
		if (state) {		/* draw frame if on */
#ifdef HAS_NETSERVER
			if (!n_flag) {
#endif
#ifndef WANT_SDL
				XEvent event;
				while (XPending(display) > 0) {
					XNextEvent(display, &event);
					switch(event.type) {
					case ConfigureNotify:
						/* check for window resize event */
						XConfigureEvent xce = event.xconfigure;
						if (xce.width != window_size || xce.height != window_size) {
			                		window_size = xce.width < xce.height ? xce.width : xce.height;
							printf("Resize event!\n\r");
							window_resized = true;
						}
						break;
					case PropertyNotify:
						/* check for window maximize/minimize/hidden property change */
						Atom actual_type, prop;
						int actual_format, status;
						unsigned long nitems, bytes_after;
						unsigned char *dp;
						printf("Property: %s\n\r", XGetAtomName(display, event.xproperty.atom));
						if (!strcmp(XGetAtomName(display, event.xproperty.atom), "_NET_WM_STATE")) {
							printf("WM state property notify event!\n\r");
					                do {
					                    status = XGetWindowProperty(display, window, event.xproperty.atom, 0L, 1L, 0, 4/*XA_ATOM*/,
					                    				&actual_type, &actual_format, &nitems, &bytes_after, &dp);
					                    if (status == Success && actual_type == 4/*XA_ATOM*/ && dp && actual_format == 32 && nitems) {
					                        for (unsigned int i = 0; i < nitems; i++) {
					                            prop = (((Atom*)dp)[i]);
					                            if (prop == wm_focused) {
					                            	printf("%d normalized\n\r", i);
									window_resized = true;
								    } else if ((prop == wm_maxhorz) || (prop == wm_maxvert)) {
					                            	printf("%d maximized\n\r", i);
									window_resized = true;
								    } else if (prop == wm_hidden) {
					                            	printf("%d minimized\n\r", i);
									window_resized = true;
								    }
								}
					                    }
					                } while (bytes_after);
					        }
					        if (window_resized) {
					        	XWindowAttributes attributes;
					        	XGetWindowAttributes(display, window, &attributes);
					        	window_size = attributes.width < attributes.height ? attributes.width : attributes.height;
					        }
					        break;
					default:;
					}
				}
				if (window_resized) {
					window_resized = false;
					pscale = window_size / canvas_size;
					XFreePixmap(display, pixmap);
					pixmap = XCreatePixmap(display, rootwindow, window_size, window_size, wa.depth);
				}
				XLockDisplay(display);
				set_fg_color(0);
				fill_rect(0, 0, window_size, window_size);
				if (dazzler_interlaced)
					field = (field == ODD) ? EVEN : ODD;
	        		draw_field(field);
				XCopyArea(display, pixmap, window, gc, 0, 0,
					  window_size, window_size, 0, 0);
				XSync(display, True);
				XUnlockDisplay(display);
#endif
#ifdef HAS_NETSERVER
			} else {
				if (net_device_alive(DEV_DZLR)) {
					ws_refresh();
				} else {
					if (msg.format) {
						memset(dblbuf, 0, 2048);
						msg.format = 0;
					}
				}
			}
#endif
		}
		else {
#ifdef HAS_NETSERVER
			if (!n_flag) {
#endif
#ifndef WANT_SDL
				if (last_state) {
					XLockDisplay(display);
					XClearWindow(display, window);
					XSync(display, True);
					XUnlockDisplay(display);
					last_state = false;
				}
#endif /* !WANT_SDL */
#ifdef HAS_NETSERVER
			}
#endif
			sleep_for_us(12129);
		}

		/* frame done, set frame flag for 4 ms vertical blank */
		flags = 0x3f;
		T_end = T + (f_value * 4000);
		while ((T < T_end) && (cpu_state == ST_CONTIN_RUN)) sleep_for_us(1);
		flags |= 0x40;
	}

	/* just in case it ever gets here */
	pthread_exit(NULL);
}
#endif /* !WANT_SDL || !HAS_NETSERVER */

void cromemco_dazzler_ctl_out(BYTE data)
{
	/* get DMA address for display memory */
	dma_addr = (data & 0x7f) << 9;

	/* switch DAZZLER on/off */
	if (data & 128) {
#ifdef HAS_NETSERVER
		if (!n_flag) {
#endif
#ifdef WANT_SDL
			if (dazzler_win_id < 0)
				dazzler_win_id = simsdl_create(&dazzler_funcs);
#else
			if (display == NULL)
				open_display();
#endif
#ifdef HAS_NETSERVER
		} else {
			if (!state)
				ws_clear();
		}
#endif
		last_state = state;
		state = true;
#if defined(WANT_SDL) && defined(HAS_NETSERVER)
		if (n_flag) {
#endif
#if !defined(WANT_SDL) || defined(HAS_NETSERVER)
			if (thread == 0) {
				if (pthread_create(&thread, NULL, update_thread,
						   NULL)) {
					LOGE(TAG, "can't create thread");
					exit(EXIT_FAILURE);
				}
			}
#endif
#if defined(WANT_SDL) && defined(HAS_NETSERVER)
		}
#endif
	} else {
		if (state) {
			last_state = state;
			state = false;
#ifdef HAS_NETSERVER
			sleep_for_ms(50);
			if (n_flag) ws_clear();
#endif
		}
	}
}

BYTE cromemco_dazzler_flags_in(void)
{
	BYTE data = 0xff;

#ifdef WANT_SDL
#ifdef HAS_NETSERVER
	if (!n_flag) {
#endif
		if (dazzler_win_id >= 0)
			data = flags;
#ifdef HAS_NETSERVER
	} else {
		if (thread != 0)
			data = flags;
	}
#endif
#else /* !WANT_SDL */
	if (thread != 0)
		data = flags;
#endif /* !WANT_SDL */

	return data;
}

void cromemco_dazzler_format_out(BYTE data)
{
	format = data;
}

#endif /* HAS_DAZZLER */
