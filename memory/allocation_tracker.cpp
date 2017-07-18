#include <memory/allocation_tracker.hpp>
#include <rng/rng.hpp>
#include <time/clock_store.hpp>

#ifdef TESTING_ENABLED
#include <stdlib.h>
#endif

using namespace cloudos;

#ifndef TESTING_ENABLED
static void stack_up(uintptr_t * &ebp, void * &eip) {
	if(ebp) {
		eip = reinterpret_cast<void*>(*(ebp + 1));
		ebp = reinterpret_cast<uintptr_t*>(*ebp);
	}
}
#endif

tracked_allocation::tracked_allocation() {
#ifndef TESTING_ENABLED
	{
		uintptr_t *ebp = nullptr;
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

	time = cloudos::track_detail::get_time();
#else
	for(size_t i = 0; i < NUM_ELEMENTS(caller); ++i) {
		caller[i] = 0;
	}
	arc4random_buf(alloc_prefix, sizeof(alloc_prefix));
	arc4random_buf(alloc_suffix, sizeof(alloc_suffix));
	time = 0;
#endif
}

cloudabi_timestamp_t cloudos::track_detail::get_time() {
#ifndef TESTING_ENABLED
	if(global_state_ && global_state_->clock_store) {
		auto c = get_clock_store()->get_clock(CLOUDABI_CLOCK_MONOTONIC);
		if(c) {
			return c->get_time(0);
		}
	}
#endif
	return 0;
}

void cloudos::track_detail::dump_allocation(tracked_allocation *track, cloudabi_timestamp_t start_tracking) {
	auto time = uint32_t(track->time / 1e6);
	auto diff = time - uint32_t(start_tracking / 1e6);
	get_vga_stream() << "- time: " << uint32_t(track->time / 1e6) << " (" << diff << " ms since start)\n";
	get_vga_stream() << "- size: " << track->user_size << "\n";
	for(size_t i = 0; i < NUM_ELEMENTS(track->caller); ++i) {
		get_vga_stream() << "- call frame " << i << ": " << track->caller[i] << "\n";
	}
}

size_t cloudos::track_detail::dump_allocations(tracked_allocation *track, bool tracking, cloudabi_timestamp_t start_tracking, cloudabi_timestamp_t stop_tracking) {
	if(track == nullptr) {
		// No leaks, dump nothing
		return 0;
	}

	auto now = uint32_t(get_time() / 1e6);
	auto start = uint32_t(start_tracking / 1e6);
	auto start_since = now - start;
	auto stop = uint32_t(stop_tracking / 1e6);
	auto stop_since = now - stop;

	get_vga_stream() << "====== POTENTIAL MEMORY LEAK REPORT ======\n";
	get_vga_stream() << "Started tracking at: " << start << " (" << start_since << " ms ago)\n";
	if(tracking) {
		// If tracking is still enabled, very recent allocations that
		// will still be deallocated are very likely to be mentioned in
		// this report
		get_vga_stream() << "Warning: tracking is still enabled\n";
	} else {
		get_vga_stream() << "Stopped tracking at: " << stop << " (" << stop_since << " ms ago)\n";
		get_vga_stream() << "Tracking period: " << (stop - start) << " ms\n";
	}

	size_t i = 0;
	size_t sum_size = 0;
	tracked_allocation *largest = nullptr;
	for(auto *t = track; t; t = t->next) {
		i++;
		sum_size += t->user_size;
		if(!largest) {
			largest = t;
		} else if(largest->user_size < t->user_size) {
			largest = t;
		}
		// assert they are ordered by time of allocation
		assert(t->time >= track->time);
	}
	get_vga_stream() << "Number of live allocations: " << i << ", summing ";
	if(sum_size > 8192) {
		get_vga_stream() << (sum_size / 1024) << " KiB\n";
	} else {
		get_vga_stream() << sum_size << " bytes\n";
	}
	get_vga_stream() << "Oldest allocation:\n";
	dump_allocation(track, start_tracking);
	if(track != largest) {
		get_vga_stream() << "Largest allocation:\n";
		dump_allocation(largest, start_tracking);
	} else if(track->next) {
		get_vga_stream() << "Second oldest allocation:\n";
		dump_allocation(track->next, start_tracking);
	}

	return i;
}
