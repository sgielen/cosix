#pragma once

#include "process_fd.hpp"

namespace cloudos {

struct interrupt_state_t;

struct scheduler {
	scheduler();

	void initial_yield();
	void thread_yield();
	void resume_running(interrupt_state_t *);

	void process_fd_ready(process_fd *fd);
	void process_fd_blocked(process_fd *fd);

	process_fd *get_running_process();

private:
	void schedule_next();

	process_list *running;
	process_list *ready;
};

}
