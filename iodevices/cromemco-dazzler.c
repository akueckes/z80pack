/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Common I/O devices used by various simulated machines
 *
 * Copyright (C) 2015-2019 by Udo Munk
 * Copyright (C) 2018 David McNaughton
 * Copyright (C) 2025 by Thomas Eberhardt
 * Copyright (C) 2025 Ansgar Kueckes
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
 * 06-JUN-2025 added support for more better performance, accurate timing, interlaced video, odd-even-line flag and window resize
*/

/*
	The previous rendering implementation used API calls for every write to the video
	buffer, it has been replaced by a variety of much more efficient drawing methods
	which happen in the client itself, before transferring the image as a whole with
	one single API call to the X server.

	For Dazzler emulation, in total three different client-based rendering methods are
	supported. With SDL2 enabled, rendering via surfaces and via textures are supported.
	Surface rendering is the fastest drawing method with SDL2 for bitblit operations
	as used with the Dazzler emulation, whereas rendering based on textures is in general
	more flexible. It can utilize the full range of rendering features supported by
	SDL2 (e.g. free scaling or hardware acceleration), but with overall lower performance
	for bitblit operations.
	
	The fastest rendering with lowest overhead however can be achieved by directly writing
	to XImage bitmaps. Free scaling then can be done without performance tradeoffs by using
	the X Rendering Extension.
	
	If neither texture based rendering is used, nor the X Rendering Extension is available,
	scaling has to be performed within the client, passing over the scaled image to the
	X Server. The original image can be scaled by full multiples by configuring
	dazzler_discrete_scaling to 1 on the config appropriate system.conf file.
	
	Measured timings:

	rendering	pure drawing	with vertical	with minimum	line sync with     sync with CPU
	method		(no sleeps)	blank period	sleeps per row	CPU clock (0.5K)   clock (2K)

	XImage: 	205 frames/s	92 frames/s	82 frames/s	19 frames/s	   13 frames/s
	Surface:	58 frames/s	39 frames/s	44 frames/s	19 frames/s	   12 frames/s
	Texture:	37 frames/s	28 frames/s	27 frames/s	12 frames/s	    7 frames/s
	
	Obviously, most accurate timing (62 Hz with vertical blank) can be achieved by using
	XImage rendering.
*/

/* SDL rendering options */
#define SURFACE	0			/* fastest rendering option */
#define TEXTURE 1			/* still fast, and can scale freely */
#define SDL_RENDER_MODE TEXTURE		/* define to either SURFACE or TEXTURE */
#define BUSMASTER			/* define to simulate busmaster operation for Dazzler DMA */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#ifndef WANT_SDL
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>
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
bool dazzler_discrete_scale = false;	/* no discrete window scaling by default */

/* SDL2/X11 stuff */
#define WSIZE 384
static int canvas_size = WSIZE;
static int window_size = WSIZE;
static int pscale = 1;
static bool window_resized = false;

#ifdef WANT_SDL
static int dazzler_client_id = -1;
static SDL_Window *window;
#if SDL_RENDER_MODE == SURFACE
static SDL_Surface *surface;
static Uint32 fg;
static Uint32 *canvas = NULL;
#else
static SDL_Renderer *renderer;
static SDL_Texture *canvas;
#endif
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
static BYTE *canvas;
static bool has_xrender_extension = false;
static XRenderPictFormat *pict_format;
static XImage *ximage;
static Picture canvas_pic, window_pic;
static XTransform transform;
static Display *display;
static Window window;
static GC gc;
static XVisualInfo vinfo;
static Window rootwindow;
static XWindowAttributes wa;
static Atom wm_focused, wm_maxhorz, wm_maxvert, wm_hidden;
static Pixmap pixmap;
static Colormap colormap;
static XColor colors[16];
static XColor grays[16];
static XColor *fg;
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
static int field;
static int scanline;

static int ticks_per_nanosleep = 150;
static struct timespec min_sleep_time = { 0, 1 };

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

/* stats data */
static struct {
	uint8_t flags[10][64];
	uint8_t format[10][64];
	int loop[10][64];
	uint64_t T1[10][64];
	uint64_t T2[10][64];
	uint64_t T3[10][64];
	int headroom[10][64];
	int rest[10][64];
	int cycle[10];
	int vblank[10];
	int row_index;
	int frame_index;
} row_data;
static int frames = 0;
static uint64_t start_time;

/* create the SDL2 or X11 window for DAZZLER display */
static void open_display(void)
{
	/* calibrate sleep timer (nanosleeps per ms) */
	if (dazzler_line_sync) {
		uint64_t t_start = T;
		for (int i=0; i<1000; i++) nanosleep(&min_sleep_time, NULL);
		if (T > t_start) ticks_per_nanosleep = (T - t_start) / 1000;
	}

	field = dazzler_interlaced ? EVEN : FULL;

#ifdef WANT_SDL
#if SDL_RENDER_MODE == SURFACE
	window = SDL_CreateWindow("Cromemco DAzzLER",
				  SDL_WINDOWPOS_UNDEFINED,
				  SDL_WINDOWPOS_UNDEFINED,
				  window_size, window_size,
				  SDL_WINDOW_SHOWN|(dazzler_discrete_scale ? SDL_WINDOW_RESIZABLE : 0));
	surface = SDL_GetWindowSurface(window);
#else /* TEXTURE */
	window = SDL_CreateWindow("Cromemco DAzzLER",
				  SDL_WINDOWPOS_UNDEFINED,
				  SDL_WINDOWPOS_UNDEFINED,
				  window_size, window_size, SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE);
	renderer = SDL_CreateRenderer(window, -1, (SDL_RENDERER_ACCELERATED |
						   SDL_RENDERER_PRESENTVSYNC));
	canvas = SDL_CreateTexture(renderer,
                                   SDL_PIXELFORMAT_RGBA8888,
                                   SDL_TEXTUREACCESS_TARGET,
                                   canvas_size, canvas_size);
#endif /* SDL_RENDER_MODE */
#else /* !WANT_SDL */
	XSizeHints *size_hints = XAllocSizeHints();
	Atom wm_delete_window;
    	int first_event, first_error;
    	int screen;
	
	display = XOpenDisplay(NULL);
	if (!display) {
		printf("Could not open display, please ensure X Server is running and DISPLAY is set\n\r");
		exit(-1);
	}

	XLockDisplay(display);
	screen = DefaultScreen(display);
	rootwindow = RootWindow(display, screen);
	XGetWindowAttributes(display, rootwindow, &wa);
	window = XCreateSimpleWindow(display, rootwindow, 0, 0,
				     window_size, window_size, 1, 0, 0);
	XStoreName(display, window, "Cromemco DAzzLER");

	wm_focused = XInternAtom(display, "_NET_WM_STATE_FOCUSED", 0);
    	wm_maxhorz = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", 0);
    	wm_maxvert = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", 0);
    	wm_hidden = XInternAtom(display, "_NET_WM_STATE_HIDDEN", 0);		
	wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(display, window, &wm_delete_window, 1);
	
	XSelectInput(display, window, StructureNotifyMask | PropertyChangeMask);

	colormap = DefaultColormap(display, 0);
	gc = XCreateGC(display, window, 0, NULL);
	XSetFillStyle(display, gc, FillSolid);
	pixmap = XCreatePixmap(display, rootwindow, canvas_size, canvas_size, wa.depth);

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
	
	/* Create an XImage structure that points to our buffer */
    	XMatchVisualInfo(display, screen, 24, TrueColor, &vinfo);
    	canvas = malloc(canvas_size * canvas_size * 4);
        ximage = XCreateImage(display, vinfo.visual, 24, ZPixmap, 0, (char *)canvas,
                                  canvas_size, canvas_size, 32, 0);

	/* XRenderExtension stuff */
    	if (XRenderQueryExtension(display, &first_event, &first_error)) {
		has_xrender_extension = true;
		pict_format = XRenderFindVisualFormat(display, DefaultVisual(display, screen));
		window_pic = XRenderCreatePicture(display, window, pict_format, 0, NULL);
		transform.matrix[0][0] = XDoubleToFixed(1);
		transform.matrix[0][1] = XDoubleToFixed(0);
		transform.matrix[0][2] = XDoubleToFixed(0);
		transform.matrix[1][0] = XDoubleToFixed(0);
		transform.matrix[1][1] = XDoubleToFixed(1);
		transform.matrix[1][2] = XDoubleToFixed(0);
		transform.matrix[2][0] = XDoubleToFixed(0);
		transform.matrix[2][1] = XDoubleToFixed(0);
		transform.matrix[2][2] = XDoubleToFixed(1);					
	}
	
	/* size hints */
	size_hints->flags = PSize | PMinSize | PMaxSize | PAspect;
	size_hints->min_width = WSIZE;
	size_hints->min_height = WSIZE;
	size_hints->base_width = WSIZE;
	size_hints->base_height = WSIZE;
	size_hints->max_width = WSIZE;
	size_hints->max_height = WSIZE;
	size_hints->min_aspect.x = 1;
	size_hints->min_aspect.y = 1;
	size_hints->max_aspect.x = 1;
	size_hints->max_aspect.y = 1;

	if (has_xrender_extension || dazzler_discrete_scale)
		size_hints->flags = PSize | PMinSize | PAspect;

	XSetWMNormalHints(display, window, size_hints);
	XFree(size_hints);

	XMapWindow(display, window);
	XUnlockDisplay(display);
#endif /* !WANT_SDL */
}

/* close the SDL or X11 window for DAZZLER display */
static void close_display(void)
{
#ifdef WANT_SDL
#if SDL_RENDER_MODE == TEXTURE
	SDL_DestroyRenderer(renderer);
	renderer = NULL;
	SDL_DestroyTexture(canvas);
	canvas = NULL;
#endif
	SDL_DestroyWindow(window);
	window = NULL;
#else
	XLockDisplay(display);
	ximage->data = NULL;
	XDestroyImage(ximage);
	XFreePixmap(display, pixmap);
	XFreeGC(display, gc);
	XDestroyWindow(display, window);
	XUnlockDisplay(display);
	XCloseDisplay(display);
	free(canvas);
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
	last_state = state;
	state = false;

#ifdef WANT_SDL
#ifdef HAS_NETSERVER
	if (!n_flag) {
#endif
		if (dazzler_client_id >= 0) {
			simsdl_destroy(dazzler_client_id);
			dazzler_client_id = -1;
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
#if SDL_RENDER_MODE == SURFACE
			surface = SDL_GetWindowSurface(window);		/* try to avoid dangling pointer/out-of-bounds */
#endif /* SDL_REDNDER_MODE */
			window_resized = true;
		}
		break;
	default:;
	}
}

static inline void set_fg_color(int i)
{
#if SDL_RENDER_MODE == SURFACE
	fg = SDL_MapRGB(surface->format, colors[i][0], colors[i][1], colors[i][2]);
#else /* TEXTURE */
	SDL_SetRenderDrawColor(renderer,
			       colors[i][0], colors[i][1], colors[i][2],
			       SDL_ALPHA_OPAQUE);
#endif /* SDL_RENDER_MODE */
}

static inline void set_fg_gray(int i)
{
#if SDL_RENDER_MODE == SURFACE
	fg = SDL_MapRGB(surface->format, grays[i][0], grays[i][1], grays[i][2]);
#else /* TEXTURE */
	SDL_SetRenderDrawColor(renderer,
			       grays[i][0], grays[i][1], grays[i][2],
			       SDL_ALPHA_OPAQUE);
#endif /* SDL_RENDER_MODE */

}

static inline void fill_rect(int x, int y, int w, int h)
{
#if SDL_RENDER_MODE == SURFACE
	register int i,j,x_max,y_max;

	if (window_resized) return;	/* try to catch surface re-allocation cause by window resize */

	x_max = x + w;
	y_max = y + h;
	for (j = y; j < y_max; j++) {
        	for (i = x; i < x_max; i++) {
        		canvas[(j * surface->w) + i] = fg;
        	}
        }
#else /* TEXTURE */
	SDL_Rect r = {x, y, w, h};

	SDL_RenderFillRect(renderer, &r);
#endif /* SDL_RENDER_MODE */
}	

#else /* !WANT_SDL */

static inline void set_fg_color(int i)
{
	fg = &colors[i];
}

static inline void set_fg_gray(int i)
{
	fg = &grays[i];
}

static inline void fill_rect(int x, int y, int w, int h)
{
    register int i,j,x_max,y_max;
    
    x_max = x + w;
    y_max = y + h;
    if (x_max > canvas_size) x_max = canvas_size;
    if (y_max > canvas_size) y_max = canvas_size;
    
    for (j = y; j < y_max; j++) {
	    for (i = x; i < x_max; i++) {
	            long offset = ((j * canvas_size) + i) * 4;	/* RGBA */
	            canvas[offset] = fg->blue;
	            canvas[offset + 1] = fg->green;
	            canvas[offset + 2] = fg->red;
	            canvas[offset + 3] = 255;			/* alpha */
	    }
    }
}

#endif /* !WANT_SDL */

#ifdef BUSMASTER
static Tstates_t dazzler_busmaster(BYTE bus_ack)
{
	int num_bytes;

	if (!bus_ack) return 0;

	num_bytes = format & 0x20 ? 32 : 16;

#if 0
	/* read DMA memory into line buffer (currently won't work with reliable timing) */
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
#endif /* BUSMASTER */

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
	processing by roughly 15%.
	
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
	not referring scanlines.
	
	The original hardware puts the CPU into a hold state during DMA transfers,
	which should be implemented by calling the emulation's bus request function,
	which is also used here with every DMA cycle.
	
	The real Dazzler hardware is always operating in interlaced mode, where
	the display shows fields of even and odd scanlines alternating with a
	vertical frequency of 62 Hz.
	
	By default, this emulation by default runs in a flickerless non-interlaced
	mode, which flattens all scanlines into a single frame with up to 62 Hz
	refresh. You can switch to the visually more accurate interlaced mode
	by setting the dazzler_interlaced property in the system.conf file to 1.
	
	Neither Linux nor Windows are offering an accurate sleep function
	for descheduling the current thread for a certain amount of time.
	The z80pack functions sleep_for_ms() and sleep_for_us() both can
	result in almost any delay. Our workaround here is using POSIX
	nanosleep() with the shortest possible latency, and try to synchronize
	with the state clock of the emulated CPU as good as possible.
	
	However, even a call to nanosleep() eventually can take more time than
	a full Dazzler DMA cycle, and since at least one call to nanosleep() is
	required to grant the application time to test the odd-line-even-line
	flag, the total amount of sleeps during a frame will be above
	the frame scan time of the original hardware.
	
	As a result, the framerate will not be exact 62Hz any more, but the
	odd-line-even-line flag will be set appropriately. Not perfect,
	but probably the only way to serve applications using that flag. You
	can activate the odd-line-even-line flag by setting dazzler_line_sync
	to 1 in the config file.

	X11 out of the box won't support free scaling of window contents (canvas).
	This normally is handled efficiently by the Xrender extension or by the
	SDL2 framework, both using the capabilities of the display hardware.
	If this extension is not supported by the X server, resizing the Dazzler
	window by default is disabled.
	
	Free scaling can, however, lead to unwanted effects in that the pixels
	have uneven sizes. This can best be avoided by limiting scale to
	full multiples of the original Dazzler display geometry, which can
	be activated by setting dazzler_discrete_scale to 1 in the config
	file. This also activates scaling without Xrender extension, which
	is then done by adjusting the pixel size already when drawing the canvas.
	As a consequence, no scaling is required any more during rendering
	in the X server.

	Functions parameters:

	The field value identifies either even field (field=EVEN),
	odd field(field=ODD), or both fields (field = FULL)
*/
static void draw_field(int field)
{
	int bytepos, num_bytes, num_dma, num_lines, current_line, psize, offset, start, step, hires_subrow, vpos;
	int dma_cycle = 0;
	uint8_t i;

	Tstates_t T_end_of_row = 0;

	frames++;
	
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

			/* calculate DMA cycle in CPU ticks */
			dma_cycle = (num_lines * f_value * 1000000) / 15980;;
#ifdef PROFILER
			if (row_data.frame_index < 10) {
				row_data.cycle[row_data.frame_index] = dma_cycle;
				row_data.T1[row_data.frame_index][row_data.row_index] = T;
				row_data.flags[row_data.frame_index][row_data.row_index] = flags;
				row_data.format[row_data.frame_index][row_data.row_index] = format;
			}
#endif
			T_end_of_row = T + dma_cycle;

			/* read data bytes into line buffer */
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

			if (format & 0x40) {	/* x4 mode */
				if (format & 0x10) {
					set_fg_color(format & 0xf);	/* color */
				}
				else {
					set_fg_gray(format & 0xf);	/* grayscale */
				}
			}
#ifdef BUSMASTER
			/* simulate bus master activity */
			if (dazzler_line_sync)
				start_bus_request(BUS_DMA_CONTINUOUS, &dazzler_busmaster);
#endif
		}

		for (bytepos=0; bytepos<num_bytes; bytepos++) {

			if (format & 0x40) {	/* x4 mode */
				/* render pixels */
				i = line_buffer[bytepos];
				if (hires_subrow == 0) {
					/* first 3 scanline subrow */
					if (i & 0x01)
						fill_rect(bytepos * 4 * psize, vpos, psize, pscale);
					if (i & 0x02)
						fill_rect((bytepos * 4 + 1) * psize, vpos, psize, pscale);
					if (i & 0x10)
						fill_rect((bytepos * 4 + 2) * psize, vpos, psize, pscale);
					if (i & 0x20)
						fill_rect((bytepos * 4 + 3) * psize, vpos, psize, pscale);
				} else {
					/* second 3 scanline subrow */
					if (i & 0x04)
						fill_rect(bytepos * 4 * psize, vpos, psize, pscale);
					if (i & 0x08)
						fill_rect((bytepos * 4 + 1) * psize, vpos, psize, pscale);
					if (i & 0x40)
						fill_rect((bytepos * 4 + 2) * psize, vpos, psize, pscale);
					if (i & 0x80)
						fill_rect((bytepos * 4 + 3) * psize, vpos, psize, pscale);
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
				fill_rect(bytepos * 2 * psize, vpos, psize, pscale);

				/* second pixel */
				i = (line_buffer[bytepos] & 0xf0) >> 4;
				if (format & 0x10) {
					set_fg_color(i);	/* color */
				}
				else {
					set_fg_gray(i);		/* grayscale */
				}
				fill_rect((bytepos * 2 + 1) * psize, vpos, psize, pscale);
			}
		}

		current_line += step;
			
		if (format & 0x40) {
			/* check which subrow we're in */
			hires_subrow = (current_line + start) / psize;
		}

		/* post processing after last line */
		if (current_line >= num_lines) {

			if (dazzler_line_sync) {
#ifdef PROFILER
				/* collect some profiler data */
				if (row_data.frame_index < 10) {
					/* capture current CPU clock */
					row_data.T2[row_data.frame_index][row_data.row_index] = T;
					/* ticks left until calculated end of DMA cycle */
					row_data.headroom[row_data.frame_index][row_data.row_index] = T_end_of_row - T;
				}
#endif
				/* wait until beam reaches end of row */
				int loop = 0;
				do {
					loop++;
					nanosleep(&min_sleep_time, NULL);
				}
				while ((T < (T_end_of_row - ticks_per_nanosleep)) && (cpu_state == ST_CONTIN_RUN));
#ifdef PROFILER
				/* collect some more profiler data */
				if (row_data.frame_index < 10) {
					row_data.loop[row_data.frame_index][row_data.row_index] = loop;
					row_data.T3[row_data.frame_index][row_data.row_index] = T;
					/* calculate offset between where we are now and where we should be
					  (positive means we still have time left, negative means we're
					  already over the time */
					row_data.rest[row_data.frame_index][row_data.row_index] = T_end_of_row - T;
					row_data.row_index++;
					if (row_data.row_index == num_dma) {
						row_data.row_index = 0;
						row_data.frame_index++;
					}
				}
#endif
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
			if (cont) break;
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
/* function for updating the display with SDL framework (called in a loop by main thread) */
static void update_display(bool tick)
{
	UNUSED(tick);
	
	int width, height;
	
	Tstates_t T_end;
	
	/* handling window resize event */
	if (window_resized) {
		window_resized = false;
		SDL_GetWindowSize(window, &width, &height);
		window_size = width > height ? height : width;		/* assert 1:1 aspect ratio */

		if (dazzler_discrete_scale) {
			/* discrete scaling */
			pscale = window_size / WSIZE;
			if (pscale < 1) pscale = 1;
			canvas_size = pscale * WSIZE;
			window_size = canvas_size;
			SDL_SetWindowSize(window, window_size, window_size);
#if SDL_RENDER_MODE == SURFACE
			surface = SDL_GetWindowSurface(window);
			return;
#else /* TEXTURE */
			SDL_DestroyTexture(canvas);
			canvas = SDL_CreateTexture(renderer,
                                   SDL_PIXELFORMAT_RGBA8888,
                                   SDL_TEXTUREACCESS_TARGET,
                                   canvas_size, canvas_size);
#endif /* SDL_RENDER_MODE */
		}
		else {
			/* smooth scaling */
#if SDL_RENDER_MODE == SURFACE
			/* no smooth scaling for surface */
			surface = SDL_GetWindowSurface(window);
			SDL_SetWindowSize(window, canvas_size, canvas_size);
			return;
#else /* TEXTURE */
			SDL_SetWindowSize(window, window_size, window_size);
		       	SDL_RenderSetScale(renderer, (double)window_size / WSIZE, (double)window_size / WSIZE);
#endif /* SDL_RENDER_MODE */
		}
	}

	/* if enabled, draw one frame dependent on graphics format */
	if (state) {
		if (dazzler_interlaced)
			field = (field == ODD) ? EVEN : ODD;
#if SDL_RENDER_MODE == SURFACE
		 /* if required (normally it isn't), lock surface (actually, during lock, no system calls should happen) */
		if (SDL_MUSTLOCK(surface)) SDL_LockSurface(surface);
		canvas = surface->pixels;
		memset(canvas, 0, canvas_size * canvas_size * 4);
#else /* TEXTURE */
		SDL_SetRenderTarget(renderer, canvas);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);
#endif /* SDL_RENDER_MODE */

	       	draw_field(field);

#if SDL_RENDER_MODE == SURFACE
		if (SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);
		SDL_UpdateWindowSurface(window);
#else /* TEXTURE */
		SDL_SetRenderTarget(renderer, NULL);
		SDL_RenderCopy(renderer, canvas, NULL, NULL);
		SDL_RenderPresent(renderer);
#endif /* SDL_RENDER_MODE */
		/* frame done, set frame flag for 4 ms vertical blank */
		flags = 0x3f;
		T_end = T + (f_value * 4000);
		while ((T < T_end) && (cpu_state == ST_CONTIN_RUN)) sleep_for_us(1);
		flags |= 0x40;
	} else {
#if SDL_RENDER_MODE == SURFACE
		memset(canvas, 0, canvas_size * canvas_size * 4);
		SDL_UpdateWindowSurface(window);
#else /* TEXTURE */
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);
		SDL_RenderPresent(renderer);
#endif /* SDL_RENDER_MODE */
	}
}

static client_funcs_t dazzler_funcs = {
	open_display,
	close_display,
	process_event,
	update_display
};
#endif

#ifndef WANT_SDL
static void process_event(int *size)
{
	XEvent event;
	Atom actual_type, prop;
	int actual_format, status;
	unsigned long nitems, bytes_after;
	unsigned char *dp;

	while (XCheckWindowEvent(display, window, StructureNotifyMask | PropertyChangeMask, &event)) {
		switch(event.type) {
		case ConfigureNotify:
			/* check for window resize event */
			XConfigureEvent xce = event.xconfigure;
			if ((xce.width != *size) || (xce.height != *size)){
				window_resized = true;
				*size = xce.width < xce.height ? xce.width : xce.height;
			}
			break;
		case PropertyNotify:
			/* grant WSLg some time to process */
			sleep_for_ms(1);
			if (!strcmp(XGetAtomName(display, event.xproperty.atom), "_NET_WM_STATE")) {
	                    status = XGetWindowProperty(display, window, event.xproperty.atom, 0L, 1L, 0, 4,
	                    				&actual_type, &actual_format, &nitems, &bytes_after, &dp);
	                    if ((status == Success) && (actual_type == 4) && dp && (actual_format == 32) && nitems) {
	                        for (unsigned int i = 0; i < nitems; i++) {
	                            prop = (((Atom*)dp)[i]);
	                            if ((prop == wm_focused) || (prop == wm_maxhorz) || (prop == wm_maxvert)) {
					window_resized = true;
					XGetWindowAttributes(display, window, &wa);
					*size = wa.width < wa.height ? wa.width : wa.height;
				    }
				}
			    }
		        }
		        break;
		default:;
		}
	}
}
#endif

#if !defined(WANT_SDL) || defined(HAS_NETSERVER)
/* thread for updating the X11 display or web server */
static void *update_thread(void *arg)
{
	Tstates_t T_end;

	UNUSED(arg);
	
	while (true) {	/* do forever or until cancelled */

		/* draw one frame dependent on graphics format */
		if (state) {		/* draw frame if on */
#ifdef HAS_NETSERVER
			if (!n_flag) {
#endif /* HAS_NETSERVER */
#ifndef WANT_SDL
				T_end = T + (8250 * f_value);
				process_event(&window_size);
				if (window_resized) {
					if (dazzler_discrete_scale) {
						pscale = window_size / WSIZE;
						if (pscale < 1) pscale = 1;
						if (canvas_size != (pscale * WSIZE)) {
							window_size = pscale * WSIZE;
							canvas_size = pscale * WSIZE;
							free(canvas);
							canvas = malloc(canvas_size * canvas_size * 4);
							XDestroyImage(ximage);
							ximage = XCreateImage(display, vinfo.visual, 24, ZPixmap, 0, (char *)canvas,
                                  				canvas_size, canvas_size, 32, 0);
							XFreePixmap(display, pixmap);
							pixmap = XCreatePixmap(display, rootwindow, canvas_size, canvas_size, wa.depth);
							XResizeWindow(display, window, window_size, window_size);
						}
					}
					else if (has_xrender_extension) {
						XResizeWindow(display, window, window_size, window_size);
						double scale_factor = (double)canvas_size / (double)window_size;					
						transform.matrix[0][0] = XDoubleToFixed(scale_factor);
						transform.matrix[0][1] = XDoubleToFixed(0);
						transform.matrix[0][2] = XDoubleToFixed(0);
						transform.matrix[1][0] = XDoubleToFixed(0);
						transform.matrix[1][1] = XDoubleToFixed(scale_factor);
						transform.matrix[1][2] = XDoubleToFixed(0);
						transform.matrix[2][0] = XDoubleToFixed(0);
						transform.matrix[2][1] = XDoubleToFixed(0);
						transform.matrix[2][2] = XDoubleToFixed(1);			
					} else {
						/* prohibit resize */
						window_size = canvas_size;
						XResizeWindow(display, window, window_size, window_size);
					}
					window_resized = false;
				}

				set_fg_color(0);
				fill_rect(0, 0, canvas_size, canvas_size);
				if (dazzler_interlaced)
					field = (field == ODD) ? EVEN : ODD;

	        		draw_field(field);

				XLockDisplay(display);
				if (has_xrender_extension && !dazzler_discrete_scale) {
					XPutImage(display, pixmap, gc, ximage, 0, 0, 0, 0, canvas_size, canvas_size);
					canvas_pic = XRenderCreatePicture(display, pixmap, pict_format, 0, NULL);
					XRenderSetPictureTransform(display, canvas_pic, &transform);
				        XRenderComposite(display, PictOpSrc, canvas_pic, 0, window_pic,
				                         0, 0, 0, 0, 0, 0, window_size, window_size);
				}
				else {
					XPutImage(display, window, gc, ximage, 0, 0, 0, 0, canvas_size, canvas_size);
				}
				XSync(display, True);
				XUnlockDisplay(display);

				/* in case we are done earlier with the frame, let's do a short nap */
				while ((T < T_end) && (cpu_state == ST_CONTIN_RUN)) nanosleep(&min_sleep_time, NULL);			
#endif /* !WANT_SDL */
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
#endif /* HAS_NETSERVER */
		}
		else {
#ifdef HAS_NETSERVER
			if (!n_flag) {
#endif /* HAS_NETSERVER */
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
#endif /* HAS_NETSERVER */
			sleep_for_us(12129);
		}

		/* frame done, set frame flag for 4 ms vertical blank */
		flags = 0x3f;
		T_end = T + (f_value * 4000);
		while ((T < T_end) && (cpu_state == ST_CONTIN_RUN)) nanosleep(&min_sleep_time, NULL);
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
			if (dazzler_client_id < 0)
				dazzler_client_id = simsdl_create(&dazzler_funcs);
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
		frames = 0;
		start_time = get_clock_us();

		if (dazzler_line_sync) {
			row_data.frame_index = 0;
		}

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
#ifdef PROFILER
			double delta = (get_clock_us() - start_time) / 1000000.0;
			printf("Dazzler: delta=%fs, frames/s=%f\r\n", delta, frames / delta);
			/* ouput debug data */
			if (dazzler_line_sync) {
				int frame, row;
				for (frame=0; frame<row_data.frame_index; frame++) {
					for (row=0; row<(format & 0x20 ? 64 : 32); row++) {
						printf("frame=%d row=%d cycle=%d loops=%d processing=%lu headroom=%d nap=%lu rest=%d flags=%02X formatb=%02X\r\n",
						frame, row, row_data.cycle[frame], row_data.loop[frame][row],
						row_data.T2[frame][row] - row_data.T1[frame][row],
						row_data.headroom[frame][row],
						row_data.T3[frame][row] - row_data.T2[frame][row],
						row_data.rest[frame][row],
						row_data.flags[frame][row],
						row_data.format[frame][row]);
					}
				}
			}
#endif

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
		if (dazzler_client_id >= 0)
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
