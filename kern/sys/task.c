#include <types.h>
#include "task.h"
#include "kheap.h"

// Needed to set up the task
static uint32_t next_pid;

// Used to speed up linked-list stuff
static i386_task_t* task_first;
static i386_task_t* task_last;

extern page_directory_t *kernel_directory;

// External assembly routines
void task_restore_context(i386_task_state_t*);

uint8_t task_temp_fxsave_region[512] __attribute__((aligned(16)));

/*
 * Saves the state of the task.
 */
void task_save_state(i386_task_t* task, void* regPtr) {
	// Back up the process' FPU/SSE state
	i386_task_state_t *state = task->task_state;

	// If FPU state memory area is not allocated, allocate it
	if(state->fpu_state == NULL) {
		state->fpu_state = (void *) kmalloc(512);
	}

	ASSERT(state->fpu_state != NULL);

	__asm__ volatile("fxsave %0; " : "=m" (task_temp_fxsave_region));

	// Copy FPU state into the thread context table
	memcpy(state->fpu_state, &task_temp_fxsave_region, 512);

	// Cast the register struct pointer to what it really is
	sched_trap_regs_t *regs = regPtr+offsetof(sched_trap_regs_t, gs);

	// Since starting at gs and ending with ss, the format of sched_trap_regs_t
	// and i386_task_state_t is identical, we can simply perform a memcpy of
	// 68 bytes (17 dwords) the relevant places in the struct.
	memcpy(state, regs, 0x44);
}

/*
 * Performs a context switch to the specified task.
 */
void task_switch(i386_task_t* task) {
	i386_task_state_t *state = task->task_state;

	// Get pointer to page tables
	state->page_table = (uint32_t) state->page_directory->tablesPhysical;

	uint32_t cr3;
	__asm__ volatile("mov %%cr3, %0" : "=r" (cr3));
	state->page_table = cr3;

	// Copy backed-up FPU state to memory
	memcpy(&task_temp_fxsave_region, state->fpu_state, 512);

	// Restore CPU state
	task_restore_context(state);
}

/*
 * Allocates a new task with the specified ELF to load the program's sections
 * from.
 */
i386_task_t* task_allocate(elf_file_t* binary) {
	// Try to get some memory for the task
	i386_task_t *task = (i386_task_t*) kmalloc(sizeof(i386_task_t));
	ASSERT(task != NULL);

	// Set up the task struct
	memclr(task, sizeof(i386_task_t));

	// If there is a previous task, set its next task to this
	if(task_last) {
		task_last->next = task;
	}

	// If there is no task currently, set it
	if(!task_first) {
		task_first = task;
	}

	// Update this task's previous ptr
	task->prev = task_last;
	task_last = task;

	task->pid = next_pid++;
	// Try to get memory for the state struct
	i386_task_state_t *state = (i386_task_state_t*) kmalloc(sizeof(i386_task_state_t));
	ASSERT(state != NULL);
	task->task_state = state;

	// Set up the state struct
	task->task_state->fpu_state = (void *) kmalloc(512);

	if(binary) {
		// Set up page table
		uint32_t directory_phys, directory_phys_phys;

		page_directory_t *directory = (page_directory_t *) kmalloc_p(sizeof(page_directory_t), &directory_phys);
		ASSERT(directory != NULL);
		memclr(directory, sizeof(page_directory_t));

		directory_phys_phys = directory_phys + (sizeof(page_table_t*) * 1024);

		kprintf("Process page table at 0x%X virt 0x%X (0x%X) phys\n", directory, directory_phys, directory_phys_phys);

		// Set up 0xC0000000 and above to point to the kernel page tables (copy 0x100)
		for(int i = 0x300; i < 0x400; i++) {
			directory->tables[i] = kernel_directory->tables[i];
			directory->tablesPhysical[i] = kernel_directory->tablesPhysical[i];
		}

		// Switch to the process' pagetable so we can allocate frames
		// __asm__ volatile("mov %0, %%cr3" : : "r"(directory_phys_phys));

		// Switch back to kernel pagetable
		paging_switch_directory(kernel_directory);

		// Store page table pointers
		state->page_directory = directory;
		state->page_table = directory_phys;
	} else { // The binary is NULL, so use kernel pagetables
		task->task_state->page_directory = kernel_directory;
		task->task_state->page_table = kernel_directory->physicalAddr;
	}

	// Notify scheduler so task is added to the queue
	sched_task_created(task);

	return task;
}

/*
 * Deallocates the task.
 */
void task_deallocate(i386_task_t* task) {
	i386_task_t *next = task->next;
	i386_task_t *prev = task->prev;

	// If there is an next task, update its previous pointer
	if(next) {
		if(prev) {
			next->prev = prev;
		} else {
			next->prev = NULL;
		}
	}

	// If there's a previous task, update its next pointer
	if(prev) {
		if(next) {
			prev->next = next;
		} else {
			prev->next = NULL;
		}
	}

	// Notify scheduler that this task is removed
	sched_task_deleted(task);

	// Clean up memory.
	kfree(task->task_state->fpu_state);
	kfree(task->task_state);
	kfree(task);
}

/*
 * Access to the linked list pointers
 */
i386_task_t* task_get_first() {
	return task_first;
}

i386_task_t* task_get_last() {
	return task_last;
}