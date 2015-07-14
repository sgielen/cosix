#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

size_t
strlen(const char *s);

void *
memset(void *b, int c, size_t len);

void *
memcpy(void *dst, const void *src, size_t n);

#if defined(__cplusplus)
}
#endif
