#pragma once

namespace cloudos {
__attribute__((noreturn)) void kernel_panic(const char *message);
inline vga_stream &get_vga_stream();
}

#ifdef NDEBUG
#define assert(x)
#else
#define assert(x) (void)((x) || (::assertion_failed(#x, __FILE__, __LINE__, __PRETTY_FUNCTION__),0))
#endif

extern "C"
__attribute__((noreturn)) inline void assertion_failed(const char *assertion, const char *filename, int lineno, const char *function) {
	auto &stream = cloudos::get_vga_stream();
	stream << "\n\n=============================================\n";
	stream << "Assertion failed: " << assertion << "\n";
	stream << function << "\n";
	stream << "At " << filename << ":" << lineno << "\n";
	cloudos::kernel_panic("Assertion failed, halting.");
}
