#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

size_t
strlen(const char *s);

size_t
strnlen(const char *s, size_t maxlen);

void *
memset(void *b, int c, size_t len);

void *
memcpy(void *dst, const void *src, size_t n);

void *
memmove(void *dst, const void *src, size_t len);

int
memcmp(const void *left, const void *right, size_t n);

int
strcmp(const char *left, const char *right);

int
strncmp(const char *left, const char *right, size_t n);

char *
strncpy(char *dst, const char *src, size_t n);

char *
strncat(char *s1, const char *s2, size_t n);

size_t
strlcat(char *dst, const char *src, size_t n);

char *
strsplit(char *str, char delim);

inline int
isupper(int c) {
	return (c >= 0x41 && c <= 0x5a) ? 1 : 0;
}

inline int
islower(int c) {
	return (c >= 0x61 && c <= 0x7a) ? 1 : 0;
}

inline int
isalpha(int c) {
	return (isupper(c) || islower(c)) ? 1 : 0;
}

#if defined(__cplusplus)
}
#endif
