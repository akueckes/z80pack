/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 1987-2024 by Udo Munk
 * Copyright (C) 2024 by Thomas Eberhardt
 */

#ifndef SIMCORE_INC
#define SIMCORE_INC

#include "sim.h"
#include "simdefs.h"

extern void init_cpu(void);
extern void reset_cpu(void);
#if !defined (EXCLUDE_I8080) && !defined(EXCLUDE_Z80)
extern void switch_cpu(int new_cpu);
#endif
extern void run_cpu(void);
extern void step_cpu(void);

extern void report_cpu_error(void);
extern void report_cpu_stats(void);

extern BYTE io_in(BYTE addrl, BYTE addrh);
extern void io_out(BYTE addrl, BYTE addrh, BYTE data);

extern void start_bus_request(BusDMA_t mode, BusDMAFunc_t *bus_master);
extern void end_bus_request(void);

#ifdef CPU_TIMER
extern Tstates_t cpu_timer_value;
extern void (*cpu_timer_callback)(void *user_data);
extern void *cpu_timer_user_data;
extern int cpu_timer_priotity;

#define MAX_CPU_TIMER	16
extern void init_cpu_timer();
extern int register_cpu_timer(int priority, void (*callback)(void *user_data), void *user_data);
extern int unregister_cpu_timer(int timer_id);
extern int set_cpu_timer(int timer_id, uint64_t cpu_ticks);
extern uint64_t get_cpu_timer(int timer_id);
extern int get_cpu_timer_id(int position);
extern void trigger_cpu_timer_callback(int timer_id);
#endif /* CPU_TIMER */

#endif /* !SIMCORE_INC */
