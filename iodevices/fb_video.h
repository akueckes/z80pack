/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Common I/O devices used by various simulated machines
 *
 * Copyright (C) 2015-2019 by Udo Munk
 * Copyright (C) 2018 David McNaughton
 * Copyright (C) 2025 by Thomas Eberhardt
 * Copyright (C) 2026 Ansgar Kueckes (full rework)
 *
 * Emulation of a Cromemco DAZZLER S100 board
 *
 * History:
 * 03-MAY-2015 first version
 */

#ifndef FB_VIDEO_INC
#define FB_VIDEO_INC

/* interlaced video */
#define EVEN	0		/* only even fields */
#define ODD	1		/* only odd fields */
#define FULL	2		/* all fields */

extern int fb_video_init(
	int width,			/* video width in pixel */
	int height,			/* video height in pixel */
	const char *title,		/* title of video window */
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
	int **ret_field,			/* returns pointer to field status */
	int **ret_timer_id);		/* returns pointer to timer ID */

extern void fb_video_show(int device_handle);
extern void fb_video_hide(int device_handle);
extern void fb_video_off(int device_handle);
extern void set_fg_color(int device_handle, int i);
extern void set_fg_gradient(int device_handle, int i);
extern void fill_rect(int device_handle, int x, int y, int w, int h, uint8_t (*fg)[3]);
extern void draw_pattern(int device_handle, int x, int y, int psize, uint8_t data, uint8_t (*fg)[3], uint8_t (*bg)[3]);

#endif /* !FB_VIDEO_INC */
