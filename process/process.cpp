#include "process.hpp"
#include <oslibc/string.h>
#include <hw/vga_stream.hpp>

cloudos::process::process() {
}

void cloudos::process::initialize(int p, void *start_addr, void *stack_addr) {
	pid = p;

	// initialize all registers and return state to zero
	memset(&state, 0, sizeof(state));

	// set ring 3 data, stack & code segments
	state.ds = state.ss = 0x23;
	state.cs = 0x1b;

	// stack location for new process
	state.useresp = reinterpret_cast<uint32_t>(stack_addr);

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
