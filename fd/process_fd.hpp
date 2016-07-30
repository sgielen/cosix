#include "fd.hpp"
#include "hw/interrupt.hpp"

namespace cloudos {

struct vga_stream;

class allocator;
struct page_allocator;

/** Process file descriptor
 *
 * This file descriptor contains all information necessary for running a process.
 */
struct process_fd : public fd {
	process_fd(page_allocator *alloc, const char *n);

	/* When a process FD refcount becomes 0, the process must be exited.
	 * This means all FDs in the file descriptor list are de-refcounted
	 * (and possibly cleaned up). Also, we must ensure that the process FD
	 * does not end up in the ready/blocked list again.
	 */

	void initialize(int pid, void *start_addr, allocator *alloc);
	void set_return_state(interrupt_state_t*);
	void get_return_state(interrupt_state_t*);
	void handle_syscall(vga_stream &stream);
	void *get_kernel_stack_top();
	void install_page_directory();
	uint32_t *get_page_table(int i);

private:

	/* file descriptor mapping to global FD pointers... */

	int pid;
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