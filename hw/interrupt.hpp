#pragma once

#include <stddef.h>
#include <stdint.h>
#include "interrupt_table.hpp"

namespace cloudos {

struct interrupt_state_t {
	// ds is the segment selector for the data, code, etc. registers; fs is
	// the selector for the fs and gs registers
	uint32_t ds, fs;
	uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
	uint32_t int_no, err_code;
	uint32_t eip, cs, eflags, useresp, ss;
};

struct interrupt_functor {
	interrupt_functor() {}
	virtual ~interrupt_functor() {}
	virtual void operator()(interrupt_state_t*) = 0;
};

struct interrupt_global {
	interrupt_global(interrupt_functor*);
	~interrupt_global();

	void reprogram_pic();
	void setup(interrupt_table&);
	void enable_interrupts();
	void disable_interrupts();

	void call(interrupt_state_t*);

private:
	interrupt_functor *functor;
};

}
