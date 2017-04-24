#pragma once

#include "thread.hpp"

namespace cloudos {

struct interrupt_state_t;

struct scheduler {
	scheduler();

	inline bool is_waiting_for_ready_task() {
		return waiting_for_ready_task;
	}

	[[noreturn]] void initial_yield();
	[[noreturn]] void thread_final_yield();
	void thread_yield();
	void resume_running(interrupt_state_t *);

	void thread_ready(shared_ptr<thread> thr);
	void thread_exiting(shared_ptr<thread> thr);
	void thread_blocked(shared_ptr<thread> thr);

	shared_ptr<thread> get_running_thread();

private:
	void wait_for_next();
	void schedule_next();

	thread_list *running = nullptr;
	thread_list *ready = nullptr;
	thread_list *dealloc_later = nullptr;
	bool waiting_for_ready_task = true;
};

}
