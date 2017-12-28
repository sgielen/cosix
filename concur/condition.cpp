#include "condition.hpp"
#include <fd/scheduler.hpp>
#include <global.hpp>

using namespace cloudos;

thread_condition::thread_condition(thread_condition_signaler *s)
: signaler(s)
, satisfied(false)
{}

thread_condition::~thread_condition()
{
	if(signaler) {
		cancel();
	}
	if(conditiondata) {
		// this is a pointer to a base class, while implementations
		// will always be of a derived class. Since the number of
		// derived implementations are limited here, we can try them
		// all; however, this may not work in the future and then we
		// should keep track of the size of the block (either here or
		// in the allocator).
		if(auto *ptr = dynamic_cast<thread_condition_data_proc_terminate*>(conditiondata)) {
			deallocate(ptr);
		} else if(auto *ptr = dynamic_cast<thread_condition_data_fd_readwrite*>(conditiondata)) {
			deallocate(ptr);
		} else {
			// don't know how to deallocate this
			deallocate(conditiondata);
		}
	}
}

void thread_condition::satisfy(thread_condition_data *c)
{
	auto thr = thread.lock();
	assert(thr);
	assert(conditiondata == nullptr);
	conditiondata = c;
	satisfied = true;
	// unblock if it was blocked
	if(thr->is_blocked()) {
		thr->thread_unblock();
	}
	reset();
}

void thread_condition::cancel()
{
	reset();
}

void thread_condition::reset()
{
	assert(signaler != nullptr);
	signaler->remove_condition(this);
	signaler = nullptr;
	thread.reset();
}

thread_condition_signaler::thread_condition_signaler()
: satisfied_function(nullptr)
, satisfied_function_userdata(nullptr)
, conditions(nullptr)
{}

thread_condition_signaler::~thread_condition_signaler()
{
	// TODO: when there are still conditions left, we will never trigger
	// them so the threads will never wake up if they are waiting only on
	// this condition. This can happen, for example, if a file descriptor
	// is closed while another thread is waiting for read. Should we add
	// something like a 'failed trigger', so that at least the thread can
	// wake up and potentially handle the failed condition?
	while(conditions) {
		conditions->data->signaler = nullptr; /* we're going away, remove dangling pointer */
		auto *next = conditions->next;
		deallocate(conditions);
		conditions = next;
	}
}

void thread_condition_signaler::set_already_satisfied_function(thread_condition_satisfied_function_t function, void *userdata) {
	satisfied_function = function;
	satisfied_function_userdata = userdata;
}

bool thread_condition_signaler::already_satisfied(thread_condition *c, thread_condition_data **conditiondata) {
	if(*conditiondata) {
		*conditiondata = nullptr;
	}
	return satisfied_function ? satisfied_function(satisfied_function_userdata, c, conditiondata) : false;
}

void thread_condition_signaler::subscribe_condition(thread_condition *c)
{
	auto item = allocate<thread_condition_list>(c);
	// append myself, don't prepend, to prevent starvation
	append(&conditions, item);
}

void thread_condition_signaler::remove_condition(thread_condition *c)
{
	remove_one(&conditions, [&](thread_condition_list *item){
		return item->data == c;
	});
}

void thread_condition_signaler::condition_notify(thread_condition_data *conditiondata) {
	if(conditions) {
		auto *c = conditions;
		conditions->data->satisfy(conditiondata);
		// satisfy will call remove_condition(), which will call
		// remove_one(), assert that the first condition changed
		(void)c;
		assert(conditions != c);
	}
}

void thread_condition_signaler::condition_broadcast(thread_condition_data *conditiondata) {
	while(conditions) {
		auto *c = conditions;
		conditions->data->satisfy(conditiondata);
		// satisfy will call remove_condition(), which will call
		// remove_one(), assert that the first condition changed
		(void)c;
		assert(conditions != c);
	}
}

bool thread_condition_signaler::has_conditions() {
	return conditions != nullptr;
}

thread_condition_waiter::thread_condition_waiter()
: conditions(nullptr)
{}

thread_condition_waiter::~thread_condition_waiter()
{
	if(conditions != nullptr) {
		auto *list = finish();
		remove_all(&list, [](thread_condition_list *) {
			return true;
		});
	}
}

void thread_condition_waiter::add_condition(thread_condition *c) {
	auto item = allocate<thread_condition_list>(c);
	append(&conditions, item);
}

void thread_condition_waiter::wait() {
	auto thr = get_scheduler()->get_running_thread();
	bool initially_satisfied = false;

	// See if any conditions are already satisfied. Because
	// already_satisfied() may itself call this function recursively, we
	// must not link conditions to this thread before all
	// already_satisfied() calls are done. Otherwise, conditions in this
	// waiter may unblock this thread, while it is blocked in another
	// waiter, which we should guarantee can't happen.
	iterate(conditions, [&](thread_condition_list *item) {
		thread_condition *c = item->data;
		assert(c);
		assert(c->signaler);

		thread_condition_data *conditiondata = nullptr;
		if(c->signaler->already_satisfied(c, &conditiondata)) {
			initially_satisfied = true;

			if(!c->satisfied) {
				// quickly subscribe it
				c->thread = thr;
				c->signaler->subscribe_condition(c);
				// then satisfy it again
				c->satisfy(conditiondata);
			}
		}
	});

	if(!initially_satisfied) {
		// Subscribe on all conditions
		iterate(conditions, [&](thread_condition_list *item) {
			thread_condition *c = item->data;
			assert(c);
			assert(c->signaler);

			// it's possible that by now, the condition has already
			// become satisfied; in this case we also shouldn't
			// block the thread
			if(c->satisfied) {
				initially_satisfied = true;
			} else {
				c->thread = thr;
				c->signaler->subscribe_condition(c);
			}
		});

		// TODO: there is a race condition here, where a condition is not
		// satisfied when we check it, but it becomes satisfied before our
		// thread blocks. Then, the thread would never be unblocked because the
		// condition would not be re-satisfied.
		// I can think of two fixes:
		// - Spinlocking a subscribed signaler, so that it won't signal conditions
		//   before we're done blocking our thread
		// - Marking our thread blocked without actually yielding, before checking
		//   whether conditions are already satisfied; then, if a condition becomes
		//   satisfied during that period, the thread will be unblocked and will
		//   automatically be scheduled again sometime after the yield

		// Block ourselves waiting on a satisfaction
		if(!initially_satisfied) {
			thr->thread_block();
		}
	}

	// Cancel all subscribed, non-satisfied conditions (satisfied
	// conditions will be already removed by the signaler)
	size_t num_satisfied = 0;
	iterate(conditions, [&](thread_condition_list *item) {
		thread_condition *c = item->data;
		if(c->satisfied) {
			num_satisfied++;
		} else if(!c->thread.expired()) { /* if we set a thread on this condition */
			c->cancel();
		}
	});
	assert(num_satisfied > 0);
}

thread_condition_list *thread_condition_waiter::finish() {
	assert(conditions != nullptr); /* no conditions at all? */

	// remove all unsatisfied conditions
	remove_all(&conditions, [&](thread_condition_list *item) {
		return !item->data->satisfied;
	});

	assert(conditions != nullptr); /* no satisfied conditions? */

	thread_condition_list *i = conditions;
	conditions = nullptr;
	return i;
}
