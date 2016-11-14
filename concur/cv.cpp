#include "cv.hpp"

using namespace cloudos;

void cv_t::wait() {
	thread_condition c(&signaler);

	thread_condition_waiter w;
	w.add_condition(&c);
	w.wait();
}

void cv_t::notify() {
	signaler.condition_notify();
}

void cv_t::broadcast() {
	signaler.condition_broadcast();
}
