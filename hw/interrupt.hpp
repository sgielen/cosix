#pragma once

#include <stddef.h>
#include <stdint.h>
#include "interrupt_table.hpp"

namespace cloudos {

struct interrupt_functor {
	interrupt_functor() {}
	virtual ~interrupt_functor() {}
	virtual void operator()(uint32_t int_no, uint32_t err_code) = 0;
};

struct interrupt_global {
	interrupt_global(interrupt_functor*);
	~interrupt_global();

	void reprogram_pic();
	void setup(interrupt_table&);
	void enable_interrupts();
	void disable_interrupts();

	void call(uint32_t int_no, uint32_t err_code);

private:
	interrupt_functor *functor;
};

}
