#pragma once

#include "thread.hpp"

namespace cloudos {

struct interrupt_state_t;

struct scheduler {
	scheduler();

	void initial_yield();
	void thread_yield();
	void resume_running(interrupt_state_t *);

	void thread_ready(thread *thr);
	void thread_exiting(thread *thr);
	void thread_blocked(thread *thr);

	thread *get_running_thread();

private:
	void schedule_next();

	thread_list *running;
	thread_list *ready;
};

}
