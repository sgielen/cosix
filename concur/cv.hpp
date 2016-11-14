#pragma once

#include <fd/thread.hpp>
#include "condition.hpp"

namespace cloudos {

/** Condition variable implementation.
 *
 * A condition variable allows a thread to efficiently wait for some condition.
 * Instead of constantly yielding until the condition becomes true, the thread
 * can add itself to a cv's waiting list and mark itself blocked. Then, when
 * another thread may have changed the condition, it notifies the CV, and one
 * thread may check whether its condition is now good. (It can also 'broadcast'
 * the CV, in which case all threads may check whether their conditions are now
 * good.)
 *
 * This CV implementation, unlike normal implementations, does not take a
 * mutex. This mutex is normally held by a user while checking the condition,
 * then passed to wait() so it is unlocked while blocking on the CV. This
 * prevents a race condition where the condition is found false, but before
 * CV::wait() is called, the condition becomes true and the CV is notified.
 * Because the thread enters the waiting list too late, it is never woken up,
 * even though the condition became true. However, because this kernel is
 * uniprocessor and does not do kernel thread preemption, this race condition
 * can never occur.
 */
struct cv_t {
	inline thread_condition_signaler &get_signaler() {
		return signaler;
	}

	void wait();
	void notify();
	void broadcast();

private:
	thread_condition_signaler signaler;
};

}
