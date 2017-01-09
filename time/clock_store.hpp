#pragma once

#include <cloudabi/headers/cloudabi_types.h>
#include <concur/condition.hpp>

namespace cloudos {

struct clock {
	virtual ~clock();

	/* all timestamp_t's are in nanoseconds */

	// Resolution: the smallest possible increase in time the clock model allows
	virtual cloudabi_timestamp_t get_resolution() = 0;
	virtual cloudabi_timestamp_t get_time(cloudabi_timestamp_t precision) = 0;

	/* return a thread_condition_signaler that will signal when the clock passes
	 * the given timestamp (in absolute time for this clock). The signal is allowed
	 * to be delayed by at most 'precision', which allows the clock to coalesce the
	 * signalers.
	 *
	 * The clock owns the returned signaler, and will broadcast and destroy
	 * it once the time has passed. If the given time has already passed,
	 * the behaviour of this function is undefined. If the time passes
	 * after you got the signaler, it may be destroyed, so use the pointer
	 * immediately to add thread conditions and do not store it.
	 */
	virtual thread_condition_signaler *get_signaler(
		cloudabi_timestamp_t timeout, cloudabi_timestamp_t precision) = 0;
};

struct clock_store {
	void register_clock(cloudabi_clockid_t type, clock *obj);
	clock *get_clock(cloudabi_clockid_t type);

private:
	static constexpr auto NUM_CLOCKS = CLOUDABI_CLOCK_THREAD_CPUTIME_ID + 1;
	clock *clocks[NUM_CLOCKS];
};

}
