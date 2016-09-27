#include "rng.hpp"
#include <oslibc/string.h>

using namespace cloudos;

void rng::seed(uint32_t seed) {
	last = seed;
}

uint32_t rng::get() {
	last = (a * last + c) % m;
	return last;
}

void rng::get(char *str, size_t length) {
	while(length > 0) {
		auto val = get();
		size_t copy = sizeof(val) < length ? sizeof(val) : length;
		memcpy(str, &val, copy);
		str += copy;
		length -= copy;
	}
}

