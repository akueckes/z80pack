/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 1987-2024 by Udo Munk
 * Copyright (C) 2024 by Thomas Eberhardt
 */

/*
 *	This module contains functions for CPU/Bus-handling
 */

#include <stdio.h>
#include <stdlib.h>

#include "sim.h"
#include "simdefs.h"
#include "simglb.h"
#include "simport.h"
#include "simmem.h"
#include "simio.h"
#ifndef EXCLUDE_I8080
#include "sim8080.h"
#endif
#ifndef EXCLUDE_Z80
#include "simz80.h"
#endif
#include "simcore.h"

#ifdef FRONTPANEL
#include "frontpanel.h"
#include "simctl.h"
#endif

/* #define LOG_LOCAL_LEVEL LOG_DEBUG */
#include "log.h"
static const char *TAG = "core";

/*
 *	Initialize the CPU
 */
void init_cpu(void)
{
	/* same for i8080 and Z80 */
	PC = 0;
	SP = rand() % 65536;
	A = rand() % 256;
	B = rand() % 256;
	C = rand() % 256;
	D = rand() % 256;
	E = rand() % 256;
	H = rand() % 256;
	L = rand() % 256;
	F = rand() % 256;

#ifndef EXCLUDE_Z80
	I = 0;
	A_ = rand() % 256;
	B_ = rand() % 256;
	C_ = rand() % 256;
	D_ = rand() % 256;
	E_ = rand() % 256;
	H_ = rand() % 256;
	L_ = rand() % 256;
	F_ = rand() % 256;
	IX = rand() % 65536;
	IY = rand() % 65536;
#endif

#ifndef EXCLUDE_I8080
	if (cpu == I8080) {
		F &= ~(Y_FLAG | X_FLAG);
		F |= N_FLAG;
	}
#endif

#ifdef CPU_TIMER
	init_cpu_timer();
#endif
}

/*
 *	Reset the CPU
 */
void reset_cpu(void)
{
	IFF = 0;
	int_int = int_protection = false;
	int_data = -1;

	PC = 0;

#ifndef EXCLUDE_Z80
	I = 0;
	R_ = R = 0;
	int_mode = 0;
	int_nmi = false;
#endif
}

#if !defined (EXCLUDE_I8080) && !defined(EXCLUDE_Z80)
/*
 *	Switch the CPU model
 */
void switch_cpu(int new_cpu)
{
	if (cpu != new_cpu) {
		if (new_cpu == I8080) {
			F &= ~(Y_FLAG | X_FLAG);
			F |= N_FLAG;
		}
		cpu = new_cpu;
		cpu_state = ST_MODEL_SWITCH;
	}
}
#endif

/*
 *	Run CPU
 */
void run_cpu(void)
{
	cpu_state = ST_CONTIN_RUN;
	cpu_error = NONE;
	while (true) {
		switch (cpu) {
#ifndef EXCLUDE_Z80
		case Z80:
			cpu_z80();
			break;
#endif
#ifndef EXCLUDE_I8080
		case I8080:
			cpu_8080();
			break;
#endif
		default:
			break;
		}
		if (cpu_state == ST_MODEL_SWITCH) {
			cpu_state = ST_CONTIN_RUN;
			continue;
		} else
			break;
	}
}

/*
 *	Step CPU
 */
void step_cpu(void)
{
	cpu_state = ST_SINGLE_STEP;
	cpu_error = NONE;
	switch (cpu) {
#ifndef EXCLUDE_Z80
	case Z80:
		cpu_z80();
		break;
#endif
#ifndef EXCLUDE_I8080
	case I8080:
		cpu_8080();
		break;
#endif
	default:
		break;
	}
	cpu_state = ST_STOPPED;
}

/*
 *	Report CPU error
 */
void report_cpu_error(void)
{
	if (cpu_error == NONE)
		return;

	/* always start on a new line */
	LOG(TAG, "\r\n");

	switch (cpu_error) {
	case NONE:
		break;
	case OPHALT:
		LOG(TAG, "INT disabled and HALT Op-Code reached at 0x%04x\r\n",
		    PC - 1);
		break;
	case IOTRAPIN:
		LOGE(TAG, "I/O input Trap at 0x%04x, port 0x%02x",
		     PC, io_port);
		break;
	case IOTRAPOUT:
		LOGE(TAG, "I/O output Trap at 0x%04x, port 0x%02x",
		     PC, io_port);
		break;
	case IOHALT:
		LOG(TAG, "System halted\r\n");
		break;
	case IOERROR:
		LOGE(TAG, "Fatal I/O Error at 0x%04x", PC);
		break;
	case OPTRAP1:
		LOGE(TAG, "Op-code trap at 0x%04x 0x%02x",
		     PC - 1, getmem(PC - 1));
		break;
	case OPTRAP2:
		LOGE(TAG, "Op-code trap at 0x%04x 0x%02x 0x%02x",
		     PC - 2, getmem(PC - 2), getmem(PC - 1));
		break;
	case OPTRAP4:
		LOGE(TAG, "Op-code trap at 0x%04x 0x%02x 0x%02x 0x%02x 0x%02x",
		     PC - 4, getmem(PC - 4), getmem(PC - 3),
		     getmem(PC - 2), getmem(PC - 1));
		break;
	case USERINT:
		LOG(TAG, "User Interrupt at 0x%04x\r\n", PC);
		break;
	case INTERROR:
		LOGW(TAG, "Unsupported bus data during INT: 0x%02x", int_data);
		break;
	case POWEROFF:
		LOG(TAG, "System powered off\r\n");
		break;
	default:
		LOGW(TAG, "Unknown error %d", cpu_error);
		break;
	}
}

/*
 * print some execution statistics
 */
void report_cpu_stats(void)
{
	unsigned freq;

	if (cpu_time)
	{
		freq = (unsigned) (cpu_freq / 10000);
		printf("I/O ran for %" PRIu64 " ms, ", total_io_time / 1000);
		printf("waited for %" PRIu64 " ms\n", total_wait_time / 1000);
		printf("CPU executed %" PRIu64 " t-states ", T);
		printf("in %" PRIu64 " ms\n", cpu_time / 1000);
		printf("Clock frequency %u.%02u MHz\n",
		       freq / 100, freq % 100);
	}
}

/*
 *	This function is called for every IN opcode from the
 *	CPU emulation. It calls the handler for the port,
 *	from which input is wanted.
 */
BYTE io_in(BYTE addrl, BYTE addrh)
{
	uint64_t t;
#ifdef FRONTPANEL
	bool val;
#else
#ifndef SIMPLEPANEL

	UNUSED(addrh);
#endif
#endif

	io_port = addrl;
	if (port_in[addrl]) {
		t = get_clock_us();
		io_data = (*port_in[addrl])();
		io_time += get_clock_us() - t;
	} else {
		if (i_flag) {
			cpu_error = IOTRAPIN;
			cpu_state = ST_STOPPED;
		}
		io_data = IO_DATA_UNUSED;
	}

#ifdef BUS_8080
	cpu_bus = CPU_WO | CPU_INP;
#endif

#ifdef FRONTPANEL
	if (F_flag) {
		fp_clock += 3;
		fp_led_address = (addrh << 8) + addrl;
		fp_led_data = io_data;
		fp_sampleData();
		val = wait_step();

		/* when single stepped INP get last set value of port */
		if (val && port_in[addrl]) {
			t = get_clock_us();
			io_data = (*port_in[addrl])();
			io_time += get_clock_us() - t;
		}
	}
#endif
#ifdef SIMPLEPANEL
	fp_led_address = (addrh << 8) + addrl;
	fp_led_data = io_data;
#endif

#if defined(INFOPANEL) || defined(IOPANEL)
	port_flags[addrl].in = true;
#endif

	LOGD(TAG, "input %02x from port %02x", io_data, io_port);

	return io_data;
}

/*
 *	This function is called for every OUT opcode from the
 *	CPU emulation. It calls the handler for the port,
 *	to which output is wanted.
 */
void io_out(BYTE addrl, BYTE addrh, BYTE data)
{
	uint64_t t;

#if !defined(FRONTPANEL) && !defined(SIMPLEPANEL)
	UNUSED(addrh);
#endif

	io_port = addrl;
	io_data = data;

	LOGD(TAG, "output %02x to port %02x", io_data, io_port);

	busy_loop_cnt = 0;

	if (port_out[addrl]) {
		t = get_clock_us();
		(*port_out[addrl])(data);
		io_time += get_clock_us() - t;
	} else {
		if (i_flag) {
			cpu_error = IOTRAPOUT;
			cpu_state = ST_STOPPED;
		}
	}

#ifdef BUS_8080
	cpu_bus = CPU_OUT;
#endif

#ifdef FRONTPANEL
	if (F_flag) {
		fp_clock += 6;
		fp_led_address = (addrh << 8) + addrl;
		fp_led_data = IO_DATA_UNUSED;
		fp_sampleData();
		wait_step();
	}
#endif
#ifdef SIMPLEPANEL
	fp_led_address = (addrh << 8) + addrl;
	fp_led_data = IO_DATA_UNUSED;
#endif

#if defined(INFOPANEL) || defined(IOPANEL)
	port_flags[addrl].out = true;
#endif
}

/*
 *	Start a bus request cycle
 */
void start_bus_request(BusDMA_t mode, BusDMAFunc_t *bus_master)
{
	bus_mode = mode;
	dma_bus_master = bus_master;
	bus_request = 1;
}

/*
 *	End a bus request cycle
 */
void end_bus_request(void)
{
	bus_mode = BUS_DMA_NONE;
	dma_bus_master = NULL;
	bus_request = 0;
}

#ifdef CPU_TIMER
typedef struct {
	bool in_use;				/* true for used slots */
	int priority;				/* priority (the timer with the lowest number is served first, 0=highest priority) */
	Tstates_t value;			/* tick count when timer callback function will be called (0 = disarmed) */
	void (*callback)(void *user_data);	/* function to be called if timer fires (NULL = no action) */
	void *user_data;			/* pointer to user data (NULL = no user data) */
} CPU_Timer;

static CPU_Timer cpu_timer[MAX_CPU_TIMER];	/* the timer slots */
static int cpu_timer_queue[MAX_CPU_TIMER];	/* an ordered list of the timers for prioritization */

/*
 *	Initialize all CPU timers
 */
void init_cpu_timer()
{
	for (int i=0; i<MAX_CPU_TIMER; i++) {
		cpu_timer[i].in_use = false;
		cpu_timer[i].priority = 0;
		cpu_timer[i].value = 0;
		cpu_timer[i].callback = NULL;
		cpu_timer[i].user_data = NULL;

		cpu_timer_queue[i] = -1;
	}
}

/*
 *	Allocate a free timer slot for use. Returns a timer ID in case of success,
 *	or -1 otherwise.
 */
int register_cpu_timer(int priority, void (*callback)(void *user_data), void *user_data)
{
	int timer_id, i;

	/* queue full? */
	if (cpu_timer_queue[MAX_CPU_TIMER-1] >= 0) return -1;

	/* assign next free slot */
	for (timer_id=0; timer_id<MAX_CPU_TIMER; timer_id++) {
		if (!cpu_timer[timer_id].in_use) {
			cpu_timer[timer_id].in_use = true;
			cpu_timer[timer_id].priority = priority;
			cpu_timer[timer_id].value = 0;
			cpu_timer[timer_id].callback = callback;
			cpu_timer[timer_id].user_data = user_data;
			break;
		}
	}
	if (timer_id == MAX_CPU_TIMER) return -1;
	
	/* assign position in queue */
	for (i=0; i<MAX_CPU_TIMER; i++) {
		if (cpu_timer_queue[i] < 0) break;
		if (priority < cpu_timer[cpu_timer_queue[i]].priority) {		
			for (int j=MAX_CPU_TIMER-1; j>i; j--) {
				cpu_timer_queue[j] = cpu_timer_queue[j-1];
			}
			break;
		}
	}
	if (i == MAX_CPU_TIMER) return -1;
	cpu_timer_queue[i] = timer_id;
	
	return timer_id;
}

/*
 *	Free up a timer which is not used any more
 */
int unregister_cpu_timer(int timer_id)
{
	int i,j;

	if ((timer_id < 0) || (timer_id >= MAX_CPU_TIMER)) return -1;
	if (!cpu_timer[timer_id].in_use) return -1;

	/* reset slot */
	cpu_timer[timer_id].in_use = false;
	cpu_timer[timer_id].value = 0;
	cpu_timer[timer_id].callback = NULL;
	cpu_timer[timer_id].user_data = NULL;

	/* remove from queue */
	for (i=0; i<MAX_CPU_TIMER; i++) {
		if (cpu_timer_queue[i] == timer_id) {
			for (j=i; j<MAX_CPU_TIMER-1; j++) {
				cpu_timer_queue[j] = cpu_timer_queue[j+1];
			}
			cpu_timer_queue[j] = -1;
			break;
		}
	}

	return 0;
}

/*
 *	Set when the timer shall trigger (in CPU ticks). A value of 0 stopps a timer.
 */
int set_cpu_timer(int timer_id, uint64_t cpu_ticks)
{
	if ((timer_id < 0) || (timer_id >= MAX_CPU_TIMER)) return -1;
	if (!cpu_timer[timer_id].in_use) return -1;

	cpu_timer[timer_id].value = cpu_ticks;

	return 0;
}

/*
 *	Return the current setting of a timer (in CPU ticks). A return value of 0
 *	indicates the timer doesn't exist or is being stopped.
 */
uint64_t get_cpu_timer(int timer_id)
{
	if ((timer_id < 0) || (timer_id >= MAX_CPU_TIMER)) return 0;
	if (!cpu_timer[timer_id].in_use) return 0;

	return cpu_timer[timer_id].value;
}

/*
 *	Return the ID of a timer from the prioritization queue
 */
int get_cpu_timer_id(int position)
{
	if ((position < 0) || (position >= MAX_CPU_TIMER)) return -1;
	if (cpu_timer_queue[position] < 0) return -1;
	if (!cpu_timer[cpu_timer_queue[position]].in_use) return -1;

	return cpu_timer_queue[position];
}

/*
 *	Set the callback function for the timer. Also sets the link of
 *	the user data, which is handed over during the call when the
 *	timer is triggered.
 */
void trigger_cpu_timer_callback(int timer_id)
{
	if ((timer_id < 0) || (timer_id >= MAX_CPU_TIMER)) return;
	if (!cpu_timer[timer_id].in_use) return;

	if (cpu_timer[timer_id].callback) {
		cpu_timer[timer_id].callback(cpu_timer[timer_id].user_data);
	}
}
#endif /* CPU_TIMER */
