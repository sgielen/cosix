#include <oslibc/assert.hpp>
#include <hw/vga_stream.hpp>
#include <global.hpp>

static void stack_up(uintptr_t * &ebp, void * &eip) {
	if(ebp) {
		eip = reinterpret_cast<void*>(*(ebp + 1));
		ebp = reinterpret_cast<uintptr_t*>(*ebp);
	}
}

extern "C"
__attribute__((noreturn)) void assertion_failed(const char *assertion, const char *filename, int lineno, const char *function) {
	auto &stream = cloudos::get_vga_stream();
	stream << "\n\n=============================================\n";
	stream << "Assertion failed: " << assertion << "\n";
	stream << function << "\n";
	stream << "At " << filename << ":" << lineno << "\n";

	uintptr_t *ebp;
	asm volatile("mov %%ebp, %0" : "=r"(ebp));
	void *eip = nullptr;
	for(size_t i = 1; i <= 5; ++i) {
		stack_up(ebp, eip);
		stream << "At " << i << ": " << eip << "\n";
	}

	cloudos::kernel_panic("Assertion failed, halting.");
}
