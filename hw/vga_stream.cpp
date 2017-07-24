#include <hw/arch/x86/x86_serial.hpp>
#include <hw/vga_stream.hpp>
#include <oslibc/numeric.h>
#include <oslibc/utility.hpp>

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

template <typename T>
static void write_integral_to_stream(vga_stream &s, T value) {
	char buf[64];
	char *ptr = nullptr;
	if(cloudos::is_unsigned<T>::value) {
		ptr = ui64toa_s(value, &buf[0], sizeof(buf), s.base);
	} else {
		ptr = i64toa_s(value, &buf[0], sizeof(buf), s.base);
	}
	s.write(ptr);
}

#define TO_STREAM(TYPE) \
vga_stream &cloudos::operator<<(vga_stream &s, TYPE value) { \
	write_integral_to_stream(s, value); \
	return s; \
}

TO_STREAM(signed char);
TO_STREAM(short int);
TO_STREAM(int);
TO_STREAM(long int);
TO_STREAM(long long int);

TO_STREAM(unsigned char);
TO_STREAM(unsigned short int);
TO_STREAM(unsigned int);
TO_STREAM(unsigned long int);
TO_STREAM(unsigned long long int);

#undef TO_STREAM

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
	s.write(str == nullptr ? "(null)" : str);
	return s;
}

vga_stream &cloudos::operator<<(vga_stream &s, void *ptr) {
	if(ptr == nullptr) {
		s.write("(null)");
	} else {
		uint64_t addr = reinterpret_cast<uint64_t>(ptr);
		char buf[16];
		s.write("0x");
		s.write(ui64toa_s(addr, &buf[0], sizeof(buf), 16));
	}
	return s;
}
