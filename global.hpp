#pragma once

#include "hw/vga_stream.hpp"

namespace cloudos {

struct global_state;
class allocator;
struct segment_table;

extern global_state *global_state_;

struct global_state {
	global_state();

	cloudos::allocator *alloc;
	cloudos::segment_table *gdt; /* for TSS access */
	cloudos::vga_stream *vga;
};

__attribute__((noreturn)) inline void kernel_panic(const char *message) {
	if(global_state_) {
		*(global_state_->vga) << "!!! KERNEL PANIC - HALTING !!!\n" << message << "\n\n\n";
	}
	asm volatile("cli; halted: hlt; jmp halted;");
	while(1) {}
}

}
