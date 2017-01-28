#pragma once

#include <stdint.h>
#include <oslibc/assert.hpp>

namespace cloudos {

struct syscall_context {
	syscall_context(void *e) : esp(reinterpret_cast<char*>(e)) {}

	template <typename T>
	T &get_from_stack(uint8_t offset) {
		return *reinterpret_cast<T*>(esp - offset);
	}

	uint64_t result = 0;

private:
	char *esp;
};

template <typename first_t, typename second_t = void, typename third_t = void, typename fourth_t = void,
          typename fifth_t = void, typename sixth_t = void, typename seventh_t = void>
struct arguments_t {
	arguments_t(syscall_context &c) {
		uint8_t offset = 8;
#define FM(n)   fill_member(c, offset, & n##_, reinterpret_cast<n##_t*>(0))
		FM(first);
		FM(second);
		FM(third);
		FM(fourth);
		FM(fifth);
		FM(sixth);
		FM(seventh);
	}

	first_t first() {
		return *reinterpret_cast<first_t*>(first_);
	}

	second_t second() {
		return *reinterpret_cast<second_t*>(second_);
	}

	third_t third() {
		return *reinterpret_cast<third_t*>(third_);
	}

	fourth_t fourth() {
		return *reinterpret_cast<fourth_t*>(fourth_);
	}

	fifth_t fifth() {
		return *reinterpret_cast<fifth_t*>(fifth_);
	}

	sixth_t sixth() {
		return *reinterpret_cast<sixth_t*>(sixth_);
	}

	seventh_t seventh() {
		return *reinterpret_cast<seventh_t*>(seventh_);
	}

private:
	// storage
	typedef char storage_t[8];
	storage_t first_;
	storage_t second_;
	storage_t third_;
	storage_t fourth_;
	storage_t fifth_;
	storage_t sixth_;
	storage_t seventh_;

	// load from the stack into storage
	void fill_member(syscall_context &c, uint8_t &offset, void *member, uint32_t*) {
		*reinterpret_cast<uint32_t*>(member) = c.get_from_stack<uint32_t>(offset);
		offset += sizeof(uint32_t);
	}

	void fill_member(syscall_context &c, uint8_t &offset, void *member, uint64_t*) {
		uint64_t lower = c.get_from_stack<uint32_t>(offset);
		uint64_t upper = c.get_from_stack<uint32_t>(offset + 16);
		*reinterpret_cast<uint64_t*>(member) = upper << 32 | lower;
		offset += 20;
	}

	template <typename T>
	void fill_member(syscall_context &c, uint8_t &offset, void *member, T**) {
		assert(sizeof(void*) == sizeof(uint32_t));
		fill_member(c, offset, member, reinterpret_cast<uint32_t*>(0));
	}

	void fill_member(syscall_context&, uint8_t&, void *, void*) {}
};

}
