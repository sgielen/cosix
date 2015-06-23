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

#if defined(__cplusplus)
}
#endif
