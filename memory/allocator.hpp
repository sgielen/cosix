#pragma once
#include <stdint.h>
#include <stddef.h>
#include "memory/segregator.hpp"
#include "memory/bucketizer.hpp"
#include "memory/map_virtual.hpp"
#include "memory/allocation_tracker.hpp"

namespace cloudos {

class allocator {
public:
	allocator();

	void *allocate(size_t x);

	void *allocate_aligned(size_t x, size_t alignment);

	template <typename T>
	T *allocate(size_t x = sizeof(T)) {
		return reinterpret_cast<T*>(allocate(x));
	}

private:
	Bucketizer<map_virtual, 512, 4096, 256> large_bucketizer;
	Bucketizer<map_virtual, 0, 512, 32> small_bucketizer;

	Segregator<4096,
		decltype(large_bucketizer),
		map_virtual> large_segregator;

	Segregator<512,
		decltype(small_bucketizer),
		decltype(large_segregator)> small_segregator;

#ifndef NDEBUG
	AllocationTracker<decltype(small_segregator)> allocation_tracker;
#endif

public:
#ifdef NDEBUG
	auto get_allocator() -> decltype(&small_segregator) {
		return &small_segregator;
	}
#else
	auto get_allocator() -> decltype(&allocation_tracker) {
		return &allocation_tracker;
	}
#endif
};

};
