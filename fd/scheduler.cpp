#include "scheduler.hpp"
#include "global.hpp"
#include <hw/interrupt.hpp>
#include <hw/segments.hpp>
#include <memory/allocator.hpp>

using namespace cloudos;

scheduler::scheduler()
: running(0)
, ready(0)
, pid_counter(1)
{}

void scheduler::schedule_next()
{
	if(running) {
		append(&ready, running);
	}

	if(ready) {
		running = ready;
		ready = running->next;
		running->next = nullptr;
	}
}

void scheduler::resume_running(interrupt_state_t *regs)
{
	if(running) {
		process_fd *fh = running->data;
		fh->get_return_state(regs);
		fh->install_page_directory();
		get_gdt()->set_fsbase(fh->get_fsbase());
		get_gdt()->set_kernel_stack(fh->get_kernel_stack_top());
	}
}

void scheduler::process_fd_ready(process_fd *fd)
{
	fd->pid = pid_counter++;

	// add to ready
	process_list *e = get_allocator()->allocate<process_list>();
	e->data = fd;
	e->next = nullptr;

	append(&ready, e);
}

void scheduler::process_fd_blocked(process_fd *)
{
	kernel_panic("process_fd_blocked: unimplemented");
}

process_fd *scheduler::get_running_process()
{
	return running == 0 ? 0 : running->data;
}
