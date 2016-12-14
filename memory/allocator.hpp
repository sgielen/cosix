#pragma once
#include <stdint.h>
#include <stddef.h>
#include "memory/segregator.hpp"
#include "memory/bucketizer.hpp"
#include "memory/multiple_page_allocator.hpp"
#include "memory/page_allocator.hpp"

#ifdef TESTING_ENABLED
#include <new>
#else
inline void *operator new(size_t, void *p) noexcept {
	return p;
}
#endif

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
	Bucketizer<page_allocator, 512, 4096, 256> large_bucketizer;
	MultiplePageAllocator<page_allocator> multiple_page;
	Bucketizer<page_allocator, 0, 512, 32> small_bucketizer;

	Segregator<4096,
		decltype(large_bucketizer),
		decltype(multiple_page)> large_segregator;

	Segregator<512,
		decltype(small_bucketizer),
		decltype(large_segregator)> small_segregator;

public:
	auto get_allocator() -> decltype(&small_segregator) {
		return &small_segregator;
	}
};

};
