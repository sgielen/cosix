#include "hw/vga_stream.hpp"
#include "oslibc/numeric.h"

using cloudos::vga_stream;

vga_stream::vga_stream(vga_buffer &v)
: base(10)
, vga_(v)
{}

cloudos::vga_buffer &vga_stream::vga() {
	return vga_;
}

// modifiers
void cloudos::bin(vga_stream &s) {
	s.base = 2;
}
void cloudos::dec(vga_stream &s) {
	s.base = 10;
}
void cloudos::hex(vga_stream &s) {
	s.base = 16;
}

vga_stream &cloudos::operator<<(vga_stream &s, cloudos::modifier m)
{
	m(s);
	return s;
}

//! Define an operator<< for given type. Producer must be a method with prototype
//!   char * PRODUCER(TYPE, char*, size_t, int)
//! MAXLENGTH should be the length of the largest character array required for
//! fulfilling this PRODUCER call correctly in base 2.
#define stream_using_producer(TYPE, PRODUCER, MAXLENGTH) \
vga_stream &cloudos::operator<<(vga_stream &s, TYPE value) { \
	char buf[MAXLENGTH]; \
	if(value < 0) { \
		s.vga().putc('-'); \
		value = -value; \
	} \
	s.vga().write(PRODUCER(value, &buf[0], sizeof(buf), s.base)); \
	return s; \
}

stream_using_producer(uint8_t, uitoa_s, 8);
stream_using_producer(uint16_t, uitoa_s, 16);
stream_using_producer(uint32_t, uitoa_s, 32);
stream_using_producer(uint64_t, ui64toa_s, 64);

stream_using_producer(int8_t, itoa_s, 8);
stream_using_producer(int16_t, itoa_s, 16);
stream_using_producer(int32_t, itoa_s, 32);
stream_using_producer(int64_t, i64toa_s, 64);

#undef stream_using_producer

vga_stream &cloudos::operator<<(vga_stream &s, bool val) {
	s.vga().write(val ? "true" : "false");
	return s;
}

vga_stream &cloudos::operator<<(vga_stream &s, char val) {
	s.vga().putc(val);
	return s;
}

vga_stream &cloudos::operator<<(vga_stream &s, const char *str) {
	s.vga().write(str == NULL ? "(null)" : str);
	return s;
}

vga_stream &cloudos::operator<<(vga_stream &s, void *ptr) {
	if(ptr == NULL) {
		s.vga().write("(null)");
	} else {
		uint64_t addr = reinterpret_cast<uint64_t>(ptr);
		char buf[16];
		s.vga().write("0x");
		s.vga().write(ui64toa_s(addr, &buf[0], sizeof(buf), 16));
	}
	return s;
}
