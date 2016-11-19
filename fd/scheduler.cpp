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
, waiting_for_ready_task(true)
{}

void scheduler::initial_yield()
{
	// yield from the initial kernel thread to init's kernel thread
	schedule_next();
	if(!running) {
		kernel_panic("In initial_yield(), schedule_next() found nothing to run");
	}
	void *esp;
	switch_thread(&esp, running->data->esp);
}

void scheduler::thread_yield()
{
	auto *old_thread = running;

	waiting_for_ready_task = true;
	while(1) {
		schedule_next();
		if(running != nullptr) {
			break;
		}

		// Wait for the next interrupt and then try to schedule
		// something again. While we're doing this,
		// waiting_for_ready_task remains true, which will prevent the
		// timer interrupt from triggering another thread_yield(). This
		// thread_yield would also wait until a timer interrupt, ad
		// infinitum, so the variable prevents eventual stack overflow.

		// TODO: we should also know when the next interesting clock
		// event occurs and program our next timer interrupt to occur
		// then, so we can handle the event immediately as it comes up.
		asm volatile("sti; hlt; nop; cli;");
	}
	waiting_for_ready_task = false;

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

	// Note: it's possible no thread was ready, in which case running is
	// set to nullptr here
	running = ready;
	if(running) {
		ready = running->next;
		running->next = nullptr;

		if(running->data->is_blocked() || !running->data->is_running() || !running->data->get_process()->is_running()) {
			get_vga_stream() << "Thread: " << running->data << ", process: " << running->data->get_process() << ", " << running->data->get_process()->name << "\n";
			kernel_panic("A thread in the ready list was blocked or had already exited");
		}
	}

	if(old_thread != running) {
		if(old_thread != 0) {
			old_thread->data->save_sse_state();
		}
		if(running != 0) {
			running->data->get_process()->install_page_directory();
			get_gdt()->set_fsbase(running->data->get_fsbase());
			get_gdt()->set_kernel_stack(running->data->get_kernel_stack_top());
			running->data->restore_sse_state();
		}
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
