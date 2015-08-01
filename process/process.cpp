#include "process.hpp"
#include <oslibc/string.h>
#include <hw/vga_stream.hpp>
#include <memory/allocator.hpp>

cloudos::process::process() {
}

void cloudos::process::initialize(int p, void *start_addr, cloudos::allocator *alloc) {
	pid = p;
	userland_stack_size = kernel_stack_size = 0x10000 /* 64 kb */;
	userland_stack_bottom = reinterpret_cast<uint8_t*>(alloc->allocate(userland_stack_size));
	kernel_stack_bottom   = reinterpret_cast<uint8_t*>(alloc->allocate(kernel_stack_size));

	// initialize all registers and return state to zero
	memset(&state, 0, sizeof(state));

	// set ring 3 data, stack & code segments
	state.ds = state.ss = 0x23;
	state.cs = 0x1b;

	// stack location for new process
	state.useresp = reinterpret_cast<uint32_t>(userland_stack_bottom) + userland_stack_size;

	// initial instruction pointer
	state.eip = reinterpret_cast<uint32_t>(start_addr);

	// allow interrupts
	const int INTERRUPT_ENABLE = 1 << 9;
	state.eflags = INTERRUPT_ENABLE;
}

void cloudos::process::set_return_state(interrupt_state_t *new_state) {
	state = *new_state;
}

void cloudos::process::get_return_state(interrupt_state_t *return_state) {
	*return_state = state;
}

void cloudos::process::handle_syscall(vga_stream &stream) {
	// software interrupt
	int syscall = state.eax;
	if(syscall == 1) {
		// getpid
		state.eax = pid;
	} else if(syscall == 2) {
		// putstring
		const char *str = reinterpret_cast<const char*>(state.ecx);
		const size_t size = state.edx;
		state.edx = 0;
		for(size_t i = 0; i < size; ++i) {
			stream << str[i];
			state.edx += 1;
		}
	} else {
		stream << "Syscall " << state.eax << " unknown\n";
	}
}

void *cloudos::process::get_kernel_stack_top() {
	return reinterpret_cast<char*>(kernel_stack_bottom) + kernel_stack_size;
}
