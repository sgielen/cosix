#pragma once

namespace cloudos {

struct global_state;
class allocator;
struct segment_table;

extern global_state *global_state_;

struct global_state {
	global_state();

	cloudos::allocator *alloc;
	cloudos::segment_table *gdt; /* for TSS access */
	cloudos::vga_stream *vga;
};

}
