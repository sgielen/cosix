#pragma once

#include <stdint.h>
#include <stddef.h>

namespace cloudos {

/**
 * A cryptographically unsafe RNG, using a linear congruential generator seeded
 * with a constant at boot time.
 */
struct rng {
	void seed(uint32_t);
	uint32_t get();
	void get(char *str, size_t length);

private:
	/* const values taken from glib */
	static const uint32_t a = 1103515245;
	static const uint64_t m = 1ull << 32;
	static const uint32_t c = 12345;
	uint64_t last = 0;
};

}
