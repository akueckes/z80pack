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
 * 07-APR-2026 rework for MAME like CPU-to-video synchronization
 * 03-MAY-2026 moved all device independent code to fb_video.c
*/

/*
	This device module implements the Cromemco Dazzler graphics board. It also works
	as an example how to implement a graphics device with time correct state model
	synchronized with the CPU emulation, and diverse rendering options decoupled
	from the actual device emulation.
	
	Configurable properties in the system.conf file:
	
	dazzler_interlaced              Set this property to 1 to show interlacing effects

	dazzler_frame_sync              Set this poperty to 0 to restrict display changes
	                                to the Dazzler vertical blank period

	dazzler_stats                   Set this property to 1 to enable statistics output
	                                
	Dazzler technical characteristics
	---------------------------------
	
	  3.579545 MHz hardware clock / NTSC carrier frequency
	  1.790 MHz pixel clock
	  15.98 KHz line frequency
	  62 Hz vertical frequency (interlaced)
	  Vertical scan period 12 ms
	  Vertical blank period 4 ms
	  DMA cycle 375 us
	  12 scanlines/pixel for low resolution nibble mode, 32x32
	  6 scanlines/pixel for medium resolution nibble mode, 64x64
	  3 scanlines/pixel for high resolution x4 mode, 128x128
	  384 scanlines per frame
	  192 scanlines per field (interlaced)
	  16 or 32 memory locations per line, depending on the video mode
	
	How the Dazzler works
	---------------------
	
	The whole display field is divided into DMA cycles, where the Dazzler
	board fetches the display data from the main memory at the memory
	address	defined in the control register accessible via I/O port 0xE.
	Depending on the current video mode, the Dazzler fetches either
	16 or 32 bytes per DMA cycle every 375 microseconds.
	
	The data is copied into a 4x64-bit shift register, which operates
	as a cache ("recycle buffer") for up to 64 nibbles, so that the pixel
	data can be streamed for each following scanline without the need
	for re-fetching the data from main memory. Each DMA cycle covers 12
	scanlines in 512 byte mode, and 6 scanlines in 2K byte mode.
	
	For creating the color information on the screen, a format register
	which is accessable via I/O port 0xF is used to set the graphics
	mode, which again determines how the data in the line buffer is
	being interpreted.
	
	The Dazzler hardware makes information about the DMA cycles and
	the vertical sync of the video signal available to the CPU via
	a flags register at I/O port 0xE.
	
	For accurate emulation, the host actually should be put into hold mode
	during the DMA fetch, which in the real hardware is slowing down
	processing by roughly 15%.
	
	Emulation
	---------

	Just as with the real Dazzler hardware, the scanline counter triggers
	the DMA cycles for "stealing" the pixel data from the video buffer
	memory. The meaning of the fetched pixel data depends on the current
	video mode, which is controlled by the format register accesible
	through I/O port 0xF. The content of both Dazzler registers are always
	effective, which means that they can be altered on-the-fly during
	display refresh.
	
	   +---+---+---+---+---+---+---+---+
	   | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |   control register port 0xE (write)
	   +---+---+---+---+---+---+---+---+
	     |   |   |   |   |   |   |   |
	     |   +---+---+---+---+---+---+--- video memory address bits 8-15
	     |   
	     +------- enable Dazzler

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
	   | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |   flags register port 0xE (read)
	   +---+---+---+---+---+---+---+---+
	     |   |   |   |   |   |   |   |
	     |   |   +---+---+---+---+---+--- don't care
	     |   +--- vertical blank
	     +------- odd-line-even-line

	Bit 6 can the used to perform changes on the display data during
	vertical blank without breaking the picture. Bit 7 can be used to
	track the switches between DMA cycles / scanline groups subsequently
	written to the display. Tracking those groups from the start of the
	screen gives (roughly) the current vertical position of the beam.
	
	The vertical blank flag can also be used as real time reference, as the 
	flag is changing with 62 Hz. The odd-line-even-line flag is only active
	during the scan period when bit 6 is true.
	
	The reference to "even lines/odd lines" is a bit misleading, since that
	flag actually refers to the DMA cycles, which are matching display
	lines only in the nibble mode, not in the x4 mode, and are especially
	not referring scanlines.
	
	   +---+---+---+---+---+---+---+---+
	   | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |   format register port 0xF (write)
	   +---+---+---+---+---+---+---+---+
	     |   |   |   |   |   |   |   |
	     |   |   |   |   |   +---+---+--- color (0=black, 1=red, 2=green 3=yellow 4=blue, 5=magenta, 6=cyan, 7=white)
             |   |   |   |   +--- luminance
	     |   |   |   +------- gray vs. color
	     |   |   +----------- 512 vs. 2K
	     |   +--------------- nibble vs. hires
	     +------------------- don't care

	The real hardware puts the CPU into a hold state during DMA transfers,
	which should be implemented by calling the emulation's bus request
	function, which is also used here with every DMA cycle.
	
	The real Dazzler hardware is always operating in interlaced mode, where
	the display shows fields of even and odd scanlines alternating with a
	vertical frequency of 62 Hz.
	
	By default, this emulation runs in a flickerless non-interlaced
	mode, which flattens all scanlines into a single frame with up to 62 Hz
	refresh. You can switch to the visually more accurate interlaced mode
	by setting the dazzler_interlaced property in the system.conf file
	to 1.
	
	CPU-to-Video synchronization
	----------------------------
	
	Tweaking the video options in real time requires a tight synchronization
	between the machine commands executed by the emulation and the drawing
	of the display. This is achieved by implementing a callback function
	which will be triggered by a CPU timer on the base of CPU ticks,
	requiring a small change to the core CPU emulation.
	
	The callbacks will be processed with 62 callbacks/second with relation
	to the CPU ticks als timing reference. Each callback draws a full
	field into a canvas buffer, which later is being rendered by the X11
	server and/or the SDL2 video subsystem.
	
	For SDL2, the actual video rendering should be done in the main thread,
	and therefore is done via regular calls from the SDL2 main function.
	
	Updating of the display related Dazzler registers and frame buffer
	are discoupled from the actual rendering (which is done with best-effort
	speed) and such can be simulated accurately independent	of the actual
	refresh rate.
	
	Webserver
	---------
	
	This Dazzler implementation also works with the z80pack web desktop if
	HAS_NETSERVER has been defined during compilation and the netserver is
	enabled on the command line.
	
	Only the data of the frame buffer memory and the content of the format
	register will be transferred to the netserver, The Dazzler rendering is
	performed by a Javascript engine at the client browser.
	
	Note that the current implementation of the Dazzler Javascript client
	has a bug in the 4x4 graphics mode, only writing the pixel data
	but not erasing the pixels not set.

	TODOs
	-----

	Because of the many supported options, and the corresponding
	conditional compliles, the code becomes a bit harder to read. I'll
	try to reduce it to the options which work out best or try to make
	an automatic selection in the future.
	
	Since the canvas data structure in fact is a square array of
	32-bit RGBA values with the same size across all the different
	rendering options, it should be consolidated.
	
	In general, it shows that SDL is usable, but for 2D rendering, a
	generic X11 implementation can be way more performant, even if
	intrinsic GPU support for scaling is substituted by a software scaler.
	The main advantage of SDL is compatibility across non-X11 platforms
	such as MS Windows, which in fact is no option with z80pack anyway.
*/

#define BUSMASTER			/* define to simulate busmaster operation for Dazzler DMA */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "sim.h"
#include "simdefs.h"
#include "simglb.h"
#include "simcfg.h"
#include "simmem.h"
#include "simport.h"
#include "simcore.h"
#include "fb_video.h"

#ifdef HAS_DAZZLER

#ifdef HAS_NETSERVER
#include "netsrv.h"
#endif

/* #define LOG_LOCAL_LEVEL LOG_DEBUG */
#include "log.h"
static const char *TAG = "DAZZLER";

#include "cromemco-dazzler.h"

/* parameters configurable in system.conf */
bool dazzler_stats = false;		/* collect & output some statistics */
bool dazzler_frame_sync = true;		/* wait for end of scan period for changing display by default */
bool dazzler_interlaced = false;	/* non-interlaced display by default */

/* Dazzler stuff */
typedef struct {
	int scanline;			/* current scanline */
	BYTE flags;			/* current flags */
	BYTE format;			/* current format */
	WORD video_addr;		/* start of video buffer */
	WORD dma_offset;		/* current address in video buffer */
} Dazzler_registers;

static Dazzler_registers dazzler = { 0, 0xff, 0x00, 0x0000, 0 };
static BYTE line_buffer[32];
static int fb_video_device = -1;

/* video dimensions */
#define HSIZE 384
#define VSIZE 384

static bool *vblank;
static int *field;
static int *timer_id;

/*
 *	Switch DAZZLER off from front panel or window termination
 */
void cromemco_dazzler_off(void)
{
	if (fb_video_device >= 0)
		fb_video_off(fb_video_device);
	fb_video_device = -1;
}

#ifdef BUSMASTER
/*
 *	Perform busmaster memory access
 */
static Tstates_t dazzler_busmaster(BYTE bus_ack)
{	
	if (!bus_ack) return 0;

#if 0
	/* read DMA memory into line buffer (currently won't work with reliable timing) */
	int bytepos, offset;
	for (bytepos=0; bytepos<dazzler.num_bytes; bytepos++) {
		offset = bytepos % 16;
		if (dazzler.format & 0x20) {
			/* add quadrant offset */
			if (bytepos > 15) offset += 512;
			if (dazzler.scanline > 191) offset += 512;
		}
		line_buffer[bytepos] = dma_read(dazzer.video_addr + dazzler.dma_offset + offset);
	}
#endif

	/* simulate bus master activity by returning t-states, slowing down CPU by about 15% */
	return (dazzler.format & 0x20 ? 32 : 16) * 3;  /* 3 t-states per byte of DMA */
}
#endif /* BUSMASTER */

/*
 *	Fill line buffer from video buffer via DMA
 */
static void process_dma(Dazzler_registers *dazzler, int num_bytes)
{
	int bytepos, offset;

	/* read data bytes into line buffer */
	for (bytepos=0; bytepos<num_bytes; bytepos++) {
		/* read DMA memory into line buffer */
		offset = bytepos % 16;
		if (dazzler->format & 0x20) {
			/* add quadrant offset */
			if (bytepos > 15) offset += 512;
			if (dazzler->scanline > 191) offset += 512;
		}
		line_buffer[bytepos] = dma_read(dazzler->video_addr + dazzler->dma_offset + offset);
	}
#ifdef BUSMASTER
	/* simulate bus master activity */
	start_bus_request(BUS_DMA_CONTINUOUS, &dazzler_busmaster);
#endif
}

/*
 *	Draw content of line buffer into canvas under control of the Dazzler format register
 */
static void draw_scanline(BYTE format, int num_bytes, int hires_subrow, int vpos, int psize)
{
	int bytepos;
	uint8_t i;

	/* select foreground color for x4 mode */
	if (format & 0x40) {
		if (format & 0x10) set_fg_color(fb_video_device, format & 0x0f);
		else set_fg_gradient(fb_video_device, format & 0x0f);
	}

	for (bytepos=0; bytepos<num_bytes; bytepos++) {

		if (format & 0x40) {	/* x4 mode */
			/* render pixels */
			i = line_buffer[bytepos];
			if (hires_subrow == 0) {
				/* first 3 scanline subrow */
				if (i & 0x01)
					fill_rect(fb_video_device, bytepos * 4 * psize, vpos, psize, 1);
				if (i & 0x02)
					fill_rect(fb_video_device, (bytepos * 4 + 1) * psize, vpos, psize, 1);
				if (i & 0x10)
					fill_rect(fb_video_device, (bytepos * 4 + 2) * psize, vpos, psize, 1);
				if (i & 0x20)
					fill_rect(fb_video_device, (bytepos * 4 + 3) * psize, vpos, psize, 1);
			} else {
				/* second 3 scanline subrow */
				if (i & 0x04)
					fill_rect(fb_video_device, bytepos * 4 * psize, vpos, psize, 1);
				if (i & 0x08)
					fill_rect(fb_video_device, (bytepos * 4 + 1) * psize, vpos, psize, 1);
				if (i & 0x40)
					fill_rect(fb_video_device, (bytepos * 4 + 2) * psize, vpos, psize, 1);
				if (i & 0x80)
					fill_rect(fb_video_device, (bytepos * 4 + 3) * psize, vpos, psize, 1);
			}
		}
		else {	/* nibble mode */

			/* first pixel */
			i = line_buffer[bytepos] & 0x0f;
			if (format & 0x10) {
				set_fg_color(fb_video_device, i);	/* color */
			}
			else {
				set_fg_gradient(fb_video_device, i);	/* grayscale */
			}
			fill_rect(fb_video_device, bytepos * 2 * psize, vpos, psize, 1);

			/* second pixel */
			i = (line_buffer[bytepos] & 0xf0) >> 4;
			if (format & 0x10) {
				set_fg_color(fb_video_device, i);	/* color */
			}
			else {
				set_fg_gradient(fb_video_device, i);	/* grayscale */
			}
			fill_rect(fb_video_device, (bytepos * 2 + 1) * psize, vpos, psize, 1);
		}
	}
}

/*
 *	Render line buffer content into canvas
 */
static void draw_line_buffer(Dazzler_registers *dazzler, int num_lines, int num_bytes, int psize)
{
	int hires_subrow, current_line, vpos, width;

	/* make sure everything is initialized */
	if (fb_video_device < 0) return;

    	if (dazzler->format & 0x40) width = num_bytes * 4 * psize;
    	else width = num_bytes * 2 *psize;

	hires_subrow = 0;
	for (current_line=0; current_line<num_lines; current_line++) {
		if (dazzler->format & 0x40) {
			/* check which subrow we're in */
			hires_subrow = current_line / psize;
		}
		vpos = dazzler->scanline;
		if (dazzler_interlaced) {
			if (((*field == ODD) && (dazzler->scanline % 2 == 1)) ||
			    ((*field == EVEN) && (dazzler->scanline % 2 == 0))) {
			    	if (dazzler->format & 0x40) {
					/* erase scanline */
					set_fg_color(fb_video_device, 0);
					fill_rect(fb_video_device, 0, vpos, width, 1);
				}

			    	/* draw scanline */
				draw_scanline(dazzler->format, num_bytes, hires_subrow, vpos, psize);
			}
			else {
				/* erase scanline */
				set_fg_color(fb_video_device, 0);
				fill_rect(fb_video_device, 0, vpos, width, 1);
			}
		}
		else {
		    	if (dazzler->format & 0x40) {
				/* erase scanline */
				set_fg_color(fb_video_device, 0);
				fill_rect(fb_video_device, 0, vpos, width, 1);
			}
			draw_scanline(dazzler->format, num_bytes, hires_subrow, vpos, psize);
		}
		dazzler->scanline++;
	}
}

#ifdef CPU_TIMER
/*
 *	Render a full DMA cycle from video memory into visual with the use of a CPU timer
 *	for near perfect synchronization with emulated CPU (called from simulator thread)
 */
void cromemco_dazzler_timer_callback(void *user_data)
{
	Dazzler_registers *dazzler = (Dazzler_registers *)user_data;
	int num_bytes, num_dma, num_lines, psize;
	uint64_t dma_cycle;
	
	if (!user_data) return;
	
	num_bytes = dazzler->format & 0x20 ? 32 : 16;			/* bytes per DMA cycle */
	num_dma = dazzler->format & 0x20 ? 64 : 32;			/* DMA cycles per frame */
	num_lines = 384 / num_dma;					/* scanlines per DMA cycle */
	dma_cycle = (num_lines * f_value * 1000000) / 15980;		/* DMA cycle in CPU ticks */

	if (dazzler->format & 0x40) psize = 192 / num_dma;		/* hires monochrome (x4 mode) */
	else psize = 384 / num_dma;					/* color/grayscale (nibble) mode */

	/* end of vertical blank period? */
	if (!(dazzler->flags & 0x40)) {
		dazzler->scanline = 0;
		dazzler->dma_offset = 0;
		dazzler->flags = 0xff;
		*vblank = false;
	}
	
#ifdef HAS_NETSERVER
	if (!n_flag) {
#endif
		/* fill line buffer from video memory */
		process_dma(dazzler, num_bytes);
	
		/* transfer graphics data into canvas */
		draw_line_buffer(dazzler, num_lines, num_bytes, psize);
#ifdef HAS_NETSERVER
	}
	else {
		dazzler->scanline += num_lines;
	}
#endif

	if (dazzler->scanline < 384) {
		dazzler->dma_offset += 16;			/* new start address */
		dazzler->flags ^= 0x80;				/* toggle line flag */

		/* re-schedule CPU timer for next DMA cycle */
		set_cpu_timer(*timer_id, T + dma_cycle);
	}
	else {
		/* field done, signal vertical blank */
		dazzler->flags = 0x3f;
		*vblank = true;

		/* re-schule CPU timer for 4 ms vertical blank  */
		set_cpu_timer(*timer_id, T + (f_value * 4000));
	}
}

#else

/*
 *	Render a full field from video memory into visual w/o CPU synchronization
 */
static void draw_field()
{
	int num_bytes, num_dma, num_lines, psize;

	dazzler.scanline = 0;
	dazzler.dma_offset = 0;
	dazzler.flags = 0xff;

	while (dazzler.scanline < 384) {

		/* register control - can be changed on the fly */
		num_bytes = dazzler.format & 0x20 ? 32 : 16;			/* bytes per DMA cycle */
		num_dma = dazzler.format & 0x20 ? 64 : 32;			/* DMA cycles per frame */
		num_lines = 384 / num_dma;					/* scanlines per DMA cycle */
		if (dazzler.format & 0x40) psize = 192 / num_dma;		/* hires monochrome (x4 mode) */
		else psize = 384 / num_dma;					/* color/grayscale (nibble) mode */

		/* fill line buffer from video memory */
		process_dma(&dazzler, num_bytes);

		/* draw data into canvas */
		draw_line_buffer(&dazzler, num_lines, num_bytes, psize);

		dazzler.dma_offset += 16;		/* new start address */
		dazzler.flags ^= 0x80;			/* toggle line flag */
	}
	
	dazzler.flags &= 0xbf;				/* clear vscan flag */
}

#endif /* CPU_TIMER */

#ifdef HAS_NETSERVER

static uint8_t dblbuf[2048];
static BYTE formatBuf = 0;
static struct {
	uint16_t format;
	uint16_t addr;
	uint16_t len;
	uint8_t buf[2048];
} msg;

/*
 *	Update netserver buffer from main memory & format register
 *
 *	Compares the current frame buffer memory with the last transmitted
 *	memory and sends continuous blocks of changes in separate chunks
 *	to the netserver. Blocks of changes can be seperated by a limited
 *	number of unchanged bytes, specified by LOOKAHEAD.
 */
void ws_update(bool do_clear)
{
	int len = (dazzler.format & 32) ? 2048 : 512;
	int num_dma = (dazzler.format & 0x20) ? 64 : 32;
	int addr;
	int i, n, x, la_count;
	bool cont;
	uint8_t val;
	
	dazzler.flags = 0xff;

	if (do_clear) {
		memset(dblbuf, 0, 2048);
		msg.format = 0;
		msg.addr = 0xFFFF;
		msg.len = 0;

		net_device_send(DEV_DZLR, (char *)&msg, msg.len + 6);

		LOGD(TAG, "Clear the screen.");
	}
	else {
		for (i = 0; i < len; i++) {

			if ((i % num_dma) == 0) dazzler.flags ^= 0x80;	/* toggle line flag */

			addr = i;
			n = 0;
			la_count = 0;
			cont = true;
			while (cont && (i < len)) {
				/* copy next block of changes into transfer buffer */
				val = dma_read(dazzler.video_addr + i);
				while ((val != dblbuf[i]) && (i < len)) {
					dblbuf[i++] = val;
					msg.buf[n++] = val;
					cont = false;
					val = dma_read(dazzler.video_addr + i);
				}
				/* if first memory location unchanged, skip look-ahead and
				   continue with next memory location */
				if (cont) break;
				x = 0;
#define LOOKAHEAD 6
				/* look-ahead up to n bytes for next change */
				while ((x < LOOKAHEAD) && !cont && (i < len)) {
					val = dma_read(dazzler.video_addr + i++);
					msg.buf[n++] = val;
					la_count++;
					val = dma_read(dazzler.video_addr + i);
					if ((i < len) && (val != dblbuf[i])) {
						cont = true;
					}
					x++;
				}
				/* if no further changes are found during look-ahead, transmit msg with
				   changes found */
				if (!cont) {
					n -= x;
					la_count -= x;
				}
			}
			if (n || (dazzler.format != formatBuf)) {
				formatBuf = dazzler.format;
				msg.format = dazzler.format;
				msg.addr = addr;
				msg.len = n;

				net_device_send(DEV_DZLR, (char *)&msg, msg.len + 6);

				LOGD(TAG, "BUF update 0x%04X-0x%04X "
				     "len: %d format: 0x%02X l/a: %d",
				     msg.addr, msg.addr + msg.len,
				     msg.len, msg.format, la_count);
			}
		}
	}

	dazzler.flags &= 0xbf;		/* clear vscan flag */
}
#else
static void ws_update(void){}		/* dummy function */
#endif /* HAS_NETSERVER */

/*
 *	Process data provided via output port command to control register
 */
void cromemco_dazzler_ctl_out(BYTE data)
{
	/* get DMA address for display memory */
	dazzler.video_addr = (data & 0x7f) << 9;
	
	/* switch DAZZLER on/off */
	if (data & 0x80) {
		if (fb_video_device >= 0) fb_video_show(fb_video_device);
		else {
#ifdef CPU_TIMER
			fb_video_device = fb_video_init(HSIZE, VSIZE, "Cromemco DAzzLER", NULL, NULL,
				dazzler_stats, dazzler_frame_sync, dazzler_interlaced, 12129, 4000,
				&ws_update, NULL, &cromemco_dazzler_timer_callback, &dazzler,
				&vblank, &field, &timer_id);
#else
			fb_video_device = fb_video_init(HSIZE, VSIZE, "Cromemco DAzzLER", NULL, NULL,
				dazzler_stats, dazzler_frame_sync, dazzler_interlaced, 12129, 4000,
				&ws_update, &draw_field, NULL, NULL, &vblank, &field, &timer_id);
#endif
			if (fb_video_device<0) {
				LOGE(TAG, "Could not initialize frame buffer video\n");
			}
		}
	} else {
		if (fb_video_device >= 0) fb_video_hide(fb_video_device);
	}
}

/*
 *	Return data requested via input port command from flags register
 */
BYTE cromemco_dazzler_flags_in(void)
{
	return dazzler.flags;
}

/*
 *	Process data provided via output port command to format register
 */
void cromemco_dazzler_format_out(BYTE data)
{
	dazzler.format = data;
}

#endif /* HAS_DAZZLER */
