#pragma once

#include "global.hpp"
#include "oslibc/list.hpp"
#include "memory/smart_ptr.hpp"

namespace cloudos {

struct process_fd;

typedef linked_list<weak_ptr<process_fd>> process_weaklist;

struct process_store {
	process_store();

	void register_process(shared_ptr<process_fd> f);

	inline process_weaklist *get_processes() {
		return processes_;
	}

	// 16 bytes are read from the pid ptr
	shared_ptr<process_fd> find_process(uint8_t const *pid);

private:
	process_weaklist *processes_;
};

};
