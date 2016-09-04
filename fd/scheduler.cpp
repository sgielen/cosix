#include "scheduler.hpp"
#include "global.hpp"
#include <fd/process_fd.hpp>
#include <hw/interrupt.hpp>
#include <hw/segments.hpp>
#include <memory/allocator.hpp>

extern "C" void switch_thread(void **old_sp, void *sp);

using namespace cloudos;

scheduler::scheduler()
: running(0)
, ready(0)
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
	auto *old_thread = running;

	schedule_next();

	if(old_thread != 0 && old_thread != running) {
		switch_thread(&old_thread->data->esp, running->data->esp);
		// scheduler yielded back to this thread
	}
}

void scheduler::schedule_next()
{
	auto *old_thread = running;

	if(running) {
		// Immediately unschedule processes that are blocked or have exited
		if(running->data->is_blocked()) {
			running->data->unscheduled = true;
		} else if(running->data->is_running() && running->data->get_process()->is_running()) {
			append(&ready, running);
		}
	}

	while(ready && !ready->data->is_ready()) {
		// next thread is not ready for running, unschedule it, we'll re-schedule it
		// later
		ready->data->unscheduled = true;
		ready = ready->next;
	}

	if(!ready) {
		// for now, init() is always ready for running. That's a waste of CPU time; we should
		// actually halt if nothing is ready. Then, interrupts should also be enabled
		// temporarily, so that we can be woken up again. However, I should study the effects
		// of such preemptions on the active kernel thread, and ensure that there's no problem
		// when such a thing happens inside the scheduler.
		kernel_panic("schedule_next() was called, but nothing was ready");
	}

	running = ready;
	ready = running->next;
	running->next = nullptr;

	if(running->data->is_blocked() || !running->data->is_running() || !running->data->get_process()->is_running()) {
		get_vga_stream() << "Thread: " << running->data << ", process: " << running->data->get_process() << ", " << running->data->get_process()->name << "\n";
		kernel_panic("A thread in the ready list was blocked or had already exited");
	}

	if(old_thread != running) {
		if(old_thread != 0) {
			old_thread->data->save_sse_state();
		}
		running->data->get_process()->install_page_directory();
		get_gdt()->set_fsbase(running->data->get_fsbase());
		get_gdt()->set_kernel_stack(running->data->get_kernel_stack_top());
		running->data->restore_sse_state();
	}
}

void scheduler::thread_ready(thread *fd)
{
	// add to ready
	thread_list *e = get_allocator()->allocate<thread_list>();
	e->data = fd;
	e->next = nullptr;

	append(&ready, e);
}

void scheduler::thread_exiting(thread *)
{
	// don't need to do anything, I'll notice it once I try
	// to schedule this thread
}

void scheduler::thread_blocked(thread *)
{
	// don't need to do anything, I'll notice it once I try
	// to schedule this thread
}

thread *scheduler::get_running_thread()
{
	return running == 0 ? 0 : running->data;
}
