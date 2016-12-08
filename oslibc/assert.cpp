#include <oslibc/assert.hpp>
#include <hw/vga_stream.hpp>
#include <global.hpp>

extern "C"
__attribute__((noreturn)) void assertion_failed(const char *assertion, const char *filename, int lineno, const char *function) {
	auto &stream = cloudos::get_vga_stream();
	stream << "\n\n=============================================\n";
	stream << "Assertion failed: " << assertion << "\n";
	stream << function << "\n";
	stream << "At " << filename << ":" << lineno << "\n";
	cloudos::kernel_panic("Assertion failed, halting.");
}
