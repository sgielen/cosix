#pragma once

#include "fd.hpp"

namespace cloudos {

struct initrdfs {
	initrdfs(multiboot_module *initrd);

	shared_ptr<fd_t> get_root_fd();

private:
	uint8_t *initrd_start = nullptr;
	size_t initrd_size = 0;
};

}
