#include "cv.hpp"
#include <memory/allocator.hpp>
#include <global.hpp>
#include <fd/scheduler.hpp>

using namespace cloudos;

cv_t::cv_t() : waiting_threads(0) {}

void cv_t::wait() {
	auto *thr = get_scheduler()->get_running_thread();

	auto item = get_allocator()->allocate<thread_list>();
	item->data = thr;
	item->next = nullptr;
	// append myself, don't prepend, to prevent starvation
	append(&waiting_threads, item);

	thr->thread_block();
	// the thread unblocked, so apparantly the cv was notified
}

void cv_t::notify() {
	if(waiting_threads) {
		thread_list *item = waiting_threads;
		waiting_threads = item->next;
		item->next = nullptr;
		item->data->thread_unblock();
	}
}

void cv_t::broadcast() {
	while(waiting_threads) {
		notify();
	}
}
