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
, dealloc(0)
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
	auto old_thread = running->data;

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

	if(old_thread && old_thread != running->data) {
		assert(old_thread.use_count() > 1);
		void **esp = &old_thread->esp;
		old_thread.reset();
		switch_thread(esp, running->data->esp);
		// scheduler yielded back to this thread

		if(dealloc) {
			assert(dealloc->next == nullptr);
			assert(dealloc->data);
			weak_ptr<thread> thr_weak = dealloc->data;
			dealloc->data.reset();
			deallocate(dealloc);
			dealloc = nullptr;
			assert(thr_weak.expired());
		}
	}
}

void scheduler::schedule_next()
{
	auto old_thread = running;
	running = nullptr;

	if(old_thread != 0 && !old_thread->data->is_exited() && !old_thread->data->is_blocked()) {
		// add this thread to the ready list, since we can reschedule it immediately
		assert(old_thread->data->get_process()->is_running());
		append(&ready, old_thread);
	}

	while(ready && !ready->data->is_ready()) {
		// next thread is not ready for running, unschedule it, we'll re-schedule it
		// later
		ready->data->unscheduled = true;
		auto *next = ready->next;
		deallocate(ready);
		ready = next;
	}

	// Note: it's possible no thread was ready, in which case running is
	// set to nullptr here
	running = ready;
	if(running) {
		ready = running->next;
		running->next = nullptr;

		if(running->data->is_blocked() || running->data->is_exited() || !running->data->get_process()->is_running()) {
			get_vga_stream() << "Thread: " << running->data << ", process: " << running->data->get_process() << ", " << running->data->get_process()->name << "\n";
			kernel_panic("A thread in the ready list was blocked or had already exited");
		}
	}

	if(old_thread != running) {
		if(old_thread != 0) {
			assert(old_thread->next == nullptr);
			old_thread->data->save_sse_state();

			// reschedule, deallocate or forget about it
			if(old_thread->data->is_exited()) {
				// deallocate it when we next switch
				dealloc = old_thread;
			} else if(old_thread->data->is_blocked()) {
				old_thread->data->unscheduled = true;
				// can safely forget about this thread here,
				// since it's still running the process keeps
				// it alive
				assert(old_thread->data.use_count() > 1);
				old_thread->data.reset();
				deallocate(old_thread);
				old_thread = nullptr;
			}
		}

		if(running != 0) {
			running->data->get_process()->install_page_directory();
			get_gdt()->set_fsbase(running->data->get_fsbase());
			get_gdt()->set_kernel_stack(running->data->get_kernel_stack_top());
			running->data->restore_sse_state();
		}
	}
}

void scheduler::thread_ready(shared_ptr<thread> fd)
{
	// add to ready
	thread_list *e = allocate<thread_list>(fd);
	append(&ready, e);
}

void scheduler::thread_exiting(shared_ptr<thread>)
{
	// don't need to do anything, I'll notice it once I try
	// to schedule this thread
}

void scheduler::thread_blocked(shared_ptr<thread>)
{
	// don't need to do anything, I'll notice it once I try
	// to schedule this thread
}

shared_ptr<thread> scheduler::get_running_thread()
{
	return running == nullptr ? nullptr : running->data;
}
