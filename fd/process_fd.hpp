#include "fd.hpp"
#include "hw/interrupt.hpp"

namespace cloudos {

struct vga_stream;

class allocator;

/** Process file descriptor
 *
 * This file descriptor contains all information necessary for running a process.
 */
struct process_fd : public fd {
	/* TODO remove */
	inline process_fd() : process_fd("DefaultProcess") {}

	inline process_fd(const char *n) : fd(fd_type_t::process, n) {}

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

private:
	/* file descriptor mapping to global FD pointers... */
	/* memory map... */
	/* execution state... */
	int pid;
	interrupt_state_t state;
	void *userland_stack_bottom;
	size_t userland_stack_size;
	void *kernel_stack_bottom;
	size_t kernel_stack_size;
};

}
