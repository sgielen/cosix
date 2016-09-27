#pragma once

#include "hw/vga_stream.hpp"

namespace cloudos {

struct global_state;
class allocator;
struct page_allocator;
struct segment_table;
struct driver_store;
struct protocol_store;
struct interface_store;
struct device;
struct scheduler;
struct process_fd;
struct rng;

extern global_state *global_state_;

struct global_state {
	global_state();

	cloudos::allocator *alloc;
	cloudos::page_allocator *page_allocator;
	cloudos::segment_table *gdt; /* for TSS access */
	cloudos::vga_stream *vga;
	cloudos::driver_store *driver_store;
	cloudos::protocol_store *protocol_store;
	cloudos::interface_store *interface_store;
	cloudos::device *root_device;
	cloudos::scheduler *scheduler;
	cloudos::process_fd *init;
	cloudos::rng *random;
};

__attribute__((noreturn)) inline void kernel_panic(const char *message) {
	if(global_state_) {
		*(global_state_->vga) << "!!! KERNEL PANIC - HALTING !!!\n" << message << "\n\n\n";
	}
	asm volatile("cli; halted: hlt; jmp halted;");
	while(1) {}
}

inline allocator *get_allocator() {
	if(!global_state_ || !global_state_->alloc) {
		kernel_panic("No allocator is set");
	}

	return global_state_->alloc;
}

inline page_allocator *get_page_allocator() {
	if(!global_state_ || !global_state_->page_allocator) {
		kernel_panic("No page allocator is set");
	}

	return global_state_->page_allocator;
}

inline segment_table *get_gdt() {
	if(!global_state_ || !global_state_->gdt) {
		kernel_panic("No gdt is set");
	}

	return global_state_->gdt;
}

inline driver_store *get_driver_store() {
	if(!global_state_ || !global_state_->driver_store) {
		kernel_panic("No driver store is set");
	}

	return global_state_->driver_store;
}

inline protocol_store *get_protocol_store() {
	if(!global_state_ || !global_state_->protocol_store) {
		kernel_panic("No protocol store is set");
	}

	return global_state_->protocol_store;
}

inline interface_store *get_interface_store() {
	if(!global_state_ || !global_state_->interface_store) {
		kernel_panic("No interface store is set");
	}

	return global_state_->interface_store;
}

inline vga_stream &get_vga_stream() {
	if(!global_state_ || !global_state_->vga) {
		kernel_panic("No VGA stream is set");
	}

	return *global_state_->vga;
}

inline device *get_root_device() {
	if(!global_state_ || !global_state_->root_device) {
		kernel_panic("No root device is set");
	}

	return global_state_->root_device;
}

inline scheduler *get_scheduler() {
	if(!global_state_ || !global_state_->scheduler) {
		kernel_panic("No scheduler is set");
	}

	return global_state_->scheduler;
}

inline rng *get_random() {
	if(!global_state_ || !global_state_->random) {
		kernel_panic("No RNG is set");
	}

	return global_state_->random;
}

}
