#include "hw/vga_stream.hpp"
#include "oslibc/numeric.h"
#include <hw/arch/x86/x86_serial.hpp>

using cloudos::vga_stream;

vga_stream::vga_stream(vga_buffer &v)
: base(10)
, vga_(v)
{}

void vga_stream::write(const char *s) {
	// TODO hack: everything written to the vga_stream is also
	// written to the serial device, if any
	if(serial) {
		serial->transmit_string(1, s);
	}
	return vga_.write(s);
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
		s.write("-"); \
		value = -value; \
	} \
	s.write(PRODUCER(value, &buf[0], sizeof(buf), s.base)); \
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
	s.write(val ? "true" : "false");
	return s;
}

vga_stream &cloudos::operator<<(vga_stream &s, char val) {
	char buf[2];
	buf[0] = val;
	buf[1] = 0;
	s.write(buf);
	return s;
}

vga_stream &cloudos::operator<<(vga_stream &s, const char *str) {
	s.write(str == NULL ? "(null)" : str);
	return s;
}

vga_stream &cloudos::operator<<(vga_stream &s, void *ptr) {
	if(ptr == NULL) {
		s.write("(null)");
	} else {
		uint64_t addr = reinterpret_cast<uint64_t>(ptr);
		char buf[16];
		s.write("0x");
		s.write(ui64toa_s(addr, &buf[0], sizeof(buf), 16));
	}
	return s;
}
