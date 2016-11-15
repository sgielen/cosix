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

/** The thread condition. This is any kind of condition that a thread may need
 * to wait on. They are satisfied (fulfilled) by thread condition signalers,
 * owned by an object such as a condvar or an fd. A thread can block on a set
 * of thread conditions by making a thread_condition_waiter. This can just be
 * done in local scope.
 *
 * A condition is usually created on-stack in local scope, and is not
 * otherwise owned.
 */
struct thread_condition {
	thread_condition(thread_condition_signaler *signaler);
	void satisfy();

	void *userdata;

private:
	//thread_condition_type type;
	thread_condition_signaler *signaler;
	thread *thread;
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
 * The signaler does not own its conditions.
 */
struct thread_condition_signaler {
	thread_condition_signaler();

	typedef bool (*thread_condition_satisfied_function_t)(void*, thread_condition*);

	void set_already_satisfied_function(thread_condition_satisfied_function_t function, void *userdata);

	bool already_satisfied(thread_condition *c);
	void subscribe_condition(thread_condition *c);
	void cancel_condition(thread_condition *c);

	void condition_notify();
	void condition_broadcast();

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
