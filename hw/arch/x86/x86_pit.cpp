#include "x86_pit.hpp"
#include <oslibc/assert.hpp>
#include <global.hpp>
#include <fd/scheduler.hpp>

using namespace cloudos;

x86_pit::x86_pit(device *parent) : device(parent), irq_handler() {
}

const char *x86_pit::description() {
	return "x86 timer";
}

cloudabi_errno_t x86_pit::init() {
	register_irq(0);
	return 0;
}

void x86_pit::handle_irq(uint8_t irq) {
	assert(irq == 0);

	get_root_device()->timer_event_recursive();
	if(!get_scheduler()->is_waiting_for_ready_task()) {
		// this timer event occurred while already waiting for something
		// to do, so just return immediately to prevent stack overflow
		get_scheduler()->thread_yield();
	}
}
