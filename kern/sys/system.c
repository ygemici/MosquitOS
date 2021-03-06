#include <types.h>
#include <io/io.h>
#include <device/pic.h>
#include <device/pit.h>
#include <device/rs232.h>
#include <device/apic.h>	

#include "irq.h"
#include "system.h"
#include "sched.h"
#include "syscall.h"
#include "sys/multiboot.h"
#include "runtime/hashmap.h"
#include "task.h"
#include "binfmt_elf.h"
#include "vga/svga.h"

// This CANNOT be static as we need to access it elsewhere
multiboot_info_t* sys_multiboot_info;

static hashmap_t* sys_kern_arguments;
static uint64_t sys_timer_ticks;
static uint64_t sys_tsc_boot_ticks;
static cpu_info_t* sys_current_cpu_info;

// Some info from the ELF
char *kern_elf_strtab;
elf_symbol_entry_t *kern_elf_symtab;
unsigned int kern_elf_symtab_entries;

// Allocate memory in the BSS for IDT, GDT, and TSSes
static idt_entry_t sys_idt[256];
static gdt_entry_t sys_gdt[16];
static i386_thread_state_t sys_tss[SYS_NUM_TSS];

// Sets location of GDT
void sys_install_gdt(void* location);
void flush_gdt(void);
void sys_copy_multiboot();
void sys_set_idt(void* base, uint16_t size);

extern void rs232_set_up_irq();

// Define the assembly IRQ handlers
extern void sys_dummy_irq(void);
extern void sys_page_fault_irq(void);

// Exception handlers
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);

void sys_timer_tick_handler(void* context);

/*
 * Initialises the system into a known state.
 */ 
void system_init() {
	// Set up IDT
	sys_setup_ints();

	// Read TSC
	sys_tsc_boot_ticks = sys_rdtsc();

	// Set up the TSS and their stacks
	sys_init_tss();

	// Set up syscalls
	syscall_init();

	// Set up scheduler
	sched_init();

	// Set up multitasking
	multitasking_init();
}

/*
 * Copies the multiboot structure out of lowmem.
 *
 * This is run BEFORE the kernel heap is properly set up, so we can't free the
 * memory we allocate here.
 */
void sys_copy_multiboot() {
	multiboot_info_t* lowmemStruct = sys_multiboot_info;
	multiboot_info_t* himemStruct = (multiboot_info_t *) kmalloc(sizeof(multiboot_info_t));

	memcpy(himemStruct, lowmemStruct, sizeof(multiboot_info_t));

	sys_multiboot_info = himemStruct;

	// Command line
	if(MULTIBOOT_CHECK_FLAG(lowmemStruct->flags, 2)) {
		size_t length = strlen((char *) lowmemStruct->cmdline);
		char *cmdline = (char *) kmalloc(length+2);
		memcpy(cmdline, (void *) lowmemStruct->cmdline, length);
		himemStruct->cmdline = (uint32_t) cmdline;

		// Allocate kernel command line hashmap
		sys_kern_arguments = hashmap_allocate();

		// Parse the command line
		char *cmdline_tmp = (char *) kmalloc(length+2);
		memcpy(cmdline_tmp, cmdline, length);

		char *pch = strtok(cmdline, " ");

		// Loop through all entries
		while(pch) {
			pch = strtok(NULL, " ");

			// This is not a key/value string
			if(strchr(pch, '=') == NULL) {
				hashmap_insert(sys_kern_arguments, pch, NULL);
			} else {
				char* value = strchr(pch, '=');
				value[0] = 0x00; // terminate key string
				value++;
				hashmap_insert(sys_kern_arguments, pch, value);
			}
		}
	}

	// Copy ELF headers
	if (MULTIBOOT_CHECK_FLAG(lowmemStruct->flags, 5)) {
		multiboot_elf_section_header_table_t *multiboot_elf_sec = &(lowmemStruct->u.elf_sec);
		memcpy(&himemStruct->u.elf_sec, multiboot_elf_sec, sizeof(multiboot_elf_section_header_table_t));

		// Physical address of the ELF headers
		elf_section_entry_t *entries = (elf_section_entry_t *) multiboot_elf_sec->addr;

		// Get string table
		elf_section_entry_t *strtab = &entries[multiboot_elf_sec->shndx];
		kern_elf_strtab = (char *) strtab->sh_addr;

		// Iterate through all the tables
		for(int i = 0; i < multiboot_elf_sec->num; i++) {
			elf_section_entry_t *ent = &entries[i];

			// Ignore NULL entries
			if(ent->sh_type != SHT_NULL) {
				char* name = kern_elf_strtab+ent->sh_name;

				// Did we find the symbol table?
				if(ent->sh_type == SHT_SYMTAB) {
					kern_elf_symtab = (elf_symbol_entry_t *) kmalloc(ent->sh_size);
					memcpy(kern_elf_symtab, (void *) ent->sh_addr, ent->sh_size);

					kern_elf_symtab_entries = ent->sh_size / sizeof(elf_symbol_entry_t);

					kprintf("Symbol table copied to 0x%X, link 0x%X\n", kern_elf_symtab, ent->sh_link);
				} else if(ent->sh_type == SHT_STRTAB) { // If we found the string table, copy to highmem
					kern_elf_strtab = (char *) kmalloc(ent->sh_size);
					memcpy(kern_elf_strtab, (void *) ent->sh_addr, ent->sh_size);

					kprintf("String table copied to 0x%X, size 0x%X\n", kern_elf_strtab, ent->sh_size);
				} else { // other kind of section
					kprintf("Section %s, addr 0x%x, sz 0x%X, type 0x%X, idx 0x%X\n", name, ent->sh_addr, ent->sh_size, ent->sh_type, i);
				}
			}
		}

		// Stuff
		elf_symbol_entry_t *symbol = &kern_elf_symtab[535];
		kprintf("Addr 0x%X: \"%s\" (%u bytes, name 0x%X)\n", symbol->st_address, kern_elf_strtab+symbol->st_name, symbol->st_size, symbol->st_name);
	}

	// Copy memory map
	if (MULTIBOOT_CHECK_FLAG(lowmemStruct->flags, 6)) {
		multiboot_memory_map_t *mmap = (multiboot_memory_map_t *) lowmemStruct->mmap_addr;
		multiboot_memory_map_t *new = (multiboot_memory_map_t *) kmalloc(lowmemStruct->mmap_length);
		memcpy(new, mmap, lowmemStruct->mmap_length);
		himemStruct->mmap_addr = (uint32_t) new;
	}

	// If bootloader sets up video, copy video info
	if (MULTIBOOT_CHECK_FLAG(lowmemStruct->flags, 11)) {
		// Copy mode info
		svga_mode_info_t *vbe_info = (svga_mode_info_t *) lowmemStruct->vbe_mode_info;
		svga_mode_info_t *new_vbe_info = (svga_mode_info_t *) kmalloc(sizeof(svga_mode_info_t));
		memcpy(new_vbe_info, vbe_info, sizeof(svga_mode_info_t));
		himemStruct->vbe_mode_info = (uint32_t) new_vbe_info;

		// Copy control info
		void *vbe_control = (void *) lowmemStruct->vbe_control_info;
		void *new_vbe_control = (void *) kmalloc(512);
		memcpy(new_vbe_control, vbe_control, 512);
		himemStruct->vbe_control_info = (uint32_t) new_vbe_control;
	}
}

/*
 * Builds the Interrupt Descriptor Table at a fixed location in memory.
 */
void sys_build_idt() {
	idt_entry_t* idt = (idt_entry_t *) &sys_idt;

	// Clear IDT
	memclr(idt, sizeof(idt_entry_t)*256);

	for(int i = 0; i < 256; i++) {
		sys_set_idt_gate(i, (uint32_t) sys_dummy_irq, 0x08, 0x8E);
	}

	// Install exception handlers (BSOD)
	sys_set_idt_gate(0, (uint32_t) isr0, 0x08, 0x8E);
	sys_set_idt_gate(1, (uint32_t) isr1, 0x08, 0x8E);
	sys_set_idt_gate(2, (uint32_t) isr2, 0x08, 0x8E);
	sys_set_idt_gate(3, (uint32_t) isr3, 0x08, 0x8E);
	sys_set_idt_gate(4, (uint32_t) isr4, 0x08, 0x8E);
	sys_set_idt_gate(5, (uint32_t) isr5, 0x08, 0x8E);
	sys_set_idt_gate(6, (uint32_t) isr6, 0x08, 0x8E);
	sys_set_idt_gate(7, (uint32_t) isr7, 0x08, 0x8E);
	sys_set_idt_gate(8, (uint32_t) isr8, 0x08, 0x8E);
	sys_set_idt_gate(9, (uint32_t) isr9, 0x08, 0x8E);
	sys_set_idt_gate(10, (uint32_t) isr10, 0x08, 0x8E);
	sys_set_idt_gate(11, (uint32_t) isr11, 0x08, 0x8E);
	sys_set_idt_gate(12, (uint32_t) isr12, 0x08, 0x8E);
	sys_set_idt_gate(13, (uint32_t) isr13, 0x08, 0x8E);
	sys_set_idt_gate(15, (uint32_t) isr15, 0x08, 0x8E);
	sys_set_idt_gate(16, (uint32_t) isr16, 0x08, 0x8E);
	sys_set_idt_gate(17, (uint32_t) isr17, 0x08, 0x8E);
	sys_set_idt_gate(18, (uint32_t) isr18, 0x08, 0x8E);

	// Page fault handler
	sys_set_idt_gate(14, (uint32_t) isr14, 0x08, 0x8E);

	// Install IDT (LIDT instruction)
	sys_set_idt((void *) idt, sizeof(idt_entry_t)*256);
}

void sys_setup_ints() {
	// Remap PICs
	sys_pic_irq_remap(0x20, 0x28);

	// Initialise PIT ch0 to hardware int
	sys_pit_init(0, 0);
	// time in ms = reload_value / (3579545 / 3) * 1000
	sys_pit_set_reload(0, SCHED_TIMESLICE);

	sys_timer_ticks = 0;

	// Initialise IRQ registration system
	irq_init();

	// Register timer IRQ
	irq_register(0, sys_timer_tick_handler, NULL);

	// Re-enable interrupts now
	__asm__("sti");
}

void sys_set_idt_gate(uint8_t entry, uint32_t function, uint8_t segment, uint8_t flags) {
	idt_entry_t *ptr = (idt_entry_t *) &sys_idt;

	ptr[entry].offset_1 = function & 0xFFFF;
	ptr[entry].offset_2 = (function >> 0x10) & 0xFFFF;
	ptr[entry].selector = segment;
	ptr[entry].flags = flags; // OR with 0x60 for user level
	ptr[entry].zero = 0x00;
}

/*
 * Builds the Global Descriptor Table with the proper code/data segments, and
 * space for some task state segment descriptors.
 */
void sys_build_gdt() {
	gdt_entry_t *gdt = (gdt_entry_t *) &sys_gdt;

	// Set up null entry
	memset(gdt, 0x00, sizeof(gdt_entry_t));

	// Kernel code segment and data segments
	sys_set_gdt_gate((SYS_KERN_CODE_SEG >> 3), 0x00000000, 0xFFFFFFFF, 0x9A, 0xCF);
	sys_set_gdt_gate((SYS_KERN_DATA_SEG >> 3), 0x00000000, 0xFFFFFFFF, 0x92, 0xCF);

	// User code and data segments
	sys_set_gdt_gate((SYS_USER_CODE_SEG >> 3), 0x00000000, 0xFFFFFFFF, 0xFA, 0xCF);
	sys_set_gdt_gate((SYS_USER_DATA_SEG >> 3), 0x00000000, 0xFFFFFFFF, 0xF2, 0xCF);

	// Create the correct number of TSS descriptors
	for(int i = 0; i < SYS_NUM_TSS; i++) {
		sys_set_gdt_gate(i+5, ((uint32_t) &sys_tss)+(i*sizeof(i386_thread_state_t)), sizeof(i386_thread_state_t), 0x89, 0x4F);
	}

	sys_install_gdt(gdt);
}

/*
 * Initialises the TSS structs: this should be called with paging enabled to
 * allow the use of the kernel heap at 0xC0000000 or whereever.
 */
void sys_init_tss() {
	// Create the correct number of TSS descriptors
	for(int i = 0; i < SYS_NUM_TSS; i++) {
		// Initialise the TSS and zero it
		i386_thread_state_t *tss = &sys_tss[i];
		memclr(tss, sizeof(i386_thread_state_t));

		// Set up kernel stack meepen
		tss->iomap = (uint16_t) sizeof(i386_thread_state_t);
		tss->ss0 = SYS_KERN_DATA_SEG;
	
		// Allocate a kernel stack
		uint32_t* stack = (uint32_t *) kmalloc(SYS_KERN_STACK_SIZE);

		// Stack grows downwards
		tss->esp0 = ((uint32_t) stack) + SYS_KERN_STACK_SIZE;
	}
}

void sys_set_gdt_gate(uint16_t num, uint32_t base, uint32_t limit, uint8_t flags, uint8_t gran) {
	gdt_entry_t *gdt = (gdt_entry_t *) &sys_gdt;
	
	gdt[num].base_low = (base & 0xFFFF);
	gdt[num].base_middle = (base >> 16) & 0xFF;
	gdt[num].base_high = (base >> 24) & 0xFF;

	gdt[num].limit_low = (limit & 0xFFFF);
	gdt[num].granularity = (limit >> 16) & 0x0F;

	gdt[num].granularity |= gran & 0xF0;
	gdt[num].access = flags;
}

/*
 * Installs the GDT.
 */
void sys_install_gdt(void* location) {
	struct {
		uint16_t length;
		uint32_t base;
	} __attribute__((__packed__)) IDTR;
 
	IDTR.length = (0x18 + (SYS_NUM_TSS * 0x08)) - 1;
	IDTR.base = (uint32_t) location;
	__asm__ volatile("lgdt (%0)" : : "p"(&IDTR));
	
	flush_gdt();
}

/*
 * Loads the IDTR register.
 */
void sys_set_idt(void* base, uint16_t size) {
	struct {
		uint16_t length;
		uint32_t base;
	} __attribute__((__packed__)) IDTR;
 
	IDTR.length = size;
	IDTR.base = (uint32_t) base;
	__asm__ volatile("lidt (%0)": : "p"(&IDTR));
}

/*
 * Returns information about the current CPU that has been previously read,
 * or performs a read of said info if not available in memory.
 */
cpu_info_t* sys_get_cpu_info() {
	// Detect CPU if not cached
	if(!sys_current_cpu_info) {
		sys_current_cpu_info = cpuid_detect();
	}

	cpuid_set_strings(sys_current_cpu_info);

	return sys_current_cpu_info;
}

/*
 * Returns the number of system timer ticks since the kernel was started.
 */
uint64_t sys_get_ticks() {
	return sys_timer_ticks;
}

/*
 * Checks if interrupts are enabled.
 */
bool sys_irq_enabled() {
	int f;
	__asm__ volatile("pushf\n\t" "popl %0" : "=g"(f));
	return f & (1 << 9);
}

/*
 * Reads TSC (CPU timestamp counter)
 */
uint64_t sys_rdtsc() {
	uint32_t lo, hi;
	// RDTSC copies contents of 64-bit TSC into EDX:EAX
	__asm__ volatile("rdtsc" : "=a" (lo), "=d" (hi));

	return (lo & 0xFFFFFFFF) | (hi << 0x20);
}

/*
 * Flushes the MMU TLB for a given logical address.
 */
void sys_flush_tlb(uint32_t m) {
	__asm__ volatile("invlpg %0" : : "m"(m) : "memory");
}

/*
 * Reads an MSR register.
 */
void sys_read_MSR(uint32_t msr, uint32_t *lo, uint32_t *hi) {
	__asm__ volatile("rdmsr" : "=a"(*lo), "=d"(*hi) : "c"(msr));
}

/*
 * Writes an MSR register.
 */ 
void sys_write_MSR(uint32_t msr, uint32_t lo, uint32_t hi) {
	__asm__ volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

/*
 * IRQ handler for the system tick.
 */
void sys_timer_tick_handler(void* context) {
	sys_timer_ticks++;
}

/*
 * Flush CPU caches.
 */
void sys_flush_cpu_caches(void) {
	__asm__ volatile("wbinvd");
}

/*
 * Page fault handler
 */
void sys_page_fault_handler(uint32_t address) {

}