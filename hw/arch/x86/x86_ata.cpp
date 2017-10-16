#include "x86_ata.hpp"
#include <oslibc/assert.hpp>
#include <global.hpp>

using namespace cloudos;

x86_ata::x86_ata(device *parent) : device(parent), irq_handler() {
}

const char *x86_ata::description() {
	return "x86 ATA controllers";
}

cloudabi_errno_t x86_ata::init() {
	register_irq(14);
	register_irq(15);
	return 0;
}

void x86_ata::handle_irq(uint8_t) {
	/* ignore it */
}
