#pragma once

#include "fd.hpp"

namespace cloudos {

struct procfs {
	static shared_ptr<fd_t> get_root_fd();

private:
};

}
