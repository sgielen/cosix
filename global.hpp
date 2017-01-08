#pragma once

#include "hw/vga_stream.hpp"
#include "oslibc/assert.hpp"

namespace cloudos {

struct global_state;
class allocator;
struct page_allocator;
struct map_virtual;
struct segment_table;
struct interrupt_handler;
struct driver_store;
struct protocol_store;
struct interface_store;
struct device;
struct scheduler;
struct process_fd;
struct rng;
struct clock_store;

extern global_state *global_state_;

struct global_state {
	global_state();

	cloudos::allocator *alloc;
	cloudos::page_allocator *page_allocator;
	cloudos::map_virtual *map_virtual;
	cloudos::segment_table *gdt; /* for TSS access */
	cloudos::interrupt_handler *interrupt_handler;
	cloudos::vga_stream *vga;
	cloudos::driver_store *driver_store;
	cloudos::protocol_store *protocol_store;
	cloudos::interface_store *interface_store;
	cloudos::device *root_device;
	cloudos::scheduler *scheduler;
	cloudos::process_fd *init;
	cloudos::rng *random;
	cloudos::clock_store *clock_store;
};

__attribute__((noreturn)) inline void kernel_panic(const char *message) {
	if(global_state_ && global_state_->vga) {
		*(global_state_->vga) << "!!! KERNEL PANIC - HALTING !!!\n" << message << "\n\n\n";
	}
	asm volatile("cli; halted: hlt; jmp halted;");
	while(1) {}
}

#define GET_GLOBAL(NAME, TYPE, MEMBER) \
inline TYPE *get_##NAME() { \
	assert(global_state_ && global_state_->MEMBER); \
	return global_state_->MEMBER; \
}

GET_GLOBAL(allocator, allocator, alloc)
GET_GLOBAL(page_allocator, page_allocator, page_allocator)
GET_GLOBAL(map_virtual, map_virtual, map_virtual)
GET_GLOBAL(gdt, segment_table, gdt)
GET_GLOBAL(interrupt_handler, interrupt_handler, interrupt_handler);
GET_GLOBAL(driver_store, driver_store, driver_store)
GET_GLOBAL(protocol_store, protocol_store, protocol_store)
GET_GLOBAL(interface_store, interface_store, interface_store)
GET_GLOBAL(root_device, device, root_device)
GET_GLOBAL(scheduler, scheduler, scheduler)
GET_GLOBAL(random, rng, random)
GET_GLOBAL(clock_store, clock_store, clock_store);

inline vga_stream &get_vga_stream() {
	assert(global_state_ && global_state_->vga);
	return *global_state_->vga;
}

}
