#pragma once

#include <oslibc/list.hpp>
#include <hw/interrupt.hpp>
#include <cloudabi/headers/cloudabi_types.h>

namespace cloudos {

struct process_fd;
struct scheduler;

struct thread;
typedef linked_list<thread*> thread_list;

static const cloudabi_tid_t MAIN_THREAD = 1;

typedef uint8_t sse_state_t [512] __attribute__ ((aligned (16)));

/**
 * A thread is a unit of execution, belonging to a process. It consists of
 * a userland stack, a kernel stack, a stack pointer for switching from
 * another kernel stack to this one, and an interrupt frame for switching
 * from this kernel stack to the userland stack.
 */
struct thread {
	/** Create a thread. auxv and entrypoint must already point to valid
	 * memory, while stack_location will be the initial stack pointer, so it must
	 * point just beyond a block of valid stack memory. The thread will start
	 * running at the entrypoint, and will receive the thread_id as a parameter on
	 * the stack unless the id is MAIN_THREAD. It will also receive the auxv ptr.
	 */
	thread(process_fd *process, void *stack_location, void *auxv, void *entrypoint, cloudabi_tid_t thread_id);

	/** Create a thread, forked off of another thread from another process. */
	thread(process_fd *process, thread *other_thread);

	void interrupt(int int_no, int err_code);
	void handle_syscall();

	void set_return_state(interrupt_state_t*);
	void get_return_state(interrupt_state_t*);
	void save_sse_state();
	void restore_sse_state();

	void *get_kernel_stack_top();
	void *get_fsbase();

	inline process_fd *get_process() { return process; }
	inline bool is_running() { return running; }

	inline void thread_exit() { running = false; }

private:
	process_fd *process;
	cloudabi_tid_t thread_id;
	bool running;

	friend struct cloudos::scheduler;
	void *esp = 0;

	interrupt_state_t state;
	sse_state_t sse_state;
	void *userland_stack_top = 0;
	void *kernel_stack_bottom = 0;
	size_t kernel_stack_size = 0;
};

}