#pragma once

#include <hw/driver.hpp>
#include <hw/device.hpp>
#include <hw/interrupt.hpp>
#include <oslibc/list.hpp>
#include <concur/condition.hpp>
#include <time/clock_store.hpp>
#include <stdint.h>

namespace cloudos {

struct x86_pit_clock_signaler {
	cloudabi_timestamp_t timeout;
	cloudabi_timestamp_t precision;
	thread_condition_signaler signaler;
};

typedef linked_list<x86_pit_clock_signaler*> x86_pit_clock_signaler_list;

struct x86_pit_clock : public clock {
	x86_pit_clock();

	cloudabi_timestamp_t get_resolution() override;
	cloudabi_timestamp_t get_time(cloudabi_timestamp_t precision) override;
	thread_condition_signaler *get_signaler(cloudabi_timestamp_t timeout,
		cloudabi_timestamp_t precision) override;

	void tick();

private:
	cloudabi_timestamp_t time = 0;
	x86_pit_clock_signaler_list *signalers = nullptr;
};

/**
 * This device represents a standard x86 PIT. It handles IRQ 0.
 */
struct x86_pit : public device, public irq_handler {
	x86_pit(device *parent);

	const char *description() override;
	cloudabi_errno_t init() override;

	void handle_irq(uint8_t irq) override;

private:
	x86_pit_clock clock;
};

}
