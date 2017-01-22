#include <memory/allocation_tracker.hpp>
#include <rng/rng.hpp>
#include <time/clock_store.hpp>

using namespace cloudos;

static void stack_up(uintptr_t * &ebp, void * &eip) {
	if(ebp) {
		eip = reinterpret_cast<void*>(*(ebp + 1));
		ebp = reinterpret_cast<uintptr_t*>(*ebp);
	}
}

tracked_allocation::tracked_allocation() {
	{
		uintptr_t *ebp;
		asm volatile("mov %%ebp, %0" : "=r"(ebp));
		void *eip = nullptr;
		stack_up(ebp, eip);
		stack_up(ebp, eip);
		for(size_t i = 0; i < NUM_ELEMENTS(caller); ++i) {
			stack_up(ebp, eip);
			caller[i] = eip;
		}
	}

	auto r = get_random();
	r->get(alloc_prefix, sizeof(alloc_prefix));
	r->get(alloc_suffix, sizeof(alloc_suffix));

	time = 0;
	if(global_state_ && global_state_->clock_store) {
		auto c = get_clock_store()->get_clock(CLOUDABI_CLOCK_MONOTONIC);
		if(c) {
			time = c->get_time(0);
		}
	}
}
