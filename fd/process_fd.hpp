#pragma once

#include "fd.hpp"
#include "hw/interrupt.hpp"
#include <oslibc/list.hpp>

namespace cloudos {

struct process_fd;
typedef linked_list<process_fd*> process_list;

struct vga_stream;

class allocator;
struct page_allocator;

/** Process file descriptor
 *
 * This file descriptor contains all information necessary for running a
 * process.
 *
 * A process_fd holds its file descriptors, but does not own them, because they
 * may be shared by multiple processes.
 *
 * A process_fd holds and owns its own page directory and the lower 0x300 page
 * tables. The upper 0x100 page tables are in the page directory, but owned by
 * the page_allocator.
 *
 * The process_fd also holds and owns its kernel and userland stack.
 *
 * When a process FD refcount becomes 0, the process must be exited. This means
 * all FDs in the file descriptor list are de-refcounted (and possibly cleaned
 * up). Also, we must ensure that the process FD does not end up in the
 * ready/blocked list again.
 */
struct process_fd : public fd_t {
	process_fd(page_allocator *alloc, const char *n);

	// TODO remove
	int pid;

	void initialize(void *start_addr, allocator *alloc);
	void set_return_state(interrupt_state_t*);
	void get_return_state(interrupt_state_t*);
	void handle_syscall(vga_stream &stream);
	void *get_kernel_stack_top();
	void install_page_directory();
	uint32_t *get_page_table(int i);

	void map_at(void *kernel_ptr, void *userland_ptr, size_t size);
	int add_fd(fd_t*);
	fd_t *get_fd(int num);

private:
	static const int PAGE_SIZE = 4096 /* bytes */;

	static const int MAX_FD = 256 /* file descriptors */;
	fd_t *fds[MAX_FD];
	int last_fd = -1;

	// Page directory, filled with physical addresses to page tables
	uint32_t *page_directory;
	// The actual backing table virtual addresses; only the first 0x300
	// entries are valid, the others are in page_allocator.kernel_page_tables
	uint32_t **page_tables;

	interrupt_state_t state;
	void *userland_stack_bottom;
	size_t userland_stack_size;
	void *kernel_stack_bottom;
	size_t kernel_stack_size;
};

}
