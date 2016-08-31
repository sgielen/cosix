#include "scheduler.hpp"
#include "global.hpp"
#include <hw/interrupt.hpp>
#include <hw/segments.hpp>
#include <memory/allocator.hpp>

extern "C" void switch_thread(void **old_sp, void *sp);

using namespace cloudos;

scheduler::scheduler()
: running(0)
, ready(0)
, pid_counter(1)
{}

void scheduler::initial_yield()
{
	// yield from the initial kernel thread to init's kernel thread
	schedule_next();
	void *esp;
	switch_thread(&esp, running->data->esp);
}

void scheduler::thread_yield()
{
	auto *old_process = running;

	schedule_next();

	if(old_process != 0 && old_process != running) {
		switch_thread(&old_process->data->esp, running->data->esp);
		// scheduler yielded back to this thread
	}
}

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

	if(old_process != running) {
		if(old_process != 0) {
			old_process->data->save_sse_state();
		}
		running->data->install_page_directory();
		get_gdt()->set_fsbase(running->data->get_fsbase());
		get_gdt()->set_kernel_stack(running->data->get_kernel_stack_top());
		running->data->restore_sse_state();
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
