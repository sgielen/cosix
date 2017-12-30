#pragma once
#include <stdint.h>
#include <stddef.h>
#include <oslibc/list.hpp>
#include <fd/thread.hpp>

namespace cloudos {

struct thread_condition;
typedef linked_list<thread_condition*> thread_condition_list;

struct thread_condition_signaler;
struct thread_condition_waiter;

struct thread_condition_data {
	virtual ~thread_condition_data() {}
};

struct thread_condition_data_proc_terminate : public thread_condition_data {
	cloudabi_signal_t signal;
	cloudabi_exitcode_t exitcode;
};

struct thread_condition_data_fd_readwrite : public thread_condition_data {
	cloudabi_filesize_t nbytes;
	cloudabi_eventrwflags_t flags;
};

/** The thread condition. This is any kind of condition that a thread may need
 * to wait on. They are satisfied (fulfilled) by thread condition signalers,
 * owned by an object such as a condvar or an fd. A thread can block on a set
 * of thread conditions by making a thread_condition_waiter. This can just be
 * done in local scope.
 *
 * A condition is usually created on-stack in local scope, and is not
 * otherwise owned.
 *
 * A condition can receive thread_condition_data in its satisfy function. It
 * will take ownership of this pointer, and will deallocate it once destructed.
 */
struct thread_condition {
	thread_condition(thread_condition_signaler *signaler);
	~thread_condition();
	void satisfy(thread_condition_data *conditiondata = nullptr);
	void cancel();

	void *userdata = nullptr;
	thread_condition_data *conditiondata = nullptr;

private:
	void reset();

	thread_condition_signaler *signaler;
	weak_ptr<thread> thread;
	bool satisfied;

	friend thread_condition_signaler;
	friend thread_condition_waiter;
};

/** The thread condition signaler. This object is owned by an object that can
 * satisfy thread conditions. For example, a process FD will have an 'exited'
 * signaler, while pipe fd will have a 'readable' signaler and a 'writable'
 * signaler. When a signaler is satisfied, one or all of its thread conditions
 * are satisfied (depending on whether condition_notify() or
 * condition_broadcast() was called); this will cause threads to wake up in
 * their thread_condition_waiter if they haven't yet.
 *
 * A signaler also has an 'already satisfied function', which will determine
 * for given userdata/condition whether the condition was already satisfied.
 * If so, any waiter will return immediately without blocking.
 *
 * When a condition is satisfied, or was already satisfied, a
 * thread_condition_data can be given that can contain extra information on the
 * satisfaction of a condition, such as process exit code. This information is
 * given to the thread_condition, which owns (and will deallocate()) the
 * information object after this.
 *
 * The signaler does not own its conditions.
 */
struct thread_condition_signaler {
	thread_condition_signaler();
	thread_condition_signaler(thread_condition_signaler const&) = delete;
	~thread_condition_signaler();

	typedef bool (*thread_condition_satisfied_function_t)(void*, thread_condition*, thread_condition_data**);

	void set_already_satisfied_function(thread_condition_satisfied_function_t function, void *userdata);

	bool already_satisfied(thread_condition *c, thread_condition_data **data);
	void subscribe_condition(thread_condition *c);
	void remove_condition(thread_condition *c);

	void condition_notify(thread_condition_data *conditiondata = nullptr);

	struct no_conditiondata_builder {
		thread_condition_data *operator()() { return nullptr; }
	};

	template <typename Function = no_conditiondata_builder>
	void condition_broadcast(Function conditiondata_builder = {}) {
		while(conditions) {
			condition_notify(conditiondata_builder());
		}
	}

	bool has_conditions();

private:
	thread_condition_satisfied_function_t satisfied_function;
	void *satisfied_function_userdata;
	thread_condition_list *conditions;
};

/** The thread condition waiter. This object is created by a thread that needs
 * to wait for a condition. It gets the conditions to wait for in its
 * add_condition() method. When wait() is called, it only returns when at least
 * one of the conditions is satisfied, and all others are cancelled. By calling
 * finish(), the thread receives a list of conditions that have been satisfied;
 * the rest of the conditions is removed from the waiter so that it returns to
 * its initial, empty state.
 *
 * The waiter is usually created on the stack in local scope. It does not own
 * the conditions, which are also often created on the stack.
 */
struct thread_condition_waiter {
	thread_condition_waiter();
	~thread_condition_waiter();

	void add_condition(thread_condition*);

	void wait();
	thread_condition_list *finish();

private:
	thread_condition_list *conditions;
};

}
