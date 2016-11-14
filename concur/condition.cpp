#include "condition.hpp"
#include <memory/allocator.hpp>
#include <fd/scheduler.hpp>
#include <global.hpp>

using namespace cloudos;

thread_condition::thread_condition(thread_condition_signaler *s)
: signaler(s)
, thread(nullptr)
, satisfied(false)
{}

void thread_condition::satisfy()
{
	if(thread == nullptr) {
		kernel_panic("Thread condition is satisfied, but has no thread");
	}
	satisfied = true;
	// unblock if it was blocked
	thread->thread_unblock();
}

thread_condition_signaler::thread_condition_signaler()
: conditions(nullptr)
{}

void thread_condition_signaler::subscribe_condition(thread_condition *c)
{
	auto item = get_allocator()->allocate<thread_condition_list>();
	item->data = c;
	item->next = nullptr;
	// append myself, don't prepend, to prevent starvation
	append(&conditions, item);
}

void thread_condition_signaler::cancel_condition(thread_condition *c)
{
	if(c->satisfied) {
		kernel_panic("Condition is cancelled, but already satisfied");
	}

	remove_one(&conditions, [&](thread_condition_list *item){
		return item->data == c;
	}, [&](thread_condition*){});
}

void thread_condition_signaler::condition_notify() {
	if(conditions) {
		thread_condition_list *item = conditions;
		conditions = item->next;
		item->next = nullptr;
		item->data->satisfy();
	}
}

void thread_condition_signaler::condition_broadcast() {
	while(conditions) {
		condition_notify();
	}
}

thread_condition_waiter::thread_condition_waiter()
: conditions(nullptr)
{}

thread_condition_waiter::~thread_condition_waiter()
{
	if(conditions != nullptr) {
		finish();
	}
}

void thread_condition_waiter::add_condition(thread_condition *c) {
	auto item = get_allocator()->allocate<thread_condition_list>();
	item->data = c;
	item->next = nullptr;
	append(&conditions, item);
}

void thread_condition_waiter::wait() {
	auto *thr = get_scheduler()->get_running_thread();

	// TODO: check if any of these conditions is already satisfied, if
	// possible

	// Subscribe on all conditions
	iterate(conditions, [&](thread_condition_list *item) {
		thread_condition *c = item->data;
		c->thread = thr;
		c->satisfied = false;
		c->signaler->subscribe_condition(c);
	});

	// Block ourselves
	thr->thread_block();

	// Cancel all non-satisfied conditions (the rest will be already
	// removed by the signaler)
	iterate(conditions, [&](thread_condition_list *item) {
		thread_condition *c = item->data;
		if(!c->satisfied) {
			c->signaler->cancel_condition(c);
		}
	});
}

thread_condition_list *thread_condition_waiter::finish() {
	// find the first thread condition that is satisfied, then
	// set its 'next' ptr to the next one continuously

	thread_condition_list *head = nullptr;
	thread_condition_list *tail = nullptr;

	iterate(conditions, [&](thread_condition_list *item) {
		if(!item->data->satisfied) {
			return;
		}
		if(!head) {
			head = tail = item;
		} else {
			tail->next = item;
			tail = item;
		}
	});

	if(tail == nullptr) {
		kernel_panic("Condition waiter is finishing, but has no satisfied thread conditions");
	}
	tail->next = nullptr;
	conditions = nullptr;
	return head;
}
