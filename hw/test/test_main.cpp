#define CATCH_CONFIG_RUNNER
#include <catch.hpp>
#include "global.hpp"

namespace cloudos {
cloudos::global_state *global_state_;
}

uint32_t _kernel_virtual_base = 0;

int main(int argc, char *argv[]) {
	cloudos::global_state_ = nullptr;
	return Catch::Session().run(argc, argv);
}
