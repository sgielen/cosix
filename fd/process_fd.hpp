#pragma once

#include "fd.hpp"
#include "mem_mapping.hpp"
#include "thread.hpp"
#include <oslibc/list.hpp>
#include <cloudabi/headers/cloudabi_types.h>

namespace cloudos {

struct process_fd;
typedef linked_list<process_fd*> process_list;

struct vga_stream;

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
 * The process_fd also holds and owns its threads. A process_fd starts without
 * threads, but creates one upon exec(), and copies the calling thread upon
 * fork().
 *
 * When a process FD refcount becomes 0, the process must be exited. This means
 * all FDs in the file descriptor list are de-refcounted (and possibly cleaned
 * up). Also, we must ensure that the process FD does not end up in the
 * ready/blocked list again.
 */
struct process_fd : public fd_t {
	process_fd(const char *n);
	void add_initial_fds();

	void install_page_directory();
	uint32_t *get_page_table(int i);
	uint32_t *ensure_get_page_table(int i);

	// Read an ELF from this fd, map it, and prepare it for execution. This
	// function will not remove previous process contents, use unexec() for
	// that.
	error_t exec(fd_t *, size_t fdslen, fd_mapping_t **new_fds);
	// TODO: make this private
	error_t exec(uint8_t *elf_buffer, size_t size);

	// create a main thread from the given calling thread, belonging to
	// another process (this function assumes its own page directory is
	// loaded)
	void fork(thread *t);

	int add_fd(fd_t*, cloudabi_rights_t rights_base, cloudabi_rights_t rights_inheriting = 0);
	error_t get_fd(fd_mapping_t **mapping, size_t num, cloudabi_rights_t has_rights);

	inline bool is_running() { return running; }
	void exit(cloudabi_exitcode_t exitcode, cloudabi_signal_t exitsignal = 0);
	void signal(cloudabi_signal_t exitsignal);

	// Add the given mem_mapping_t to the page directory and tables, and add
	// it to the list of mappings. If overwrite is false, will kernel_panic()
	// on existing mappings.
	error_t add_mem_mapping(mem_mapping_t *mapping, bool overwrite = false);
	// Unmap the given address range
	void mem_unmap(void *addr, size_t len);

	// Find a piece of the address space that's free to be mapped.
	void *find_free_virtual_range(size_t num_pages);

	/* Add a thread to this process.
	 * auxv_address and entrypoint must already point to valid memory in
	 * this process; stack_address must point just beyond a valid memory region.
	 */
	thread *add_thread(void *stack_address, void *auxv_address, void *entrypoint);

	static const int PAGE_SIZE = 4096 /* bytes */;

private:
	thread_list *threads;
	void add_thread(thread *thr);
	// TODO: for shared mutexes, all cloudabi_tid_t's should be globally
	// unique; we don't have shared mutexes yet
	cloudabi_tid_t last_thread = MAIN_THREAD - 1;

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

	void *elf_phdr = 0;
	size_t elf_phnum = 0;

	bool running = false;
	cloudabi_exitcode_t exitcode = 0;
	cloudabi_signal_t exitsignal = 0;
};

}
