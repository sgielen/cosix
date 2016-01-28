#pragma once

#include "hw/vga.hpp"

namespace cloudos {

struct vga_stream {
	vga_stream(vga_buffer &v);

	int base;

	vga_buffer &vga();

private:
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

vga_stream &operator<<(vga_stream &, int8_t);
vga_stream &operator<<(vga_stream &, int16_t);
vga_stream &operator<<(vga_stream &, int32_t);
vga_stream &operator<<(vga_stream &, int64_t);

vga_stream &operator<<(vga_stream &, bool);
vga_stream &operator<<(vga_stream &, char);
vga_stream &operator<<(vga_stream &, const char*);
vga_stream &operator<<(vga_stream &, void*);

}
