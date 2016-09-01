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
		// Don't re-schedule processes that have exited
		if(running->data->is_running() && running->data->get_process()->is_running()) {
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

	if(!running->data->is_running() || !running->data->get_process()->is_running()) {
		get_vga_stream() << "Exited thread: " << running->data << ", process: " << running->data->get_process() << ", " << running->data->get_process()->name << "\n";
		kernel_panic("A thread in the ready list had actually exited");
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

void scheduler::thread_exiting(thread *thr)
{
	thread_blocked(thr);
}

void scheduler::thread_blocked(thread *thr)
{
	// remove thread from the blocked list
	remove_object(&ready, thr, [](thread*){});
}

thread *scheduler::get_running_thread()
{
	return running == 0 ? 0 : running->data;
}
