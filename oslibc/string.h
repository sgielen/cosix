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

int
memcmp(const void *left, const void *right, size_t n);

int
strcmp(const char *left, const char *right);

char *
strncpy(char *dst, const char *src, size_t n);

char *
strncat(char *s1, const char *s2, size_t n);

size_t
strlcat(char *dst, const char *src, size_t n);

char *
strsplit(char *str, char delim);

#if defined(__cplusplus)
}
#endif
