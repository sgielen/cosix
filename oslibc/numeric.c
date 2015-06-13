#include "numeric.h"

char *itoa_s(int32_t value, char *buffer, size_t bufsize, int base) {
	return i64toa_s(value, buffer, bufsize, base);
}

char *i64toa_s(int64_t value, char *buffer, size_t bufsize, int base) {
	uint8_t neg = value < 0;
	if(neg) value = -value;
	char *b = ui64toa_s((uint64_t)value, buffer, bufsize, base);
	if(b == NULL || (b == buffer && neg)) {
		return NULL;
	}
	if(neg) {
		b -= 1;
		b[0] = '-';
	}
	return b;
}

char *uitoa_s(uint32_t value, char *buffer, size_t bufsize, int base) {
	return ui64toa_s(value, buffer, bufsize, base);
}

char *ui64toa_s(uint64_t value, char *buffer, size_t bufsize, int base) {
	static const char xlat[] = "0123456789abcdefghijklmnopqrstuvwxyz";
	if(buffer == NULL || bufsize == 0 || base == 0 || base > 36) {
		return NULL;
	}
	size_t i = bufsize;
	buffer[--i] = 0;
	do {
		if(i == 0) {
			return NULL;
		}

		buffer[--i] = xlat[value % base];
	} while(value /= base);

	return buffer + i;
}

