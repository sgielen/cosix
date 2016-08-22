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
	auto *old_process = running;

	if(running) {
		// Don't re-schedule processes that have exited
		if(running->data->is_running()) {
			append(&ready, running);
		}
	}

	if(!ready) {
		// for now, init() is always ready for running. When we implement blocking,
		// it might be possible that nothing is ready for running; in that case, just
		// do nothing until we're interrupted again.
		kernel_panic("schedule_next() was called, but nothing was ready");
	}

	running = ready;
	ready = running->next;
	running->next = nullptr;

	if(!running->data->is_running()) {
		kernel_panic("A process in the ready list had actually exited");
	}

	if(old_process != 0 && old_process != running) {
		old_process->data->save_sse_state();
		running->data->restore_sse_state();
	}
}

void scheduler::resume_running(interrupt_state_t *regs)
{
	if(!running) {
		kernel_panic("Cannot resume_running with nothing scheduled to run");
	}

	while(running && !running->data->is_running()) {
		schedule_next();
	}

	if(!running) {
		// This can't happen, because init is always running
		kernel_panic("Cannot resume_running, all processes exited");
	}

	process_fd *fh = running->data;
	fh->get_return_state(regs);
	fh->install_page_directory();
	get_gdt()->set_fsbase(fh->get_fsbase());
	get_gdt()->set_kernel_stack(fh->get_kernel_stack_top());
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
