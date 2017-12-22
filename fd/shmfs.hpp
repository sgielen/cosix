#pragma once

#include "fd.hpp"

namespace cloudos {

struct shmfs {
	shmfs(cloudabi_device_t device = 0);

	shared_ptr<fd_t> get_shm();

private:
	cloudabi_device_t device;
};

}
