#include <stddef.h>
#include <stdint.h>
#include "cloudos_version.h"

void kernel_main(uint32_t, void*, void*);
extern char __end_of_binary;
extern char stack_top;

// TODO: this takes 12 kb of physical kernel space, while it's only used when the kernel starts
// instead, we could allocate this somewhere in the memory map, since the kernel will allocate
// and install a new directory and new tables as soon as it starts, so it's no problem if this
// gets overwritten later
uint32_t initial_paging_directory[1024] __attribute__((aligned(4096)));
uint32_t initial_paging_table_lower[1024] __attribute__((aligned(4096)));
uint32_t initial_paging_table_upper[1024] __attribute__((aligned(4096)));

/* Display a 'boot failed' message. second_line must be max 80 characters to fit
 * in a single line. */
__attribute__((noreturn)) void kernel_boot_failed(const char *second_line) {
	const char *first_line = "CloudOS v" cloudos_VERSION " -- BOOT FAILED";

	uint16_t *vga_buffer = (uint16_t*)0xb8000;
	const int width = 80;
	const int height = 24;

	// first, clear the screen
	for (size_t y = 0; y < height; ++y) {
		for (size_t x = 0; x < width; ++x) {
			vga_buffer[y * width + x] = (' ' | 15 << 8);
		}
	}

	// write the first line
	for (size_t x = 0; x < width && first_line[x]; ++x) {
		vga_buffer[x] = (first_line[x] | 15 << 8);
	}

	// write the second line
	for (size_t x = 0; x < width && second_line[x]; ++x) {
		vga_buffer[width + x] = (second_line[x] | 15 << 8);
	}

	asm volatile("cli; halted: hlt; jmp halted;");
	while(1) {}
}

__attribute__((noreturn)) void kernel_start(uint32_t multiboot_magic, void *bi_ptr) {
	/* this function is running in lower half, its objective is to map the
	 * kernel to upper half (0xc0000000) and then call kernel_main as soon
	 * as possible */

	// sanity check: our full binary must be in the first 4 mb, as well as
	// our current stack and instruction pointers
	if((uint32_t)kernel_start >= 0x400000) {
		kernel_boot_failed("Kernel not booted in first 4 mb of memory");
	}
	int esp;
	asm volatile("movl %%esp, %0" : "=r"(esp));
	if(esp >= 0x400000) {
		kernel_boot_failed("Kernel stack not in first 4 mb of memory");
	}
	if((uint32_t)&__end_of_binary >= 0x400000) {
		kernel_boot_failed("Kernel does not fit in first 4 mb of memory");
	}
	
	size_t i;
	for(i = 0; i < 1024; ++i) {
		initial_paging_directory[i] = 0; // non-present page
		initial_paging_table_lower[i] = (i * 0x1000) | 0x3; // read-write kernel-only present page
		initial_paging_table_upper[i] =  ((0*1024 + i) * 0x1000) | 0x3;
	}

	// identity map the first 4 mb of virtual memory, where we are running now
	initial_paging_directory[0] = (uint32_t)initial_paging_table_lower | 0x03; // read-write present table
	// map 4 mb of upper half memory as well
	initial_paging_directory[0x300] = (uint32_t)initial_paging_table_upper | 0x03;

	// Set the paging directory in cr3
	asm volatile("mov %0, %%cr3" : : "r"(initial_paging_directory) : "memory");

	// Turn on paging in cr0
	int cr0;
	asm volatile("mov %%cr0, %0" : "=r"(cr0));
	cr0 |= 0x80000000;
	asm volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");

	// Set our stack to upper memory and call kernel_main
	asm volatile(
		"movl %[stack_top], %%esp; "
		"push %[endptr]; "
		"push %[biptr]; "
		"push %[magic]; "
		"call *%[krnlmain]" : :
			[stack_top]"r"(&stack_top + 0xc0000000),
			[endptr]"r"(&__end_of_binary),
			[biptr]"r"(bi_ptr),
			[magic]"r"(multiboot_magic),
			[krnlmain]"d"(kernel_main + 0xc0000000));

	kernel_boot_failed("kernel_main returned");
}

