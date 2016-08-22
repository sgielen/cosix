#pragma once

#include "fd.hpp"
#include "mem_mapping.hpp"
#include "hw/interrupt.hpp"
#include <oslibc/list.hpp>
#include <cloudabi/headers/cloudabi_types.h>

namespace cloudos {

struct process_fd;
typedef linked_list<process_fd*> process_list;

struct vga_stream;

typedef uint8_t sse_state_t [512] __attribute__ ((aligned (16)));

struct fd_mapping_t {
	fd_t *fd; /* can be 0, in this case, the mapping is unused and can be reused for another fd */
	cloudabi_rights_t rights_base;
	cloudabi_rights_t rights_inheriting;
};

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
	process_fd(const char *n);

	// TODO remove
	int pid;

	void set_return_state(interrupt_state_t*);
	void get_return_state(interrupt_state_t*);
	void handle_syscall(vga_stream &stream);
	void *get_kernel_stack_top();
	void *get_fsbase();
	void install_page_directory();
	uint32_t *get_page_table(int i);
	uint32_t *ensure_get_page_table(int i);

	// Read an ELF from this fd, map it, and prepare it for execution. This
	// function will not remove previous process contents, use unexec() for
	// that.
	error_t exec(fd_t *);
	// TODO: make this private
	error_t exec(uint8_t *elf_buffer, size_t size);

	int add_fd(fd_t*, cloudabi_rights_t rights_base, cloudabi_rights_t rights_inheriting = 0);
	error_t get_fd(fd_mapping_t **mapping, size_t num, cloudabi_rights_t has_rights);

	void save_sse_state();
	void restore_sse_state();

	inline bool is_running() { return running; }
	void exit(cloudabi_exitcode_t exitcode, cloudabi_signal_t exitsignal = 0);
	void signal(cloudabi_signal_t exitsignal);

	static const int PAGE_SIZE = 4096 /* bytes */;

private:
	void initialize(void *start_addr);

	static const int PAGE_DIRECTORY_SIZE = 1024 /* entries */;

	static const int MAX_FD = 256 /* file descriptors */;
	fd_mapping_t *fds[MAX_FD];
	int last_fd = -1;

	// Page directory, filled with physical addresses to page tables
	uint32_t *page_directory = 0;
	// The actual backing table virtual addresses; only the first 0x300
	// entries are valid, the others are in page_allocator.kernel_page_tables
	uint32_t **page_tables = 0;

	// The memory mappings used by this process.
	mem_mapping_list *mappings = 0;
	// Add the given mem_mapping_t to the page directory and tables, and add
	// it to the list of mappings. If overwrite is false, will kernel_panic()
	// on existing mappings.
	error_t add_mem_mapping(mem_mapping_t *mapping, bool overwrite = false);
	// Find a piece of the address space that's free to be mapped.
	void *find_free_virtual_range(size_t num_pages);

	interrupt_state_t state;
	sse_state_t sse_state;
	size_t userland_stack_size = 0;
	void *userland_stack_address = 0;
	void *kernel_stack_bottom = 0;
	size_t kernel_stack_size = 0;

	void *elf_phdr = 0;
	size_t elf_phnum = 0;

	bool running = false;
	cloudabi_exitcode_t exitcode = 0;
	cloudabi_signal_t exitsignal = 0;
};

}
