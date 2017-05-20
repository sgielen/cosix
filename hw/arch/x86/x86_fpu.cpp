#include "x86_fpu.hpp"
#include <oslibc/assert.hpp>
#include <global.hpp>

using namespace cloudos;

x86_fpu::x86_fpu(device *parent) : device(parent), irq_handler() {
}

const char *x86_fpu::description() {
	return "x86 FPU";
}

cloudabi_errno_t x86_fpu::init() {
	register_irq(13);
	return 0;
}

void x86_fpu::handle_irq(uint8_t irq) {
	/* ignore it */
}
