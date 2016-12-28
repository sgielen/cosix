#include "global.hpp"

void operator delete (void *ptr) noexcept {
	if(ptr != nullptr) {
		cloudos::kernel_panic("operator delete called, but you should use deallocate() instead of delete.");
	}
}

extern "C"
void __cxa_pure_virtual(void) {
	cloudos::kernel_panic("Pure virtual method called");
}
