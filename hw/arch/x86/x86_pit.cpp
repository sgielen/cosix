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
	UNUSED(irq);

	get_root_device()->timer_event_recursive();
	clock.tick();
	if(!get_scheduler()->is_waiting_for_ready_task()) {
		// this timer event occurred while already waiting for something
		// to do, so just return immediately to prevent stack overflow
		get_scheduler()->thread_yield();
	}
}

x86_pit_clock::x86_pit_clock() {
	get_clock_store()->register_clock(CLOUDABI_CLOCK_MONOTONIC, this);
	// TODO: while we don't have a realtime clock, the monotonic clock will act like one:
	get_clock_store()->register_clock(CLOUDABI_CLOCK_REALTIME, this);
}

cloudabi_timestamp_t x86_pit_clock::get_resolution() {
	// The base tick frequency is 14.31818 MHz
	// The default divider is 12, so that's 1.1932 MHz
	// The soft divider frequency is 65536, so 18.207 Hz
	// Then, the resolution is 1 / 18.207 = 54925417 ns
	return 54925417;
}

cloudabi_timestamp_t x86_pit_clock::get_time(cloudabi_timestamp_t /*precision*/) {
	return time;
}

void x86_pit_clock::tick() {
	time += get_resolution();

	while(signalers) {
		assert(signalers->next == nullptr || signalers->next->data->timeout >= signalers->data->timeout);
		if(signalers->data->timeout > time) {
			break;
		}

		auto *item = signalers;
		signalers = item->next;

		item->data->signaler.condition_broadcast();

		deallocate(item->data);
		deallocate(item);
	}
}

thread_condition_signaler *x86_pit_clock::get_signaler(cloudabi_timestamp_t timeout,
	cloudabi_timestamp_t precision)
{
	assert(timeout > time);

	// TODO: find existing signalers for this time range, then
	// coalesce this request into one
	x86_pit_clock_signaler *sig = allocate<x86_pit_clock_signaler>();
	x86_pit_clock_signaler_list *item = allocate<x86_pit_clock_signaler_list>(sig);

	if(!sig || !item) {
		kernel_panic("Failed to allocate clock signaler / clock signaler list");
	}

	sig->timeout = timeout;
	sig->precision = precision;

	// ordered insert
	if(signalers == nullptr || signalers->data->timeout >= timeout) {
		item->next = signalers;
		signalers = item;
		return &(sig->signaler);
	}

	for(auto *s = signalers; s != nullptr; s = s->next) {
		assert(s->data->timeout < timeout);
		if(s->next == nullptr || s->next->data->timeout >= timeout) {
			// insert it in between
			item->next = s->next;
			s->next = item;
			return &(sig->signaler);
		}
	}

	kernel_panic("unreachable");
}
