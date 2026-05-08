/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Common I/O devices used by various simulated machines
 *
 * Copyright (C) 2015-2019 by Udo Munk
 * Copyright (C) 2018 David McNaughton
 * Copyright (C) 2025 by Thomas Eberhardt
 * Copyright (C) 2026 Ansgar Kueckes
 *
 * Emulation of a S100 frame buffer video device
 *
 * History:
 * 03-MAY-2026 first version
*/

/*
	This module provides everything for rendering frame buffer video on
	the local display with different rendering options, as well as
	transfer of device data to a browser based web client via netserver.
	
	Integration
	-----------
	
	Everything specific to the emulated device should be implemented in
	a dedicated device module.
	
	Emulation of the device's state machine and the rendering on the
	local system are decoupled, so that the state machine can run in-sync
	with the CPU, while the actual rendering is performed on a best
	effort base. Accurate synchronization utilizes CPU timers triggering
	callback functions.

	Rendering options
	-----------------

	The original rendering implementation used API calls for every write
	to the video buffer. It has been replaced by a choice of more
	efficient drawing methods which happen in the client itself and
	transferring the image as a whole with one single API call to
	the X server.

	For Dazzler emulation, in total three different client-based rendering
	methods are supported, two rendering methods (Surface and Texture) are
	supported with SDL2, plus XImage based rendering for vanilla X11.

	Surface rendering is the fastest drawing method for bitblit operations
	with SDL2 as used with the Dazzler emulation, whereas rendering based
	on Textures is in general more flexible. It can utilize the full range
	of rendering features supported by SDL2 (e.g. free scaling or hardware
	acceleration), but with overall slightly lower performance for bitblit
	operations.
	
	The fastest rendering with lowest overhead, however, can be achieved
	by directly writing to XImage bitmaps. Free scaling won't be supported
	by X11, but can be done	without performance tradeoffs by using the
	X Rendering Extension (if supported by the X Server).
	
	If neither Texture based rendering is used, nor the X Rendering
	Extension is available,	scaling can be performed within the client,
	passing over the scaled image to the X Server.
	
	Webserver
	---------
	
	The z80pack emulation can be run in webserver mode, where no rendering
	is done on the local display, but rather a web interface is provided
	with a desktop holding a complete S100 computer environment, including
	the Dazzler display. The desktop is accessable from a web browser via
	localhost:8080.
	
	This implementation will serve the z80pack web interface, but relevant
	portions of the Dazzler rendering are hidden inside a Javascript engine,
	so no synchronization is possible with the renderer. Consequently,
	certain features like tweaking or interlaced display are not available
	with the web desktop. Generating the information in the flags register
	will still be accurate, so that some Dazzler programs using this
	information will work, which will not run otherwise.

	TODOs
	-----

	Because of the many supported options, and the corresponding
	conditional compliles, the code becomes a bit hard to read. I'll
	try to reduce it to the options which work out best or try to make
	an automatic selection in the future.
	
	In general, it shows that SDL is usable, but for 2D rendering, a
	generic X11 implementation can be way more performant, even if
	intrinsic GPU support for scaling is substituted by a software scaler.

	The main advantage of SDL is compatibility across non-X11 platforms
	such as MS Windows, which in fact is not an option with z80pack anyway.
*/

/* SDL rendering options */
#define SURFACE	(0)			/* fastest rendering option */
#define TEXTURE (1)			/* still fast, and can scale freely */
#define SDL_RENDER_MODE SURFACE		/* define to either SURFACE or TEXTURE */
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

#ifdef HAS_NETSERVER
#include "netsrv.h"
#endif

#if !defined(WANT_SDL) || defined(HAS_NETSERVER)
#include <pthread.h>
#endif

/* #define LOG_LOCAL_LEVEL LOG_DEBUG */
#include "log.h"
static const char *TAG = "FB_VIDEO";

#include "fb_video.h"

/* frame buffer video properties */
typedef struct _fbvideo {
	/* video on/off state */
	bool state;

	bool vblank;		/* vertical blank active */
	int field;		/* even/odd field indicator when interlaced enabled */
	int vscan_period;
	int vblank_period;
	int timer_id;
	Tstates_t T_end_vscan;
	Tstates_t T_end_vblank;

	/* system.conf properties */
	bool stats;
	bool frame_sync;
	bool interlaced;

	/* generic window stuff */
	int canvas_height;
	int canvas_width;
	int window_height;
	int window_width;
	bool window_resized;
	const char *title;

	/* colormaps */
	uint8_t (*colors)[3];
	uint8_t (*gradients)[3];

	/* stats data */
	int frames;
	uint64_t start_time;

	/* callbacks */
	void (*draw_field)(void);
	void (*callback)(void *user_data);
	void *user_data;
#ifdef HAS_NETSERVER
	void (*ws_update)(bool do_clear);
#endif /* HAS_NETSERVER */

	/* SDL specific */
#ifdef WANT_SDL
	int client_id;
	SDL_Window *window;
#if SDL_RENDER_MODE == SURFACE
	SDL_Surface *surface;
	Uint32 fg;
	Uint32 *canvas;
#elif SDL_RENDER_MODE == TEXTURE
	SDL_Renderer *renderer;
	SDL_Texture *texture;
	Uint32 *canvas;
	Uint32 fg;
	int pitch;
#endif /* RENDER_MODE */

#else /* !WANT_SDL */

	/* X11 specific */
	BYTE *canvas, *scaled_canvas;
	XRenderPictFormat *pict_format;
	XImage *ximage, *scaled_ximage;
	Picture canvas_pic, window_pic;
	XTransform transform;
	Window window;
	GC gc;
	XVisualInfo vinfo;
	Window rootwindow;
	XWindowAttributes wa;
	Atom wm_focused, wm_maxhorz, wm_maxvert, wm_hidden, wm_delete_window;
	Pixmap pixmap;
	uint8_t (*fg)[3];

#endif /* !WANT_SDL */

#if !defined(WANT_SDL) || defined(HAS_NETSERVER)
	pthread_t thread;
#endif
} FBVideo;

/* standard color palette */
static uint8_t standard_colors[16][3] = {
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

/* standard halftone palette */
static uint8_t standard_gradients[16][3] = {
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

#ifdef HAS_NETSERVER
static void ws_clear(int device_handle);
#endif

#define MAX_NUM_DEVICES	16

static FBVideo fb_video_devices[MAX_NUM_DEVICES];
static int num_devices = 0;
static uint32_t used_devices = 0;
static int open_handle;

#ifdef WANT_SDL
/*
 *	Create the SDL window
 *
 *	Set open_handle appropriately before calling this function
 */
static void open_window(int device_handle)
{
	if (device_handle < 0) return;

	FBVideo *dev = &fb_video_devices[device_handle];
	
	int width = dev->canvas_width;
	int height = dev->canvas_height;

	dev->field = dev->interlaced ? EVEN : FULL;
	dev->vblank = false;

#if SDL_RENDER_MODE == SURFACE
	dev->window = SDL_CreateWindow(dev->title,
				  SDL_WINDOWPOS_UNDEFINED,
				  SDL_WINDOWPOS_UNDEFINED,
				  width, height,
				  SDL_WINDOW_SHOWN);
	dev->surface = SDL_GetWindowSurface(dev->window);
	dev->canvas = dev->surface->pixels;
#elif SDL_RENDER_MODE == TEXTURE
	dev->window = SDL_CreateWindow(dev->title,
				  SDL_WINDOWPOS_UNDEFINED,
				  SDL_WINDOWPOS_UNDEFINED,
				  width, height,
				  SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
	dev->renderer = SDL_CreateRenderer(dev->window, -1,
				(SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));
	dev->texture = SDL_CreateTexture(dev->renderer,
                                   SDL_PIXELFORMAT_RGBA8888,
                                   SDL_TEXTUREACCESS_STREAMING,
                                   width, height);
	int pitch;
	SDL_LockTexture(dev->texture, NULL, (void **)&dev->canvas, &pitch);
	dev->pitch = pitch/4;
#endif /* SDL_RENDER_MODE */

#ifdef CPU_TIMER
	if (dev->timer_id < 0) {
		/* get CPU timer */
		dev->timer_id = register_cpu_timer(0, dev->callback, dev->user_data);
		if (dev->timer_id < 0) {
			LOGE(TAG, "Unable to allocate CPU timer (all slots in use)\n");
			return;
		}
	}

	dev->timer_id = dev->timer_id;
	
	/* start timer */
	set_cpu_timer(dev->timer_id, 1);
#endif /* CPU_TIMER */
}

/*
 *	Close the SDL window
 */
static void close_window(int device_handle)
{
	if (device_handle < 0) return;

	FBVideo *dev = &fb_video_devices[device_handle];

#if SDL_RENDER_MODE == TEXTURE
	SDL_DestroyRenderer(dev->renderer);
	dev->renderer = NULL;
	SDL_DestroyTexture(dev->texture);
	dev->texture = NULL;
#endif
	SDL_DestroyWindow(dev->window);
	dev->window = NULL;
}

#else /* !WANT_SDL */

static Display *display = NULL;			/* used by all X11 fb_video windows */
static bool has_xrender_extension = false;	/* set after display setup */

/*
 *	Create the X11 window
 */
static void open_window(int device_handle)
{
	if (device_handle < 0) return;

	FBVideo *dev = &fb_video_devices[device_handle];
	
	int width = dev->canvas_width;
	int height = dev->canvas_height;

	dev->field = dev->interlaced ? EVEN : FULL;
	dev->vblank = false;

	XSizeHints *size_hints = XAllocSizeHints();
    	int first_event, first_error, screen;
	
	if (display == NULL) {
		display = XOpenDisplay(NULL);
		if (display == NULL) {
			LOGE(TAG, "Could not open display, please ensure X Server is running and DISPLAY is set\n\r");
			return;
		}
		has_xrender_extension = XRenderQueryExtension(display, &first_event, &first_error);
	}

	XLockDisplay(display);
	screen = DefaultScreen(display);
	dev->rootwindow = RootWindow(display, screen);
	XGetWindowAttributes(display, dev->rootwindow, &dev->wa);
	dev->window = XCreateSimpleWindow(display, dev->rootwindow, 0, 0,
				     width, height, 1, 0, 0);
	XStoreName(display, dev->window, dev->title);

	dev->wm_focused = XInternAtom(display, "_NET_WM_STATE_FOCUSED", 0);
    	dev->wm_maxhorz = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", 0);
    	dev->wm_maxvert = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", 0);
    	dev->wm_hidden = XInternAtom(display, "_NET_WM_STATE_HIDDEN", 0);		
	dev->wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(display, dev->window, &dev->wm_delete_window, 1);
	
	XSelectInput(display, dev->window, StructureNotifyMask | PropertyChangeMask);

	dev->gc = XCreateGC(display, dev->window, 0, NULL);
	XSetFillStyle(display, dev->gc, FillSolid);
	dev->pixmap = XCreatePixmap(display, dev->rootwindow, width, height, dev->wa.depth);
	
	/* Create an XImage structure that points to our buffer */
    	XMatchVisualInfo(display, screen, 24, TrueColor, &dev->vinfo);
    	dev->canvas = malloc(width * height * 4);
        dev->ximage = XCreateImage(display, dev->vinfo.visual, 24, ZPixmap, 0, (char *)dev->canvas,
                                  width, height, 32, 0);

#if 1
	/* XRenderExtension stuff */
    	if (has_xrender_extension) {
		dev->pict_format = XRenderFindVisualFormat(display, DefaultVisual(display, screen));
		dev->window_pic = XRenderCreatePicture(display, dev->window, dev->pict_format, 0, NULL);
		dev->transform.matrix[0][0] = XDoubleToFixed(1);
		dev->transform.matrix[0][1] = XDoubleToFixed(0);
		dev->transform.matrix[0][2] = XDoubleToFixed(0);
		dev->transform.matrix[1][0] = XDoubleToFixed(0);
		dev->transform.matrix[1][1] = XDoubleToFixed(1);
		dev->transform.matrix[1][2] = XDoubleToFixed(0);
		dev->transform.matrix[2][0] = XDoubleToFixed(0);
		dev->transform.matrix[2][1] = XDoubleToFixed(0);
		dev->transform.matrix[2][2] = XDoubleToFixed(1);					
	}
#endif

	/* size hints */
	size_hints->flags = PSize | PMinSize | PAspect;
	size_hints->min_width = width;
	size_hints->min_height = height;
	size_hints->base_width = width;
	size_hints->base_height = height;
	size_hints->min_aspect.x = 1;
	size_hints->min_aspect.y = 1;
	size_hints->max_aspect.x = 1;
	size_hints->max_aspect.y = 1;

	XSetWMNormalHints(display, dev->window, size_hints);
	XFree(size_hints);

	XMapWindow(display, dev->window);
	XUnlockDisplay(display);

#ifdef CPU_TIMER
	if (dev->timer_id < 0) {
		/* get CPU timer */
		dev->timer_id = register_cpu_timer(0, dev->callback, dev->user_data);
		if (dev->timer_id < 0) {
			LOGE(TAG, "Unable to allocate CPU timer (all slots in use)\n");
			return;
		}
	}

	/* start timer */
	set_cpu_timer(dev->timer_id, 1);
#endif /* CPU_TIMER */
}

/*
 *	Close the X11 window
 */
static void close_window(int device_handle)
{
	if ((device_handle < 0) || (display == NULL)) return;

	FBVideo *dev = &fb_video_devices[device_handle];

	XLockDisplay(display);
	XDestroyImage(dev->ximage);
	if (dev->scaled_ximage) {
		XDestroyImage(dev->scaled_ximage);
	}
	XFreePixmap(display, dev->pixmap);
	XFreeGC(display, dev->gc);
	XDestroyWindow(display, dev->window);
	XUnlockDisplay(display);

	if (num_devices == 1) {
		XCloseDisplay(display);
		display = NULL;
	}
}

#endif /* !WANT_SDL */

#if !defined(WANT_SDL) || defined(HAS_NETSERVER)
/*
 *	Terminate worker thread for X11 and netserver
 */
static void kill_thread(int device_handle)
{
	if (device_handle < 0) return;

	FBVideo *dev = &fb_video_devices[device_handle];

	if (dev->thread != 0) {
		sleep_for_ms(50);
		pthread_cancel(dev->thread);
		pthread_join(dev->thread, NULL);
		dev->thread = 0;
	}
}
#endif

/*
 *	Switch video on
 */
void fb_video_show(int device_handle)
{
	if (device_handle < 0) return;

	FBVideo *dev = &fb_video_devices[device_handle];
	
	dev->state = true;

#ifdef HAS_NETSERVER
	if (n_flag)
		ws_clear(device_handle);
	else
#endif

#ifdef WANT_SDL
		SDL_ShowWindow(dev->window);
#else
		XMapWindow(display, dev->window);
#endif /* !WANT_SDL */

#ifdef CPU_TIMER
	set_cpu_timer(dev->timer_id, 1);
#endif /* CPU_TIMER */
}

/*
 *	Switch video off
 */
void fb_video_hide(int device_handle)
{
	if (device_handle < 0) return;

	FBVideo *dev = &fb_video_devices[device_handle];

	dev->state = false;

#ifdef CPU_TIMER
	set_cpu_timer(dev->timer_id, 0);
#endif /* CPU_TIMER */

#ifdef HAS_NETSERVER
	if (n_flag)
		ws_clear(device_handle);
	else
#endif

#ifdef WANT_SDL
		SDL_HideWindow(dev->window);
#else
		XUnmapWindow(display, dev->window);
#endif /* !WANT_SDL */

	if (dev->stats) {
		double delta = (get_clock_us() - dev->start_time) / 1000000.0;
		printf("\r\nFBVideo: delta=%f seconds frames/s=%f\r\n", delta, dev->frames / delta);
	}
}


/*
 *	Terminate frame buffer video device
 */
void fb_video_off(int device_handle)
{
	if (device_handle < 0) return;

	if (device_handle > MAX_NUM_DEVICES) {
    		LOGE(TAG, "Device handle out of range\n");
    		return;
	}
    
	if ((used_devices & (1 << device_handle)) == 0) {
    		LOGE(TAG, "Unused device handle\n");
    		return;
	}

	FBVideo *dev = &fb_video_devices[device_handle];

	dev->state = false;

#ifdef CPU_TIMER
	/* stop CPU timer */
	set_cpu_timer(dev->timer_id, 0);
	
	/* unregister CPU timer callback */
	unregister_cpu_timer(dev->timer_id);
#endif /* CPU_TIMER */

#ifdef WANT_SDL
#ifdef HAS_NETSERVER
	if (!n_flag) {
#endif
		if (dev->client_id >= 0) {
			simsdl_destroy(dev->client_id);
			dev->client_id = -1;
		}
#ifdef HAS_NETSERVER
	} else {
		kill_thread(device_handle);
		ws_clear(device_handle);
	}
#endif
#else /* !WANT_SDL */
	kill_thread(device_handle);
	close_window(device_handle);
#ifdef HAS_NETSERVER
	if (n_flag)
		ws_clear(device_handle);
#endif
#endif /* WANT_SDL */

    num_devices--;
    used_devices &= ~(1 << device_handle);
}

#ifdef WANT_SDL

/*
 *	Process SDL event
 */
static void process_event(int device_handle, SDL_Event *event)
{
	if (device_handle < 0) return;

	FBVideo *dev = &fb_video_devices[device_handle];

	switch(event->type) {
	case SDL_WINDOWEVENT:
		switch(event->window.event) {
		case SDL_WINDOWEVENT_RESIZED:
		case SDL_WINDOWEVENT_SIZE_CHANGED:
		case SDL_WINDOWEVENT_MAXIMIZED:
		case SDL_WINDOWEVENT_RESTORED:
#if SDL_RENDER_MODE == SURFACE
			dev->surface = SDL_GetWindowSurface(dev->window);		/* try to avoid dangling pointer/out-of-bounds */
#endif /* SDL_REDNDER_MODE */
			dev->window_resized = true;
			break;
		case SDL_WINDOWEVENT_CLOSE:
			// do whatever should be done when window gets closed by user
			break;
		default: break;
		}
	default:;
	}
}

/*
 *	Set foreground color for SDL
 */
inline void set_fg_color(int device_handle, int i)
{
	if (device_handle < 0) return;
	
	FBVideo *dev = &fb_video_devices[device_handle];

#if SDL_RENDER_MODE == SURFACE
	dev->fg = SDL_MapRGB(dev->surface->format, *dev->colors[i], *(dev->colors[i]+1), *(dev->colors[i]+2));
#elif SDL_RENDER_MODE == TEXTURE
	dev->fg = *dev->colors[i] << 24 | *(dev->colors[i]+1) << 16 | *(dev->colors[i]+2) << 8 | 0xff;
#endif /* SDL_RENDER_MODE */
}

/*
 *	Set foreground gradient value for SDL
 */
inline void set_fg_gradient(int device_handle, int i)
{
	if (device_handle < 0) return;

	FBVideo *dev = &fb_video_devices[device_handle];
	
#if SDL_RENDER_MODE == SURFACE
	dev->fg = SDL_MapRGB(dev->surface->format, *dev->gradients[i], *(dev->gradients[i]+1), *(dev->gradients[i]+2));
#elif SDL_RENDER_MODE == TEXTURE
	dev->fg = *dev->gradients[i] << 24 | *(dev->gradients[i]+1) << 16 | *(dev->gradients[i]+2) << 8 | 0xff;
#endif /* SDL_RENDER_MODE */
}

/*
 *	Draw rectangle for SDL with current foreground
 */
inline void fill_rect(int device_handle, int x, int y, int w, int h)
{
	if (device_handle < 0) return;

	FBVideo *dev = &fb_video_devices[device_handle];

	/* try to catch surface re-allocation caused by concurrent window resize */
	if (dev->window_resized) return;

	register int i,j,x_max,y_max;

	x_max = x + w;
	y_max = y + h;
	if (x_max > dev->canvas_width) x_max = dev->canvas_width;
	if (y_max > dev->canvas_height) y_max = dev->canvas_height;

	for (j = y; j < y_max; j++) {
        	for (i = x; i < x_max; i++) {
#if SDL_RENDER_MODE == SURFACE
        		dev->canvas[(j * dev->surface->w) + i] = dev->fg;
#elif SDL_RENDER_MODE == TEXTURE
        		dev->canvas[(j * dev->pitch) + i] = dev->fg;
#endif /* RENDER_MODE*/
        	}
        }
}	

#else /* !WANT_SDL */

/*
 *	Process X11 event
 */
static void process_event(int device_handle, int *ret_width, int *ret_height)
{
	if (device_handle < 0) return;

	FBVideo *dev = &fb_video_devices[device_handle];

	XEvent event;
	Atom actual_type, prop;
	int actual_format, status;
	unsigned long nitems, bytes_after;
	unsigned char *dp;

	while(XCheckTypedWindowEvent(display, dev->window, ClientMessage, &event)) {
            if (((Atom*)event.xclient.data.l)[0] == dev->wm_delete_window) {
		// do whatever should be done when window gets closed by user
		break;
            }
        }

	while (XCheckWindowEvent(display, dev->window, StructureNotifyMask | PropertyChangeMask, &event)) {
		switch(event.type) {
		case ConfigureNotify:
			/* check for window resize event */
			XConfigureEvent xce = event.xconfigure;
			if ((xce.width != *ret_width) || (xce.height != *ret_height)){
				*ret_width = xce.width;
				*ret_height = xce.height;
				dev->window_resized = true;
			}
			break;
		case PropertyNotify:
			/* grant WSLg some time to process */
			sleep_for_ms(1);
			if (!strcmp(XGetAtomName(display, event.xproperty.atom), "_NET_WM_STATE")) {
	                    status = XGetWindowProperty(display, dev->window, event.xproperty.atom, 0L, 1L, 0, 4,
	                    				&actual_type, &actual_format, &nitems, &bytes_after, &dp);
	                    if ((status == Success) && (actual_type == 4) && dp && (actual_format == 32) && nitems) {
	                        for (unsigned int i = 0; i < nitems; i++) {
	                            prop = (((Atom*)dp)[i]);
	                            if ((prop == dev->wm_focused) || (prop == dev->wm_maxhorz) || (prop == dev->wm_maxvert)) {
					XGetWindowAttributes(display, dev->window, &dev->wa);
					*ret_width = dev->wa.width;
					*ret_height = dev->wa.height;
					dev->window_resized = true;
				    }
				}
			    }
		        }
		        break;
		default:;
		}
	}
}

/*
 *	Set foreground color for X11
 */
inline void set_fg_color(int device_handle, int i)
{
	if (device_handle < 0) return;

	FBVideo *dev = &fb_video_devices[device_handle];

	dev->fg = &dev->colors[i];
}

/*
 *	Set foreground gradient value for X11
 */
inline void set_fg_gradient(int device_handle, int i)
{
	if (device_handle < 0) return;

	FBVideo *dev = &fb_video_devices[device_handle];

	dev->fg = &dev->gradients[i];
}

/*
 *	Fill rectangle for X11 with foreground color
 */
inline void fill_rect(int device_handle, int x, int y, int w, int h)
{
	if (device_handle < 0) return;

	FBVideo *dev = &fb_video_devices[device_handle];

	register int i,j,x_max,y_max;
    
	x_max = x + w;
	y_max = y + h;
	if (x_max > dev->canvas_width) x_max = dev->canvas_width;
	if (y_max > dev->canvas_height) y_max = dev->canvas_height;
    
	for (j = y; j < y_max; j++) {
		for (i = x; i < x_max; i++) {
	            long offset = ((j * dev->canvas_width) + i) * 4;	/* RGBA */
	            dev->canvas[offset] = (*dev->fg)[2];
	            dev->canvas[offset + 1] = (*dev->fg)[1];
	            dev->canvas[offset + 2] = (*dev->fg)[0];
	            dev->canvas[offset + 3] = 255;			/* alpha */
		}
	}
}

/*
 *	Scale a source XImage to a destination XImage using bilinear interpolation
 */
static void scale_ximage(XImage *src, XImage *dst) {
    int sw = src->width;
    int sh = src->height;
    int dw = dst->width;
    int dh = dst->height;

    /* Use 16-bit precision for fractions */
    const int FP_SHIFT = 16;
    const uint32_t FP_MASK = (1 << FP_SHIFT) - 1;

    /* Calculate ratios (fixed-point 16.16) */
    /* We use (sw - 1) to ensure the (x+1) sampling stays within bounds */
    uint32_t x_ratio = (uint32_t)(((uint64_t)(sw - 1) << FP_SHIFT) / dw);
    uint32_t y_ratio = (uint32_t)(((uint64_t)(sh - 1) << FP_SHIFT) / dh);

    uint32_t *src_data = (uint32_t *)src->data;
    uint32_t *dst_data = (uint32_t *)dst->data;

    for (int i = 0; i < dh; i++) {
        uint32_t py = i * y_ratio;
        uint32_t y = py >> FP_SHIFT;
        uint32_t y_diff = py & FP_MASK;
        uint32_t y_inv = (1 << FP_SHIFT) - y_diff;

        uint32_t *row1 = &src_data[y * sw];
        uint32_t *row2 = &src_data[(y + 1) * sw];
        uint32_t *dst_row = &dst_data[i * dw];

        for (int j = 0; j < dw; j++) {
            uint32_t px = j * x_ratio;
            uint32_t x = px >> FP_SHIFT;
            uint32_t x_diff = px & FP_MASK;
            uint32_t x_inv = (1 << FP_SHIFT) - x_diff;

            /* Sample the 4 pixels */
            uint32_t p1 = row1[x];
            uint32_t p2 = row1[x + 1];
            uint32_t p3 = row2[x];
            uint32_t p4 = row2[x + 1];

            /* Use 64-bit math for the interpolation to avoid overflow 
               before the final 32-bit (FP_SHIFT * 2) right-shift. */
            
            #define INTERPOLATE(shift) ( \
                ((uint64_t)((p1 >> shift) & 0xFF) * x_inv + ((uint64_t)(p2 >> shift) & 0xFF) * x_diff) * y_inv + \
                ((uint64_t)((p3 >> shift) & 0xFF) * x_inv + ((uint64_t)(p4 >> shift) & 0xFF) * x_diff) * y_diff \
            ) >> (FP_SHIFT * 2)

            uint32_t r = INTERPOLATE(16);
            uint32_t g = INTERPOLATE(8);
            uint32_t b = INTERPOLATE(0);

            dst_row[j] = (r << 16) | (g << 8) | b;
        }
    }
}

#endif /* !WANT_SDL */

#ifdef HAS_NETSERVER
/*
 *	Clear netserver memory buffer
 */
static void ws_clear(int device_handle)
{
	if (device_handle < 0) return;

	FBVideo *dev = &fb_video_devices[device_handle];

	if (dev->ws_update) dev->ws_update(true);
}

/*
 *	Refresh netserver buffer
 */
static void ws_refresh(int device_handle)
{
	if (device_handle < 0) return;

	FBVideo *dev = &fb_video_devices[device_handle];

	if (dev->ws_update) dev->ws_update(false);
}
#endif /* HAS_NETSERVER */

#ifdef WANT_SDL
/*
 *	Update the display with SDL framework (called in a loop by main thread)
 *
 *	Note: Every call to this routine is done strictly in sequence, so multiple
 *            devices get served one after the other and should not block each other
 *	      (e.g. by using too long sleep functions).
 */
static void update_display(int device_handle, bool tick)
{
	UNUSED(tick);
	
	if (device_handle < 0) return;

	FBVideo *dev = &fb_video_devices[device_handle];

	/* handling window resize event */
	if (dev->window_resized) {
		SDL_GetWindowSize(dev->window, &dev->window_width, &dev->window_height);
#if SDL_RENDER_MODE == SURFACE
		/* no smooth scaling for surface */
		dev->surface = SDL_GetWindowSurface(dev->window);
		SDL_SetWindowSize(dev->window, dev->canvas_width, dev->canvas_height);
#elif SDL_RENDER_MODE == TEXTURE
		SDL_SetWindowSize(dev->window, dev->window_width, dev->window_height);
	       	SDL_RenderSetScale(dev->renderer, (double)dev->window_width / dev->canvas_width, (double)dev->window_height / dev->canvas_height);
#endif /* SDL_RENDER_MODE */
		dev->window_resized = false;
	}
	else {
		if (dev->callback && dev->frame_sync) {
			/* sync with vertical blank */
			if (dev->state && (!dev->vblank)) return;
		}
	}

	/* frame timing if not done in callback function */
	if (!dev->callback) {
		if (T < dev->T_end_vscan) return;
		if (T < dev->T_end_vblank) {
			dev->vblank = true;
			return;
		}
		dev->vblank = false;
		dev->T_end_vscan = T + (f_value * dev->vscan_period);
		dev->T_end_vblank = dev->T_end_vscan + (f_value * dev->vblank_period);
	}

	if (dev->state) {
		if (dev->interlaced)
			dev->field = (dev->field == ODD) ? EVEN : ODD;

		/* draw the frame/field */
	       	if (dev->draw_field) dev->draw_field();

#if SDL_RENDER_MODE == SURFACE
		SDL_UpdateWindowSurface(dev->window);
#elif SDL_RENDER_MODE == TEXTURE
		SDL_UnlockTexture(dev->texture);
		SDL_RenderCopy(dev->renderer, dev->texture, NULL, NULL);
		SDL_RenderPresent(dev->renderer);
		int pitch;
		SDL_LockTexture(dev->texture, NULL, (void **)&dev->canvas, &pitch);
		dev->pitch = pitch/4;
#endif /* SDL_RENDER_MODE */

		dev->frames++;
	}
}

static client_funcs2_t fb_video_funcs = {
	open_window,
	close_window,
	process_event,
	update_display
};

#endif /* WANT_SDL */

#if !defined(WANT_SDL) || defined(HAS_NETSERVER)

/*
 *	Thread for updating the X11 display or web server
 */
static void *update_thread(void *arg)
{
	int device_handle = *((int *)arg);

	if (device_handle < 0) pthread_exit(NULL);

	FBVideo *dev = &fb_video_devices[device_handle];
	
	/* can be cancelled all the time */
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	while (true) {	/* do forever or until cancelled */

#ifdef HAS_NETSERVER
		if (!n_flag) {
#endif /* HAS_NETSERVER */
#ifndef WANT_SDL
			/* handle window events */
			process_event(device_handle, &dev->window_width, &dev->window_height);
			
			/* handle resize event */
			if (dev->window_resized) {
				if (has_xrender_extension) {
					XResizeWindow(display, dev->window, dev->window_width, dev->window_height);
					double x_scale_factor = (double)dev->canvas_width / (double)dev->window_width;					
					double y_scale_factor = (double)dev->canvas_height / (double)dev->window_height;					
					dev->transform.matrix[0][0] = XDoubleToFixed(x_scale_factor);
					dev->transform.matrix[0][1] = XDoubleToFixed(0);
					dev->transform.matrix[0][2] = XDoubleToFixed(0);
					dev->transform.matrix[1][0] = XDoubleToFixed(0);
					dev->transform.matrix[1][1] = XDoubleToFixed(y_scale_factor);
					dev->transform.matrix[1][2] = XDoubleToFixed(0);
					dev->transform.matrix[2][0] = XDoubleToFixed(0);
					dev->transform.matrix[2][1] = XDoubleToFixed(0);
					dev->transform.matrix[2][2] = XDoubleToFixed(1);			
				} else {
					if (dev->scaled_ximage) XDestroyImage(dev->scaled_ximage);
				    	dev->scaled_canvas = malloc(dev->window_width * dev->window_height * 4);
				        dev->scaled_ximage = XCreateImage(display, dev->vinfo.visual,
				        	24, ZPixmap, 0, (char *)dev->scaled_canvas,
		                                dev->window_width, dev->window_height, 32, 0);
				}
				dev->window_resized = false;
			}
			else if (dev->callback && dev->frame_sync) {
				/* sync with vertical blank */
				if (dev->state && (!dev->vblank)) continue;
			}
#endif /* !WANT_SDL */
#ifdef HAS_NETSERVER
		}
		if (!dev->callback || n_flag) {
#else
		if (!dev->callback) {
#endif /* HAS_NETSERVER */
			if (T < dev->T_end_vscan) {
				sleep_for_us((dev->T_end_vscan - T) / f_value);
			}
			if (T < dev->T_end_vblank) {
				dev->vblank = true;
				sleep_for_us((dev->T_end_vblank - T) / f_value);
			}
			dev->vblank = false;
			dev->T_end_vscan = T + (f_value * dev->vscan_period);
			dev->T_end_vblank = dev->T_end_vscan + (f_value * dev->vblank_period);
		}

		if (dev->state) {
#ifdef HAS_NETSERVER
			if (!n_flag) {
#endif /* HAS_NETSERVER */
#ifndef WANT_SDL
				if (dev->interlaced)
					dev->field = (dev->field == ODD) ? EVEN : ODD;

				/* draw the frame/field */
	        		if (dev->draw_field) dev->draw_field();

				dev->frames++;

				XLockDisplay(display);
				if (has_xrender_extension) {
					XPutImage(display, dev->pixmap, dev->gc, dev->ximage, 0, 0, 0, 0, dev->canvas_width, dev->canvas_height);
					dev->canvas_pic = XRenderCreatePicture(display, dev->pixmap, dev->pict_format, 0, NULL);
					XRenderSetPictureTransform(display, dev->canvas_pic, &dev->transform);
				        XRenderComposite(display, PictOpSrc, dev->canvas_pic, 0, dev->window_pic,
				                         0, 0, 0, 0, 0, 0, dev->window_width, dev->window_height);
				}
				else {
					if (dev->scaled_ximage) {
						scale_ximage(dev->ximage, dev->scaled_ximage);
						XPutImage(display, dev->window, dev->gc, dev->scaled_ximage, 0, 0, 0, 0, dev->window_width, dev->window_height);
					}
					else {
						XPutImage(display, dev->window, dev->gc, dev->ximage, 0, 0, 0, 0, dev->canvas_width, dev->canvas_height);
					}
				}
				XUnlockDisplay(display);
#endif /* !WANT_SDL */
#ifdef HAS_NETSERVER
			} else {
				ws_refresh(device_handle);
			}
#endif /* HAS_NETSERVER */
		}
		else {
			sleep_for_us(dev->vscan_period);
		}
	}

	/* just in case it ever gets here */
	pthread_exit(NULL);
}

#endif /* !WANT_SDL || !HAS_NETSERVER */

/*
 *	Initialize frame buffer video
 */
int fb_video_init(
	int width,			/* video width in pixel */
	int height,			/* video height in pixel */
	const char *title,		/* title of video window */
	uint8_t (*colors)[3],		/* custom color map (optional) */
	uint8_t (*gradients)[3],	/* custom gradient map (optional) */
	bool stats,			/* enable statistics */
	bool frame_sync,		/* perform changes only during vertical blank period */
	bool interlaced,		/* enable interlaced display */
	int vscan_period,		/* vertical scan period in microseconds */
	int vblank_period,		/* vertical blank period in microseconds */
	void *ws_update,		/* webserver update callback */
	void *draw_field,		/* draw field callback */
	void *callback,			/* CPU timer callback */
	void *user_data,		/* user data for CPU timer callback */
	bool **ret_vblank,		/* returns pointer to vblank status */
	int **ret_field,		/* returns pointer to field status */
	int **ret_timer_id)		/* returns pointer to timer ID */
{
	int device_handle;
	
	if (num_devices >= MAX_NUM_DEVICES) {
		LOGE(TAG, "Failed to register device (max. %d video devices supported)\n", MAX_NUM_DEVICES);
		return -1;
	}
	
	for (device_handle=0; device_handle<MAX_NUM_DEVICES; device_handle++) {
		if ((used_devices & (1 << device_handle)) == 0) break;
	}
	used_devices |= 1 << device_handle;
	num_devices++;
	FBVideo *dev = &fb_video_devices[device_handle];

	dev->canvas_width = width;
	dev->canvas_height = height;
	dev->stats = stats;
	dev->title = title;
	dev->frame_sync = frame_sync;
	dev->interlaced = interlaced;
	dev->vscan_period = vscan_period;
	dev->vblank_period = vblank_period;
	dev->draw_field = draw_field;
	dev->callback = callback;
	dev->user_data = user_data;
	
	dev->colors = standard_colors;
	if (colors) {
		dev->colors = colors;
	}
	dev->gradients = standard_gradients;
	if (gradients) {
		dev->gradients = gradients;
	}
#ifdef HAS_NETSERVER
	dev->ws_update = ws_update;
#else
	UNUSED(ws_update);
#endif

	/* other initilizations */
	dev->T_end_vscan = 0;
	dev->T_end_vblank = 0;
	dev->vblank = false;
	dev->field = FULL;
	dev->window_resized = false;
	dev->state = true;
	dev->frames = 0;
	dev->start_time = get_clock_us();
	dev->timer_id = -1;

	open_handle = device_handle;	// we need to make the handle persistent in case creating a thread takes longer

#ifdef HAS_NETSERVER
	if (!n_flag) {
#endif
#ifdef WANT_SDL
		dev->client_id = simsdl_create2(device_handle, &fb_video_funcs);
#else
		open_window(device_handle);
		if (display == NULL) return -1;
#endif
#ifdef HAS_NETSERVER
	} else {
		ws_clear(device_handle);
	}
#endif /* HAS_NETSERVER */

#if defined(WANT_SDL) && defined(HAS_NETSERVER)
	if (n_flag) {
#endif
#if !defined(WANT_SDL) || defined(HAS_NETSERVER)
		if (pthread_create(&dev->thread, NULL, update_thread, &open_handle)) {
			LOGE(TAG, "can't create thread");
			return -1;
		}
#endif
#if defined(WANT_SDL) && defined(HAS_NETSERVER)
	}
#endif
	if (ret_vblank) *ret_vblank = &dev->vblank;
	if (ret_field) *ret_field = &dev->field;
	if (ret_timer_id) *ret_timer_id = &dev->timer_id;

	return device_handle;
}