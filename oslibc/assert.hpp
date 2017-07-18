#pragma once

#ifdef TESTING_ENABLED
#include <cassert>
#else

#ifdef NDEBUG
#define assert(x)
#else
#define assert(x) (void)((x) || (::assertion_failed(#x, __FILE__, __LINE__, __PRETTY_FUNCTION__),0))
#endif

#define UNUSED(x) ((void)(x));

extern "C"
__attribute__((noreturn)) void assertion_failed(const char *assertion, const char *filename, int lineno, const char *function);

#endif
