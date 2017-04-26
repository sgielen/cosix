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

int
memcmp(const void *l, const void *r, size_t n) {
	const uint8_t *left = l;
	const uint8_t *right = r;
	while(n > 0) {
		if(*left < *right) return -1;
		if(*left > *right) return 1;
		left++;
		right++;
		n--;
	}
	return 0;
}

int
strcmp(const char *left, const char *right) {
	while(1) {
		if(*left == 0 && *right == 0) return 0;
		if(*left < *right) return -1;
		if(*left > *right) return 1;
		left++;
		right++;
	}
}

char *
strncpy(char *dst, const char *src, size_t n) {
	size_t i;
	for(i = 0; i < n && src[i] != 0; ++i) {
		dst[i] = src[i];
	}
	for(; i < n; ++i) {
		dst[i] = 0;
	}
	return dst;
}


char *
strncat(char *s1, const char *s2, size_t n) {
	size_t s1len = strlen(s1);
	strncpy(s1 + s1len, s2, n);
	return s1;
}

size_t
strlcat(char *dst, const char *src, size_t n) {
	size_t dstlen = strlen(dst);
	size_t srclen = strlen(src);

	size_t to_copy = n - dstlen; /* including space for nullbyte */
	if(to_copy > srclen + 1 /* nullbyte */) {
		to_copy = srclen + 1;
	}

	for(size_t i = 0; i < to_copy; ++i) {
		dst[dstlen + i] = src[i];
	}
	dst[dstlen + to_copy - 1] = 0;

	// return 'intended' size of the new string
	return dstlen + srclen;
}

char *
strsplit(char *str, char delim) {
	if(str == 0) {
		return 0;
	}
	for(char *res = str; res[0] != 0; ++res) {
		if(res[0] == delim) {
			res[0] = 0; /* end of str */
			return res + 1 /* beginning of res */;
		}
	}
	return 0;
}
