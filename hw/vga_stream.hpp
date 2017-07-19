#pragma once

#include <hw/vga.hpp>
#include <oslibc/error.h>
#include <oslibc/utility.hpp>

namespace cloudos {

struct x86_serial;

struct vga_stream {
	vga_stream(vga_buffer &v);

	int base;

	void write(const char*);
	inline void set_serial(x86_serial *s) {
		serial = s;
	}

	inline vga_buffer &get_vga_buffer() {
		return vga_;
	}

private:
	x86_serial *serial = nullptr;
	vga_buffer &vga_;
};

// modifiers
typedef void (*modifier)(vga_stream &);
void bin(vga_stream&);
void dec(vga_stream&);
void hex(vga_stream&);
vga_stream &operator<<(vga_stream &, modifier);

vga_stream &operator<<(vga_stream &, uint8_t);
vga_stream &operator<<(vga_stream &, uint16_t);
vga_stream &operator<<(vga_stream &, uint32_t);
vga_stream &operator<<(vga_stream &, uint64_t);

template <typename enable_if<!is_same<size_t, uint64_t>::value, int>::type = 0>
vga_stream &operator<<(vga_stream &s, size_t sz)
{
	s << static_cast<uint64_t>(sz);
	return s;
}

vga_stream &operator<<(vga_stream &, int8_t);
vga_stream &operator<<(vga_stream &, int16_t);
vga_stream &operator<<(vga_stream &, int32_t);
vga_stream &operator<<(vga_stream &, int64_t);

vga_stream &operator<<(vga_stream &, bool);
vga_stream &operator<<(vga_stream &, char);
vga_stream &operator<<(vga_stream &, const char*);
vga_stream &operator<<(vga_stream &, void*);

}
