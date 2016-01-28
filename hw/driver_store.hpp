#pragma once

#include "global.hpp"
#include "memory/allocator.hpp"

namespace cloudos {

struct driver;

struct driver_list {
	driver *driver;
	driver_list *next;
};

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
