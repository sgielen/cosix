#pragma once

#include "hw/interrupt.hpp"

namespace cloudos {

struct vga_stream;

class allocator;

struct process {
	process();

	void initialize(int pid, void *start_addr, allocator *alloc);
	void set_return_state(interrupt_state_t*);
	void get_return_state(interrupt_state_t*);
	void handle_syscall(vga_stream &stream);
	void *get_kernel_stack_top();

private:
	int pid;
	interrupt_state_t state;
	void *userland_stack_bottom;
	size_t userland_stack_size;
	void *kernel_stack_bottom;
	size_t kernel_stack_size;
};


};
