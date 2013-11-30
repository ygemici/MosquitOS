#########################################################################################
# Multiboot header
#########################################################################################
.set ALIGN,    1 << 0					# align loaded modules on page boundaries
.set MEMINFO,  1 << 1					# provide memory map
.set FLAGS,    ALIGN | MEMINFO			# this is the multiboot 'flag' field
.set MAGIC,    0x1BADB002				# 'magic number' lets bootloader find the header
.set CHECKSUM, -(MAGIC + FLAGS)			# checksum required

#########################################################################################
# This function is responsible for taking control from the bootloader, re-locating the
# kernel to a more sane address, and then starting the kernel.
#########################################################################################
.section .multiboot
.long MAGIC
.long FLAGS
.long CHECKSUM

.section .entry

.globl _osentry
.type _osentry, @function
.set osentry, (_osentry - 0xC0000000)
.globl osentry

_osentry:
	# Install the GDT to push us to 0xC0000000 until we can set up paging proper
	lgdt 	(kern_gdt - 0xC0000000)
	mov 	$0x10, %cx
	mov 	%cx, %ds
	mov 	%cx, %es
	mov 	%cx, %fs
	mov 	%cx, %gs
	mov 	%cx, %ss
 
	# Enter protected mode
    mov		%cr0, %ecx
    or		$0x00000001, %cx
    mov		%ecx, %cr0

	# jump to the higher half kernel
	ljmp 	$0x08, $jmp_highhalf
 
# The CPU will now add 0x40000000 to all addresses, translating them.
jmp_highhalf:
	# Now, we're executing at 0xC0100000, so use the virtual address for stack.
	mov		$stack_top, %esp

	# Push multiboot info (EBX) and magic number (EAX)
    push 	%ebx
    push	%eax

	# Check for SSE, and if it exists, enable it
	mov		$0x1, %eax
	cpuid
	test	$(1 << 25), %edx
	jz 		.noSSE
	
	call	sse_init

.noSSE:
	movw	$0xF030, 0xC00B8000

	jmp		.

	# Initialise paging
	call	paging_init

	# Now jump into the kernel's main function
	call	kernel_main

	# In case the kernel returns, hang forever
	cli

.Lhang:
	hlt
	jmp .Lhang

/*
 * Enables the SSE features of the processor.
 */
sse_init:
	mov		%cr0, %eax

	# clear coprocessor emulation CR0.EM
	and		$0xFFFB, %ax

	# set coprocessor monitoring CR0.MP
	or		$0x2, %ax
	mov		%eax, %cr0
	mov		%cr4, %eax

	# set CR4.OSFXSR and CR4.OSXMMEXCPT at the same time
	or		$(3 << 9), %ax
	mov		%eax, %cr4
	ret

/*
 * Flush the GDT
 */
 .global flush_gdt
 flush_gdt:
 	push	%eax
	mov 	$0x10, %ax
	mov 	%ax, %ds
	mov 	%ax, %es
	mov 	%ax, %fs
	mov 	%ax, %gs
	mov 	%ax, %ss

	ljmp 	$0x08, $flush_gdt_2

flush_gdt_2:
	pop		%eax
	ret

.section .text
/*
 * Panic halt loop
 */
.globl panic_halt_loop
panic_halt_loop:
	hlt
	jmp		panic_halt_loop


# Reserve a stack of 16K
.section .bss
stack_bottom:
.skip 0x4000
stack_top:

# GDT
.section .entry
kern_gdt:
	.word gdt_end - gdt - 1 # size of the GDT
	.long (gdt - 0xC0000000) # linear address of GDT

gdt:
	.long 0, 0							# null gate
	.byte 0xFF, 0xFF, 0, 0, 0, 0x9A, 0xCF, 0x40	# code selector 0x08: base 0x40000000, limit 0xFFFFFFFF, type 0x9A, granularity 0xCF
	.byte 0xFF, 0xFF, 0, 0, 0, 0x92, 0xCF, 0x40	# data selector 0x10: base 0x40000000, limit 0xFFFFFFFF, type 0x92, granularity 0xCF
 
gdt_end:
