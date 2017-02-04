#pragma once

#include <oslibc/list.hpp>
#include <hw/interrupt.hpp>
#include <cloudabi/headers/cloudabi_types.h>
#include <memory/allocation.hpp>
#include <memory/smart_ptr.hpp>

namespace cloudos {

struct process_fd;
struct scheduler;

struct thread;
typedef linked_list<shared_ptr<thread>> thread_list;
typedef linked_list<weak_ptr<thread>> thread_weaklist;

// MAIN_THREAD should be set to a value higher than 0, so that locks that are
// acquired by a thread always have a nonzero value. This is assumed in the
// implementation.
static const cloudabi_tid_t MAIN_THREAD = 1;

typedef uint8_t sse_state_t [512] __attribute__ ((aligned (16)));

/**
 * A thread is a unit of execution, belonging to a process. It consists of
 * a userland stack, a kernel stack, a stack pointer for switching from
 * another kernel stack to this one, and an interrupt frame for switching
 * from this kernel stack to the userland stack.
 */
struct thread : enable_shared_from_this<thread> {
	/** Create a thread. auxv and entrypoint must already point to valid
	 * memory, while stack_location will be the initial stack pointer, so it must
	 * point just beyond a block of valid stack memory. The thread will start
	 * running at the entrypoint, and will receive the thread_id as a parameter on
	 * the stack unless the id is MAIN_THREAD. It will also receive the auxv ptr.
	 */
	thread(process_fd *process, void *stack_location, void *auxv, void *entrypoint, cloudabi_tid_t thread_id);

	/** Create a thread, forked off of another thread from another process. */
	thread(process_fd *process, shared_ptr<thread> other_thread);

	~thread();

	inline cloudabi_tid_t get_thread_id() {
		return thread_id;
	}

	void interrupt(int int_no, int err_code);
	void handle_syscall();

	void set_return_state(interrupt_state_t*);
	void get_return_state(interrupt_state_t*);
	void save_sse_state();
	void restore_sse_state();

	void *get_kernel_stack_top();
	void *get_fsbase();

	inline process_fd *get_process() { return process; }

	inline bool is_exited() { return exited; }
	bool is_ready();
	inline bool is_blocked() { return blocked; }
	inline bool is_unscheduled() { return unscheduled; }

	void thread_exit();
	void thread_block();
	void thread_unblock();

	void acquire_userspace_lock(_Atomic(cloudabi_lock_t) *lock, cloudabi_eventtype_t locktype);
	void drop_userspace_lock(_Atomic(cloudabi_lock_t) *lock);

	void wait_userspace_cv(_Atomic(cloudabi_condvar_t) *condvar);
	void signal_userspace_cv(_Atomic(cloudabi_condvar_t) *condvar, cloudabi_nthreads_t nwaiters);

private:
	process_fd *process = 0;
	// note: only the bottom 30 bits may be used
	cloudabi_tid_t thread_id = 0;
	bool exited = false;

	friend struct cloudos::scheduler;
	void *esp = 0;

	bool blocked = false;
	bool unscheduled = false;

	interrupt_state_t state;
	sse_state_t sse_state;
	void *userland_stack_top = 0;
	Blk kernel_stack_alloc;
	size_t kernel_stack_size = 0;
};

}
