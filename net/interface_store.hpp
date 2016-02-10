#pragma once

#include "oslibc/error.h"
#include "oslibc/list.hpp"

namespace cloudos {

struct interface;

typedef linked_list<interface*> interface_list;

struct interface_store;
struct vga_stream;

void dump_interfaces(vga_stream &stream, interface_store *device);

struct interface_store
{
	interface_store();

	interface *get_interface(const char *name);
	error_t register_interface(interface *i, const char *prefix);
	error_t register_interface_fixed_name(interface *i, const char *name);

	inline interface_list *get_interfaces() {
		return interfaces_;
	}

private:
	interface_list *interfaces_;
};

}
