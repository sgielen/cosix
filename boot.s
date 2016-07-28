# Source: http://wiki.osdev.org/Bare_Bones

.set ALIGN,    1<<0             # align loaded modules on page boundaries
.set MEMINFO,  1<<1             # provide memory map
.set FLAGS,    ALIGN | MEMINFO  # this is the Multiboot 'flag' field
.set MAGIC,    0x1BADB002       # 'magic number' lets bootloader find the header
.set CHECKSUM, -(MAGIC + FLAGS) # checksum of above, to prove we are multiboot

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

.set KERNEL_VIRTUAL_BASE, 0xc0000000
.set KERNEL_PAGE_NUMBER, (KERNEL_VIRTUAL_BASE >> 22)

.section .bootstrap_stack, "aw", @nobits
stack_bottom:
.skip 16384 # 16 KiB
.global stack_top
stack_top:

.section .data
.align 0x1000
_boot_page_directory:
	# identity map the first 4 MB
	.long 0x00000083
	# no pages until upper half
	.rept (KERNEL_PAGE_NUMBER - 1)
	.long 0
	.endr
	# identity map first 4 MB of upper half
	.long 0x00000083
	# no pages until end of memory
	.rept (1024 - KERNEL_PAGE_NUMBER - 1)
	.long 0
	.endr
.global _kernel_virtual_base
_kernel_virtual_base:
	.long KERNEL_VIRTUAL_BASE

.section .text
.align 4
.global _entrypoint
.type _entrypoint, @function
.set _entrypoint, (_start - KERNEL_VIRTUAL_BASE)

.global _start
.type _start, @function
_start:
	# when _entrypoint is called, this is the only code running in 0x10000
	# our job is to set up paging for the higher half, then call into
	# kernel_main in the higher half
	cli

	# set page directory
	mov $(_boot_page_directory - KERNEL_VIRTUAL_BASE), %ecx
	mov %ecx, %cr3

	# enable 4 mb pages
	mov %cr4, %ecx
	or $0x00000010, %ecx
	mov %ecx, %cr4

	# enable paging
	mov %cr0, %ecx
	or $0x80000000, %ecx
	mov %ecx, %cr0

	lea [higher_half], %ecx
	jmp *%ecx

higher_half:
	# Unmap the identity-mapped pages
	movl $0, _boot_page_directory

	movl $stack_top, %esp

	# We expect a Multiboot boot, where %eax contains the Multiboot magic
	# and %ebx a pointer to a struct with hardware information in physical
	# memory. These, along with the symbol pointing to the end of the
	# memory used for the kernel binary, are passed as parameters to the
	# kernel_main function.
	add $KERNEL_VIRTUAL_BASE, %ebx
	push $__end_of_binary
	push %ebx
	push %eax
	call kernel_main

	hlt
.Lhang:
	jmp .Lhang

.size _start, . - _start
