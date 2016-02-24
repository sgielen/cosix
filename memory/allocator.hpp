#pragma once
#include <stdint.h>
#include <stddef.h>

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

	void *allocate_aligned(size_t x, size_t alignment) {
		uint32_t addr = reinterpret_cast<uint64_t>(allocate(x + alignment));
		uint32_t misalignment = addr % alignment;
		if(misalignment != 0) {
			// align it
			addr += alignment - misalignment;
		}
		return reinterpret_cast<void*>(addr);
	}

	template <typename T>
	T *allocate(size_t x = sizeof(T)) {
		return reinterpret_cast<T*>(allocate(x));
	}

private:
	uint8_t *handout;
	size_t capacity;
};

};
