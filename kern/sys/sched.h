#ifndef SCHED_H
#define SCHED_H

#include <types.h>
#include "task.h"

// software interrupt to trap into scheduler
#define SCHED_TRAP_NUM 0x88

// PIT interval for a timeslice
#define SCHED_TIMESLICE 1194
#define SCHED_TIMESLICE_MS SCHED_TIMESLICE / (3579545 / 3) * 1000

// Maximum times a process can get run in one scheduling cycle
#define SCHED_MAX_EXEC_PER_CYCLE 8

typedef struct sched_info {
	// The last "scheduling cycle" this process was ran.
	uint64_t last_cycle;
	// How many times this process had the CPU
	uint32_t num_cpu_per_cycle;

	// Pointer to the task descriptor
	void *task_descriptor;
} sched_task_t;

// Initialises scheduler
void sched_init();
// Called when a process yields control to the scheduler
void sched_yield(sched_trap_regs_t);
// Called when a task is destroyed
void sched_task_deleted(void*);
// Called when a task is created
void sched_task_created(void*);
// Returns the currently executing task.
void* sched_curr_task();
// Initialises multitasking
void multitasking_init();

#endif