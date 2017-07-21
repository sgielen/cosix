#pragma once

#include <cloudabi_types.h>
#include <fd/fd.hpp>
#include <memory/smart_ptr.hpp>
#include <oslibc/list.hpp>

namespace cloudos {

struct terminal;

typedef linked_list<shared_ptr<terminal>> terminal_list;

struct terminal_store
{
	terminal_store();

	shared_ptr<terminal> get_terminal(const char *name);
	cloudabi_errno_t register_terminal(shared_ptr<terminal> i);

	shared_ptr<fd_t> get_root_fd();

	inline terminal_list *get_terminals() {
		return terminals_;
	}

private:
	terminal_list *terminals_;
};

}
