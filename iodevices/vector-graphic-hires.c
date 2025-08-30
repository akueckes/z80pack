/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Common I/O devices used by various simulated machines
 *
 * Copyright (C) 2015-2019 by Udo Munk
 * Copyright (C) 2018 David McNaughton
 * Copyright (C) 2024 Ansgar Kueckes
 *
 * Emulation of a Vector Graphic High Resoution Graphics board
 *
 * History:
 * 11-OCT-2024 first version
 */
 
#include <stdint.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>

#include "sim.h"

#ifdef HAS_VECTOR_GRAPHIC_HIRES

#include "simdefs.h"
#include "simglb.h"
#include "simcfg.h"
#include "simmem.h"
#include "simport.h"
#ifdef WANT_SDL
#include "simsdl.h"
#endif

#ifdef HAS_NETSERVER
#include "netsrv.h"
#endif
#include "vector-graphic-hires.h"

/* #define LOG_LOCAL_LEVEL LOG_DEBUG */
#include "log.h"
static const char *TAG = "HIRES";

enum VideoMode {
	BILEVEL = 0,
	HALFTONE = 1
};

#define DEFAULT_HIRES_MODE	BILEVEL		/* default video mode BILEVEL/HALFTONE */
#define DEFAULT_HIRES_ADDRESS	0xe000		/* default video memory address */
char default_hires_foreground[] = "00ff00";	/* default foreground color */

int vector_graphic_hires_mode = DEFAULT_HIRES_MODE;
int vector_graphic_hires_address = DEFAULT_HIRES_ADDRESS;
uint8_t vector_graphic_hires_fg_color[3] = {0, 255, 0};

static int window_width = 512;
static int window_height = 480;
static int canvas_width = 512;
static int canvas_height = 480;
static bool window_resized = false;

#ifdef WANT_SDL
static int hires_win_id = -1;
static SDL_Window *window;
static SDL_Renderer *renderer;
static uint8_t colors[2][3] = {
	{ 0x00, 0x00, 0x00 },
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
#else
/* X11 stuff */
static int has_xrender_extension = 0;
static XRenderPictFormat *pict_format;
static Picture canvas_pic;
static Picture window_pic;
static double scale_factor;
static Display *display;
static Window window;
static int screen;
static GC gc;
static XWindowAttributes wa;
static Atom wm_focused, wm_maxhorz, wm_maxvert, wm_hidden;	
static Pixmap pixmap;
static Colormap colormap;
static XColor colors[2];
static XColor grays[16];
static char background[] =  "#000000";
#endif

static int state;

/* UNIX stuff */
static pthread_t thread;

#ifdef HAS_NETSERVER
static void ws_clear(void);
#endif

/* create the window for HiRes display */
static void open_display(void)
{
#ifdef WANT_SDL
	/* foreground color */ 
	int i;
	float r,g,b;
	r = vector_graphic_hires_fg_color[0] / 255.0;
	g = vector_graphic_hires_fg_color[1] / 255.0;
	b = vector_graphic_hires_fg_color[2] / 255.0;
	for (i=0; i<16; i++) {
		grays[i][0] = (uint8_t) i*0x11*r;
		grays[i][1] = (uint8_t) i*0x11*g;
		grays[i][2] = (uint8_t) i*0x11*b;
	}
	colors[1][0] = r * 255; 
	colors[1][1] = g * 255; 
	colors[1][2] = b * 255; 
	
	/* create window */
	window = SDL_CreateWindow("Vector Graphic HiRes",
				  SDL_WINDOWPOS_UNDEFINED,
				  SDL_WINDOWPOS_UNDEFINED,
				  window_width, window_height, 0);
	renderer = SDL_CreateRenderer(window, -1, (SDL_RENDERER_ACCELERATED |
						   SDL_RENDERER_PRESENTVSYNC));
#else /* !WANT_SDL */
	Window rootwindow;
	XSizeHints *size_hints = XAllocSizeHints();
	Atom wm_delete_window;
    	int first_event, first_error;
	char rgb_str[8];
	int i, r, g, b;

	display = XOpenDisplay(NULL);
	if (!display) {
		printf("Could not open display, please ensure XServer is running and DISPLAY is set\n\r");
		exit(-1);
	}
	XLockDisplay(display);
	screen = DefaultScreen(display);
	rootwindow = RootWindow(display, screen);
	XGetWindowAttributes(display, rootwindow, &wa);
	window = XCreateSimpleWindow(display, rootwindow, 0, 0,
				     window_width, window_height, 1, 0, 0);
	XStoreName(display, window, "Vector Graphic HiRes");

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
	pixmap = XCreatePixmap(display, rootwindow, window_width, window_height, wa.depth);

	/* bilevel colors */
	XParseColor(display, colormap, background, &colors[0]);
	XAllocColor(display, colormap, &colors[0]);
	sprintf(rgb_str, "#%02X%02X%02X",
		vector_graphic_hires_fg_color[0],
		vector_graphic_hires_fg_color[1],
		vector_graphic_hires_fg_color[2]);
	XParseColor(display, colormap, rgb_str, &colors[1]);
	XAllocColor(display, colormap, &colors[1]);

	/* halftone shades */
	for (i=0; i<16; i++) {
		r = (vector_graphic_hires_fg_color[0] * i) / 16;
		g = (vector_graphic_hires_fg_color[1] * i) / 16;
		b = (vector_graphic_hires_fg_color[2] * i) / 16;
		sprintf(rgb_str, "#%02X%02X%02X", r, g, b);
		XParseColor(display, colormap, rgb_str, &grays[i]);
		XAllocColor(display, colormap, &grays[i]);
	}

	/* XRenderExtension stuff */
    	if (XRenderQueryExtension(display, &first_event, &first_error)) {
		XTransform transform;
		has_xrender_extension = 1;
		pict_format = XRenderFindVisualFormat(display, DefaultVisual(display, screen));
		canvas_pic = XRenderCreatePicture(display, pixmap, pict_format, 0, NULL);
		window_pic = XRenderCreatePicture(display, window, pict_format, 0, NULL);
		scale_factor = 1.0;					
		transform.matrix[0][0] = XDoubleToFixed(scale_factor);
		transform.matrix[0][1] = XDoubleToFixed(0);
		transform.matrix[0][2] = XDoubleToFixed(0);
		transform.matrix[1][0] = XDoubleToFixed(0);
		transform.matrix[1][1] = XDoubleToFixed(scale_factor);
		transform.matrix[1][2] = XDoubleToFixed(0);
		transform.matrix[2][0] = XDoubleToFixed(0);
		transform.matrix[2][1] = XDoubleToFixed(0);
		transform.matrix[2][2] = XDoubleToFixed(1);					
		XRenderSetPictureTransform(display, canvas_pic, &transform);
	}

	/* size hints */
	size_hints->flags = PBaseSize | PMinSize | PMaxSize | PAspect;
	size_hints->base_width = window_width;
	size_hints->base_height = window_height;
	size_hints->min_width = window_width;
	size_hints->min_height = window_height;
	size_hints->max_width = window_width;
	size_hints->max_height = window_height;
	size_hints->min_aspect.x = 16;
	size_hints->min_aspect.y = 15;
	size_hints->max_aspect.x = 16;
	size_hints->max_aspect.y = 15;
	
	if (has_xrender_extension) size_hints->flags = PBaseSize | PMinSize | PAspect;

	XSetWMNormalHints(display, window, size_hints);
	XFree(size_hints);

	XMapWindow(display, window);
	XUnlockDisplay(display);
#endif /* !WANT_SDL */
}

/* close the SDL or X11 window for HiRes display */
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
	Draw scanlines for a full frame

	Timing:
	
	  14.318 MHz system clock
	  7.875 MHz pixel clock
	  15.75 KHz line frequency
	  60 Hz vertical frequency (non interlaced)
	  2 scanlines/pixel (medium resolution halftone mode, 128x120)
	  1 scanline/pixel (high resolution bilevel mode, 256x240)
	  240 scanlines per frame
	
	The Vector Graphic HiRes board uses its own 8K RAM, of which only
	7.5K are used for video memory, the remaining 512 bytes can be
	used for other purposes.
	
*/
static void draw_frame()
{
	int bytepos, scanline, psize;
	WORD addr = vector_graphic_hires_address;
	BYTE i, data;
	int subrow, current_line, num_lines;
	
	current_line = 0;

	/* select foreground color for bilevel mode */
	if (vector_graphic_hires_mode == BILEVEL) {
		psize = 2;
		num_lines = 2;
		set_fg_color(1);
	}
	else {
		psize = 4;
		num_lines = 1;
		set_fg_gray(15);
	}

	/* now draw the frame */
	for (scanline=0; scanline<240; scanline++) {
		
		/* read data from video memory & write into X pixmap */
		for (bytepos=0; bytepos<64; bytepos++) {

			data = dma_read(addr + bytepos);
			subrow = scanline % 2;
			if (vector_graphic_hires_mode == BILEVEL) {
				/* render pixels */
				i = data;
				if (subrow == 0) {
					/* first subrow */
					if (i & 0x80)
						fill_rect(bytepos * 4 * psize, scanline * 2, psize, 2);
					if (i & 0x40)
						fill_rect((bytepos * 4 + 1) * psize, scanline * 2, psize, 2);
					if (i & 0x08)
						fill_rect((bytepos * 4 + 2) * psize, scanline * 2, psize, 2);
					if (i & 0x04)
						fill_rect((bytepos * 4 + 3) * psize, scanline * 2, psize, 2);
				} else {
					/* second subrow */
					if (i & 0x20)
						fill_rect(bytepos * 4 * psize, scanline * 2, psize, 2);
					if (i & 0x10)
						fill_rect((bytepos * 4 + 1) * psize, scanline * 2, psize, 2);
					if (i & 0x02)
						fill_rect((bytepos * 4 + 2) * psize, scanline * 2, psize, 2);
					if (i & 0x01)
						fill_rect((bytepos * 4 + 3) * psize, scanline * 2, psize, 2);
				}
			}
			else {	/* nibble mode */
				/* first pixel */
				i = (data & 0xf0) >> 4;

				/* halftone */
				set_fg_gray(i);
				fill_rect(bytepos * 2 * psize, scanline * psize, psize * 2, psize);			

				/* second pixel */
				i = data & 0x0f;

				/* halftone */
				set_fg_gray(i);
				fill_rect((bytepos * 2 + 1) * psize, scanline * psize, psize * 2, psize);				
			}
		}

		current_line++;
		if (current_line == num_lines) {
			addr += 64;
			current_line = 0;		
		}
	}
}

#ifdef HAS_NETSERVER
static uint8_t dblbuf[8192];

static struct {
	uint16_t addr;
	uint16_t len;
	uint8_t buf[8192];
} msg;

static void ws_clear(void)
{
	memset(dblbuf, 0, 8192);

	msg.addr = 0xFFFF;
	msg.len = 0;
	net_device_send(DEV_HIRES, (char *) &msg, msg.len + 6);
	LOGD(TAG, "Clear the screen.");
}

static void ws_refresh(void)
{
	int len = 8192;
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
			val = dma_read(vector_graphic_hires_address + i);
			while ((val != dblbuf[i]) && (i < len)) {
				dblbuf[i++] = val;
				msg.buf[n++] = val;
				cont = false;
				val = dma_read(vector_graphic_hires_address + i);
			}
			if (cont)
				break;
			x = 0;
#define LOOKAHEAD 6
			/* look-ahead up to n bytes for next change */
			while ((x < LOOKAHEAD) && !cont && (i < len)) {
				val = dma_read(vector_graphic_hires_address + i++);
				msg.buf[n++] = val;
				la_count++;
				val = dma_read(vector_graphic_hires_address + i);
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
		if (n) {
			msg.addr = addr;
			msg.len = n;
			net_device_send(DEV_HIRES, (char *) &msg, msg.len + 6);
#if 0
			LOGD(TAG, "BUF update 0x%04X-0x%04X "
			     "len: %d l/a: %d",
			     msg.addr, msg.addr + msg.len,
			     msg.len, la_count);
#endif
		}
	}
}
#endif /* HAS_NETSERVER */

#ifdef WANT_SDL
/* function for updating the display */
static void update_display(bool tick)
{
	uint64_t t,tleft;

	UNUSED(tick);

	t = get_clock_us();

	/* handling window resize event */
	if (window_resized) {
		window_resized = false;
		
		/* smooth scaling */
		SDL_GetWindowSize(window, &window_width, &window_height);
		window_width = (window_height * 16) / 15;
		SDL_SetWindowSize(window, window_width, window_height);
	       	SDL_RenderSetScale(renderer, (double)window_width / canvas_width, (double)window_height / canvas_height);
	}

	/* draw one frame dependent on graphics format */
	set_fg_color(0);
	SDL_RenderClear(renderer);
	if (state) {		/* draw frame if on */
		draw_frame();
		SDL_RenderPresent(renderer);

		/* sleep rest to 16666 us so that we get 60 fps */
		tleft = 16666L - (long) (get_clock_us() - t);
		if (tleft > 0)
			sleep_for_us(tleft);

		t = get_clock_us();
	} else
		SDL_RenderPresent(renderer);
}

static win_funcs_t hires_funcs = {
	open_display,
	close_display,
	process_event,
	update_display
};
#endif /* WANT SDL */

#ifndef WANT_SDL
/* process X11 events */
static void process_event()
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
			if ((xce.width != window_width) || (xce.height != window_height)){
				window_resized = true;
			}
			break;
		case PropertyNotify:
			/* check for window maximize/minimize/normalize property change */
			sleep_for_ms(1);
			if (!strcmp(XGetAtomName(display, event.xproperty.atom), "_NET_WM_STATE")) {
	                    status = XGetWindowProperty(display, window, event.xproperty.atom, 0L, 1L, 0, 4,
	                    				&actual_type, &actual_format, &nitems, &bytes_after, &dp);
	                    if ((status == Success) && (actual_type == 4) && dp && (actual_format == 32) && nitems) {
	                        for (unsigned int i = 0; i < nitems; i++) {
	                            prop = (((Atom*)dp)[i]);
	                            if ((prop == wm_focused) || (prop == wm_maxhorz) || (prop == wm_maxvert)) {
					window_resized = true;
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
	uint64_t t,tleft;

	UNUSED(arg);

	t = get_clock_us();

	while (true) {	/* do forever or until canceled */

		/* draw one frame dependent on graphics format */
		if (state) {		/* draw frame if on */
#ifdef HAS_NETSERVER
			if (!n_flag) {
#endif
#ifndef WANT_SDL
				process_event();
				if (window_resized) {
					XGetWindowAttributes(display, window, &wa);
					window_width = wa.width;
					window_height = wa.height;
					/* make sure we have a the correct aspect ratio even if the wm ignores the size hints */
					window_width = (window_height * 16) / 15;
					if (has_xrender_extension) {
						XResizeWindow(display, window, window_width, window_height);
						XTransform transform;
						double scale_factor_x = (double)canvas_width / (double)window_width;					
						double scale_factor_y = (double)canvas_height / (double)window_height;					
						transform.matrix[0][0] = XDoubleToFixed(scale_factor_x);
						transform.matrix[0][1] = XDoubleToFixed(0);
						transform.matrix[0][2] = XDoubleToFixed(0);
						transform.matrix[1][0] = XDoubleToFixed(0);
						transform.matrix[1][1] = XDoubleToFixed(scale_factor_y);
						transform.matrix[1][2] = XDoubleToFixed(0);
						transform.matrix[2][0] = XDoubleToFixed(0);
						transform.matrix[2][1] = XDoubleToFixed(0);
						transform.matrix[2][2] = XDoubleToFixed(1);					
						XRenderSetPictureTransform(display, canvas_pic, &transform);
					} else {
						/* prohibit resize */
						window_width = canvas_width;
						window_height = canvas_height;
						XResizeWindow(display, window, window_width, window_height);
					}
					window_resized = false;
				}
				XLockDisplay(display);
				set_fg_color(0);
				fill_rect(0, 0, window_width, window_height);
	        		draw_frame();
				if (has_xrender_extension) {
				        XRenderComposite(display, PictOpSrc, canvas_pic, 0, window_pic,
				                         0, 0, 0, 0, 0, 0, window_width, window_height);
				}
				else {
					XCopyArea(display, pixmap, window, gc, 0, 0, window_width, window_height, 0, 0);
				}
				XSync(display, True);
				XUnlockDisplay(display);
#endif
#ifdef HAS_NETSERVER
			} else {
				if (net_device_alive(DEV_HIRES)) {
					ws_refresh();
				}
			}
#endif
		}

		/* sleep rest to 16666 us so that we get 60 fps */
		tleft = 16666L - (long) (get_clock_us() - t);
		if (tleft > 0)
			sleep_for_us(tleft);

		t = get_clock_us();
	}

	/* just in case it ever gets here */
	pthread_exit(NULL);
}
#endif /* !WANT_SDL || !HAS_NETSERVER */

void vector_graphic_hires_init()
{
#ifdef HAS_NETSERVER
		if (!n_flag) {
#endif
#ifdef WANT_SDL
			if (hires_win_id < 0)
				hires_win_id = simsdl_create(&hires_funcs);
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
}

#if !defined(WANT_SDL) || defined(HAS_NETSERVER)
void kill_thread(void)
{
	if (thread != 0) {
		sleep_for_ms(50);
		pthread_cancel(thread);
		pthread_join(thread, NULL);
		thread = 0;
	}
}
#endif

/* switch HiRes off from front panel */
void vector_graphic_hires_off(void)
{
	state = false;

#ifdef WANT_SDL
#ifdef HAS_NETSERVER
	if (!n_flag) {
#endif
		if (hires_win_id >= 0) {
			simsdl_destroy(hires_win_id);
			hires_win_id = -1;
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

#endif /* HAS_VECTOR_GRAPHIC_HIRES */
