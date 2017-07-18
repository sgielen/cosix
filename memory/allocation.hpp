#pragma once

#include <stdint.h>
#include <stddef.h>
#include <oslibc/assert.hpp>

#ifdef TESTING_ENABLED
#include <new>
#else
inline void *operator new(size_t, void *p) noexcept {
	return p;
}
#endif

namespace cloudos {

struct Blk {
	Blk() : ptr(nullptr), size(0) {}
	Blk(void *p, size_t s) : ptr(p), size(s) {}
	void *ptr;
	size_t size;
};

Blk allocate(size_t n);
Blk allocate_aligned(size_t n, size_t alignment);
void deallocate(Blk b);

template <typename T, class... Args>
T *allocate(Args&&... args) {
	Blk b = allocate(sizeof(T));
	if(b.ptr != nullptr) {
		assert(b.size == sizeof(T));
		new (b.ptr) T(args...);
	}
	return reinterpret_cast<T*>(b.ptr);
}

template <typename T>
void deallocate(T *ptr) {
	ptr->~T();
	deallocate({ptr, sizeof(*ptr)});
}

}
