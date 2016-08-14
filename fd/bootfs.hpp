#pragma once

#include "fd.hpp"

namespace cloudos {

struct bootfs {
	static fd_t *get_root_fd();
};

}
