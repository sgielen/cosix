#pragma once

#include "fd.hpp"

namespace cloudos {

struct procfs {
	static fd_t *get_root_fd();

private:
};

}
