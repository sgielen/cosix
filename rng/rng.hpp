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
	/* m and c must be relative primes (gcd is 1), all prime factors of m
	 * divide a - 1; these values are taken from glibc
	 */
	static const uint32_t a = 1103515245;
	static const uint64_t m = 1ull << 32;
	static const uint32_t c = 12345;
	uint64_t last = 0;
};

}
