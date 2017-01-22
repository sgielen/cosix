#pragma once

#include <stdint.h>
#include <stddef.h>
#include <memory/allocation.hpp>
#include <oslibc/assert.hpp>
#include <oslibc/string.h>
#include <cloudabi/headers/cloudabi_types.h>

namespace cloudos {

struct tracked_allocation {
	tracked_allocation();

	char track_marker[8] = {'A', 'L', 'L', 'C', 'M', 'G', 'I', 'C'};
	char alloc_prefix[8];
	char alloc_suffix[8];

	tracked_allocation *prev = nullptr;
	tracked_allocation *next = nullptr;

	void *caller[4] = {nullptr, nullptr, nullptr, nullptr};

	Blk blk;
	size_t user_size;
	cloudabi_timestamp_t time;
};

template <typename Allocator>
struct AllocationTracker {
	AllocationTracker(Allocator *a)
	: allocator(a)
	{}

	void assert_alloc(char *buf, size_t size) {
		char *original_alloc = buf - sizeof(tracked_allocation) - sizeof(tracked_allocation::alloc_prefix);
		tracked_allocation *info = reinterpret_cast<tracked_allocation*>(original_alloc);

		char *prefix = buf - sizeof(tracked_allocation::alloc_prefix);
		char *suffix = buf + info->user_size;

		assert(memcmp(original_alloc, "ALLCMGIC", sizeof(info->track_marker)) == 0);
		assert(memcmp(prefix, info->alloc_prefix, sizeof(info->alloc_prefix)) == 0);
		assert(memcmp(suffix, info->alloc_suffix, sizeof(info->alloc_suffix)) == 0);
		assert(size == info->user_size);
	}

	Blk allocate_aligned(size_t s, size_t alignment) {
		size_t actual_size = s + alignment + sizeof(tracked_allocation) + sizeof(tracked_allocation::alloc_prefix) + sizeof(tracked_allocation::alloc_suffix);
		Blk res = allocator->allocate(actual_size);
		assert(res.ptr == 0 || res.size == actual_size);
		if(res.ptr == 0) {
			return {};
		}

		// find the first ptr in this allocation that is aligned
		uint32_t actual_ptr = reinterpret_cast<uint32_t>(res.ptr) + sizeof(tracked_allocation) + sizeof(tracked_allocation::alloc_prefix);
		auto misalignment = actual_ptr % alignment;
		if(misalignment != 0) {
			actual_ptr += alignment - misalignment;
		}

		assert((actual_ptr % alignment) == 0);

		tracked_allocation *info = reinterpret_cast<tracked_allocation*>(actual_ptr - sizeof(tracked_allocation) - sizeof(tracked_allocation::alloc_prefix));
		return track_allocation(info, res, s);
	}

	Blk allocate(size_t s) {
		size_t actual_size = s + sizeof(tracked_allocation) + sizeof(tracked_allocation::alloc_prefix) + sizeof(tracked_allocation::alloc_suffix);
		Blk res = allocator->allocate(actual_size);
		assert(res.ptr == 0 || res.size == actual_size);
		if(res.ptr == 0) {
			return {};
		}

		tracked_allocation *info = reinterpret_cast<tracked_allocation*>(res.ptr);
		return track_allocation(info, res, s);
	}

	Blk track_allocation(tracked_allocation *info, Blk blk, size_t size) {
		new (info) tracked_allocation();

		if(tracking) {
			if(head == nullptr) {
				head = info;
			}

			if(tail) {
				tail->next = info;
			}

			info->prev = tail;
			tail = info;
		}

		char *prefix = reinterpret_cast<char*>(info) + sizeof(tracked_allocation);
		memcpy(prefix, info->alloc_prefix, sizeof(info->alloc_prefix));

		char *actual_alloc = prefix + sizeof(info->alloc_prefix);

		char *suffix = actual_alloc + size;
		memcpy(suffix, info->alloc_suffix, sizeof(info->alloc_suffix));

		info->blk = blk;
		info->user_size = size;

		assert_alloc(actual_alloc, size);
		return {actual_alloc, size};
	}

	void deallocate(Blk s) {
		char *actual_alloc = reinterpret_cast<char*>(s.ptr);
		assert_alloc(actual_alloc, s.size);

		char *original_alloc = actual_alloc - sizeof(tracked_allocation) - sizeof(tracked_allocation::alloc_prefix);
		tracked_allocation *info = reinterpret_cast<tracked_allocation*>(original_alloc);

		// take info out of the linked list
		auto *prev = info->prev;
		auto *next = info->next;

		if(next) {
			assert(next->prev == info);
			next->prev = prev;
		}
		if(prev) {
			assert(prev->next == info);
			prev->next = next;
		}

		if(head == info) {
			head = next;
		}
		if(tail == info) {
			tail = prev;
		}

		allocator->deallocate(info->blk);
	}

	void start_tracking() {
		tracking = true;
	}

	tracked_allocation *get_head() {
		return head;
	}

private:
	Allocator *allocator;
	tracked_allocation *head = nullptr;
	tracked_allocation *tail = nullptr;
	bool tracking = false;
};

}
