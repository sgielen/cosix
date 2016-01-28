#include "global.hpp"

void operator delete (void*) noexcept {
	// TODO(sjors): implement as soon as we have allocations
	// C++ destructors need this operator to be around, even if
	// it won't be used
}

extern "C"
void __cxa_pure_virtual(void) {
	cloudos::kernel_panic("Pure virtual method called");
}
