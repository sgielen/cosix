#pragma once

#include "fd.hpp"

namespace cloudos {

struct bootfs {
	static shared_ptr<fd_t> get_root_fd();
};

}
