#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * All these functions convert a numeric value to a string in given base.
 * They return a ptr to somewhere inside buffer, which is assumed to be
 * of size bufsize. If the given value does not fit inside the given buffer,
 * NULL is returned.
 */

char *itoa_s(int32_t value, char *buffer, size_t bufsize, int base);
char *i64toa_s(int64_t value, char *buffer, size_t bufsize, int base);
char *uitoa_s(uint32_t value, char *buffer, size_t bufsize, int base);
char *ui64toa_s(uint64_t value, char *buffer, size_t bufsize, int base);

#if defined(__cplusplus)
}
#endif
