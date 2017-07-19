#pragma once

#include <oslibc/list.hpp>
#include <cloudabi_types.h>

namespace cloudos {

struct terminal;

typedef linked_list<terminal*> terminal_list;

struct terminal_store
{
	terminal_store();

	terminal *get_terminal(const char *name);
	cloudabi_errno_t register_terminal(terminal *i);

	inline terminal_list *get_terminals() {
		return terminals_;
	}

private:
	terminal_list *terminals_;
};

}
