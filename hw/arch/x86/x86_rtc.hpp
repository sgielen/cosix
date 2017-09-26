#pragma once

#include <concur/condition.hpp>
#include <hw/device.hpp>
#include <hw/driver.hpp>
#include <hw/interrupt.hpp>
#include <oslibc/list.hpp>
#include <time/clock_store.hpp>

#include <stdint.h>

namespace cloudos {

struct x86_rtc_clock : public clock {
	x86_rtc_clock();

	cloudabi_timestamp_t get_resolution() override;
	cloudabi_timestamp_t get_time(cloudabi_timestamp_t precision) override;

	thread_condition_signaler *get_signaler(
		cloudabi_timestamp_t timeout, cloudabi_timestamp_t precision) override;

	void set_current_utc_time(cloudabi_timestamp_t);

private:
	clock *get_monotonic();
	cloudabi_timestamp_t offset_monotonic_to_utc = 0;
};

/**
 * This device reads and writes an x86 RTC. It does not use the RTC for actual
 * timing; there, it uses the x86 PIT.
 */
struct x86_rtc : public device, public irq_handler {
	x86_rtc(device *parent);

	const char *description() override;
	cloudabi_errno_t init() override;

	void handle_irq(uint8_t irq) override;

private:
	x86_rtc_clock clock;
};


}
