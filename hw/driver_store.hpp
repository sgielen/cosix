#pragma once

#include "global.hpp"
#include "memory/allocator.hpp"
#include "oslibc/list.hpp"

namespace cloudos {

struct driver;

typedef linked_list<driver*> driver_list;

struct driver_store {
	driver_store();

	void register_driver(driver *d);

	inline driver_list *get_drivers() {
		return drivers_;
	}

private:
	driver_list *drivers_;
};

};
