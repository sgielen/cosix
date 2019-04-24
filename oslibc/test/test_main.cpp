#define CATCH_CONFIG_RUNNER
#include <catch.hpp>
#include "global.hpp"

namespace cloudos {
cloudos::global_state *global_state_;
}

cloudos::global_state::global_state() {
	memset(this, 0, sizeof(*this));
}

int main(int argc, char *argv[]) {
	cloudos::global_state global;
	cloudos::global_state_ = &global;
	return Catch::Session().run(argc, argv);
}
