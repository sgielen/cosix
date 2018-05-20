#pragma once

#include <oslibc/error.h>
#include <oslibc/list.hpp>
#include <memory/smart_ptr.hpp>

namespace cloudos {

struct blockdev;

typedef linked_list<shared_ptr<blockdev>> blockdev_list;

struct blockdev_store
{
	blockdev_store();

	shared_ptr<blockdev> get_blockdev(const char *name);
	cloudabi_errno_t register_blockdev(shared_ptr<blockdev> i, const char *prefix);
	cloudabi_errno_t register_blockdev_fixed_name(shared_ptr<blockdev> i, const char *name);

	inline blockdev_list *get_blockdevs() {
		return blockdevs_;
	}

private:
	blockdev_list *blockdevs_;
};

}
