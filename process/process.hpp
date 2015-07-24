#pragma once

#include "hw/interrupt.hpp"

namespace cloudos {

struct vga_stream;

struct process {
	process();

	void initialize(int pid, void *start_addr, void *stack_addr);
	void set_return_state(interrupt_state_t*);
	void get_return_state(interrupt_state_t*);
	void handle_syscall(vga_stream &stream);

private:
	int pid;
	interrupt_state_t state;
};


};
