#include <oslibc/string.h>

size_t
strlen(const char* str) {
	size_t ret = 0;
	while ( str[ret] != 0 )
		ret++;
	return ret;
}

void *
memset(void *b, int c, size_t len) {
	unsigned char *buf = b;
	for(size_t i = 0; i < len; ++i) {
		buf[i] = c;
	}
	return b;
}

void *
memcpy(void *d, const void *s, size_t n) {
	uint8_t *dst = d;
	const uint8_t *src = s;
	for(; n > 0; --n) {
		dst[n-1] = src[n-1];
	}
	return d;
}
