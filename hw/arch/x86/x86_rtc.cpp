#include "x86_rtc.hpp"
#include <oslibc/assert.hpp>
#include <global.hpp>
#include <hw/cpu_io.hpp>

using namespace cloudos;

static uint8_t cmos_read(bool nmi_disable, uint8_t register_num) {
	outb(0x70, ((nmi_disable?1:0) << 7) | register_num);
	// TODO: 'reasonable delay' to allow CMOS to switch
	return inb(0x71);
}

static bool rtc_is_updating() {
	uint8_t statusA = cmos_read(false, 0x0a);
	return (statusA & 0x40) != 0;
}

static uint8_t convert_bcd(bool isbin, uint8_t value) {
	return isbin
		? isbin
		: ((value / 16) * 10 + (value & 0xf));
}

static bool is_leap_year(uint16_t year) {
	if((year % 4) != 0) return false;
	if((year % 100) != 0) return true;
	if((year % 400) != 0) return false;
	return true;
}

static cloudabi_timestamp_t read_utc_from_rtc_immediate() {
	uint8_t statusB = cmos_read(false, 0x0b);
	bool is12hr = (statusB & 0x02) == 0;
	bool isbin  = (statusB & 0x04) != 0;

	auto hours = cmos_read(false, 0x04);
	bool ispm = (hours & 0x80) != 0;
	hours = convert_bcd(isbin, hours & 0x7f);
	if(ispm && is12hr) {
		hours += 12;
	} else if(!ispm && hours == 12) {
		hours = 0;
	}

	auto seconds = convert_bcd(isbin, cmos_read(false, 0x00));
	auto minutes = convert_bcd(isbin, cmos_read(false, 0x02));
	auto dom     = convert_bcd(isbin, cmos_read(false, 0x07));
	auto mon     = convert_bcd(isbin, cmos_read(false, 0x08));
	uint16_t yr  = convert_bcd(isbin, cmos_read(false, 0x09));

	// TODO: we assume century is 20, this will be untrue starting at the year 2100
	yr += 2000;

	// sanity check
	if(yr < 1970) {
		yr = 1970;
	}

	// Convert to UTC time since epoch (1 January 1970 01:00:00)
	cloudabi_timestamp_t days_since_epoch = 0;
	for(uint16_t y = 1970; y < yr; ++y) {
		days_since_epoch += is_leap_year(y) ? 366 : 365;
	}
	for(uint16_t m = 1; m < mon; ++m) {
		if(m == 1 || m == 3 || m == 5 || m == 7 || m == 8 || m == 10 || m == 12) {
			days_since_epoch += 31;
		} else if(m == 2) {
			days_since_epoch += is_leap_year(yr) ? 29 : 28;
		} else {
			days_since_epoch += 30;
		}
	}
	days_since_epoch += dom - 1;

	cloudabi_timestamp_t seconds_since_epoch = 0;
	seconds_since_epoch += days_since_epoch * 24 * 3600;
	seconds_since_epoch += hours * 3600;
	seconds_since_epoch += minutes * 60;
	seconds_since_epoch += seconds;

	return seconds_since_epoch * 1000000000;
}

static cloudabi_timestamp_t read_utc_from_rtc() {
	// if the RTC is currently updating, wait until it's done
	while(rtc_is_updating()) {}

	// retrieve current time
	auto stamp_base = read_utc_from_rtc_immediate();

	while(1) {
		// wait until it is done again
		while(rtc_is_updating()) {}

		// retrieve current time again
		auto stamp = read_utc_from_rtc_immediate();

		// if we read the same values twice and the RTC was at some
		// point not updating in between these reads, we read a stable
		// value, so return it
		if(stamp == stamp_base) {
			return stamp;
		}

		// else, read again
		stamp_base = stamp;
	}
}

x86_rtc::x86_rtc(device *parent) : device(parent), irq_handler() {
}

const char *x86_rtc::description() {
	return "x86 real-time clock";
}

cloudabi_errno_t x86_rtc::init() {
	clock.set_current_utc_time(read_utc_from_rtc());
	return 0;
}

void x86_rtc::handle_irq(uint8_t) {
	assert(!"Shouldn't receive IRQs");
}

x86_rtc_clock::x86_rtc_clock() {
	get_clock_store()->register_clock(CLOUDABI_CLOCK_REALTIME, this);
}

cloudabi_timestamp_t x86_rtc_clock::get_resolution() {
	return get_monotonic()->get_resolution();
}

cloudabi_timestamp_t x86_rtc_clock::get_time(cloudabi_timestamp_t precision) {
	return get_monotonic()->get_time(precision) + offset_monotonic_to_utc;
}

thread_condition_signaler *x86_rtc_clock::get_signaler(cloudabi_timestamp_t timeout, cloudabi_timestamp_t precision) {
	// We currently convert times back to UTC, then get a monotonic
	// signaler for that time. Because of this, if an event is set in 1
	// month, it will still occur in 1 month. Otherwise, suppose the
	// date-time is reset to 1 month earlier, an event set in 1 minute
	// would occur in 1 month and 1 minute.
	return get_monotonic()->get_signaler(timeout - offset_monotonic_to_utc, precision);
}

void x86_rtc_clock::set_current_utc_time(cloudabi_timestamp_t utctime) {
	offset_monotonic_to_utc = utctime - get_monotonic()->get_time(0);
}

cloudos::clock *x86_rtc_clock::get_monotonic() {
	// TODO: check if the monotonic clock didn't change?
	return get_clock_store()->get_clock(CLOUDABI_CLOCK_MONOTONIC);
}
