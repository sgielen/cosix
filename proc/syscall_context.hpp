#pragma once

#include <stdint.h>
#include <oslibc/assert.hpp>
#include <fd/thread.hpp>

namespace cloudos {

struct thread;
struct process_fd;

struct syscall_context {
	syscall_context(thread *t, void *e) : thread(t), esp(reinterpret_cast<char*>(e)) {}

	template <typename T>
	T &get_from_stack(uint8_t offset) {
		return *reinterpret_cast<T*>(esp + offset);
	}

	void set_results(uint32_t a, uint32_t b) {
		result = a;
		result = result << 32 | b;
	}

	thread *thread = nullptr;
	uint64_t result = 0;

	auto process() -> decltype(thread->get_process()) {
		return thread->get_process();
	}

private:
	char *esp;
};

struct no_argument_tag {};

template <typename first_t, typename second_t = no_argument_tag, typename third_t = no_argument_tag,
          typename fourth_t = no_argument_tag, typename fifth_t = no_argument_tag,
          typename sixth_t = no_argument_tag, typename seventh_t = no_argument_tag>
struct arguments_t {
	arguments_t(syscall_context &c) {
		uint8_t offset = 4;
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
	void fill_member(syscall_context &c, uint8_t &offset, void *member, uint8_t*) {
		*reinterpret_cast<uint8_t*>(member) = c.get_from_stack<uint8_t>(offset);
		offset += sizeof(uint32_t);
	}

	void fill_member(syscall_context &c, uint8_t &offset, void *member, int8_t*) {
		*reinterpret_cast<int8_t*>(member) = c.get_from_stack<int8_t>(offset);
		offset += sizeof(uint32_t);
	}

	void fill_member(syscall_context &c, uint8_t &offset, void *member, uint16_t*) {
		*reinterpret_cast<uint16_t*>(member) = c.get_from_stack<uint16_t>(offset);
		offset += sizeof(uint32_t);
	}

	void fill_member(syscall_context &c, uint8_t &offset, void *member, int16_t*) {
		*reinterpret_cast<int16_t*>(member) = c.get_from_stack<int16_t>(offset);
		offset += sizeof(uint32_t);
	}

	void fill_member(syscall_context &c, uint8_t &offset, void *member, uint32_t*) {
		*reinterpret_cast<uint32_t*>(member) = c.get_from_stack<uint32_t>(offset);
		offset += sizeof(uint32_t);
	}

	void fill_member(syscall_context &c, uint8_t &offset, void *member, int32_t*) {
		*reinterpret_cast<int32_t*>(member) = c.get_from_stack<int32_t>(offset);
		offset += sizeof(uint32_t);
	}

	void fill_member(syscall_context &c, uint8_t &offset, void *member, uint64_t*) {
		*reinterpret_cast<uint64_t*>(member) = c.get_from_stack<uint64_t>(offset);
		offset += sizeof(uint64_t);
	}

	void fill_member(syscall_context &c, uint8_t &offset, void *member, int64_t*) {
		*reinterpret_cast<int64_t*>(member) = c.get_from_stack<int64_t>(offset);
		offset += sizeof(uint64_t);
	}

	void fill_member(syscall_context &c, uint8_t &offset, void *member, cloudabi_lookup_t*) {
		*reinterpret_cast<cloudabi_lookup_t*>(member) = c.get_from_stack<cloudabi_lookup_t>(offset);
		offset += sizeof(cloudabi_lookup_t);
	}

	template <typename T>
	void fill_member(syscall_context &c, uint8_t &offset, void *member, T**) {
		assert(sizeof(void*) == sizeof(uint32_t));
		fill_member(c, offset, member, reinterpret_cast<uint32_t*>(0));
	}

	void fill_member(syscall_context&, uint8_t&, void *, no_argument_tag*) {}
};

}
