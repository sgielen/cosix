#pragma once
#include <global.hpp>
#include <rng/rng.hpp>
#include <oslibc/numeric.h>
#include <oslibc/ctype.h>

#define UUID_LEN 36

namespace cloudos {

inline void generate_random_uuid(uint8_t *buf, size_t bufsize) {
	assert(bufsize == 16);
	get_random()->get(reinterpret_cast<char*>(buf), bufsize);
	buf[6] = (buf[6] & 0x0f) | 0x40;
	buf[8] = (buf[8] & 0x3f) | 0x80;
}

inline void uuid_to_string(const uint8_t *uuid, size_t uuidsize, char *buf, size_t bufsize) {
	assert(uuidsize == 16);
	assert(bufsize > UUID_LEN);
	size_t bufptr = 0;
	char b[8];
	for(uint8_t i = 0; i < 16; ++i) {
		if (i == 4 || i == 6 || i == 8 || i == 10) {
			buf[bufptr++] = '-';
		}
		char *p = uitoa_s(uuid[i], b, sizeof(b), 16);
		assert(p[0] != 0);
		if (p[1] == 0) {
			buf[bufptr++] = '0';
			buf[bufptr++] = p[0];
		} else {
			buf[bufptr++] = p[0];
			buf[bufptr++] = p[1];
			assert(p[2] == 0);
		}
	}
	assert(bufptr == UUID_LEN);
	buf[bufptr++] = 0;
	assert(bufptr <= bufsize);
}

inline bool string_to_uuid(const char *buf, size_t bufsize, uint8_t *uuid, size_t uuidsize) {
	if (bufsize < UUID_LEN) {
		return false;
	}
	assert(uuidsize == 16);
	size_t bufptr = 0;
	char b[3];
	b[2] = 0;
	for(uint8_t i = 0; i < 16; ++i) {
		if (i == 4 || i == 6 || i == 8 || i == 10) {
			if (buf[bufptr++] != '-') {
				return false;
			}
		}
		b[0] = buf[bufptr++];
		b[1] = buf[bufptr++];
		if (!isalpha(b[0]) && !isdigit(b[0])) {
			return false;
		}
		if (!isalpha(b[1]) && !isdigit(b[1])) {
			return false;
		}
		int32_t value;
		if (!atoi_s(b, &value, 16)) {
			return false;
		}
		uuid[i] = value & 0xff;
	}
	assert(bufptr == UUID_LEN);
	assert(bufptr <= bufsize);
	return bufsize == UUID_LEN || buf[bufptr] == 0;
}

}
