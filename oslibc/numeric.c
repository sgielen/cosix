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
	if(buffer == NULL || bufsize == 0 || base <= 0 || base > 36) {
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

bool atoi_s(const char *str, int32_t *value, int base) {
	int64_t value64;
	if(!atoi64_s(str, &value64, base)) {
		return false;
	}
	if(value64 > 0x7fffffff || value64 < -0x7fffffff) {
		// wouldn't fit
		return false;
	}
	*value = value64;
	return true;
}

bool atoi64_s(const char *str, int64_t *value, int base) {
	if(str == NULL || value == NULL || base <= 0 || base > 36) {
		return false;
	}
	bool negative = false;
	if(*str == '-') {
		negative = true;
		str++;
	}
	if(*str == 0) {
		return false;
	}
	*value = 0;
	while(*str != 0) {
		int64_t oldvalue = *value;
		*value *= base;
		char digit = *str;
		if(digit >= 0x61) {
			digit -= 0x57;
		} else if(digit >= 0x41) {
			digit -= 0x37;
		} else if(digit >= 0x30) {
			digit -= 0x30;
		} else {
			return false;
		}
		if(digit >= base) {
			return false;
		}
		*value += digit;
		if(*value < oldvalue) {
			// value went out of bounds, kill it
			return false;
		}
		str++;
	}
	if(negative) {
		*value = -*value;
	}
	return true;
}
