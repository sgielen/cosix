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

vga_stream &operator<<(vga_stream &, signed char);
vga_stream &operator<<(vga_stream &, short int);
vga_stream &operator<<(vga_stream &, int);
vga_stream &operator<<(vga_stream &, long int);
vga_stream &operator<<(vga_stream &, long long int);

vga_stream &operator<<(vga_stream &, unsigned char);
vga_stream &operator<<(vga_stream &, unsigned short int);
vga_stream &operator<<(vga_stream &, unsigned int);
vga_stream &operator<<(vga_stream &, unsigned long int);
vga_stream &operator<<(vga_stream &, unsigned long long int);

vga_stream &operator<<(vga_stream &, bool);
vga_stream &operator<<(vga_stream &, char);
vga_stream &operator<<(vga_stream &, const char*);
vga_stream &operator<<(vga_stream &, void*);

}
