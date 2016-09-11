#include "thread.hpp"
#include "process_fd.hpp"
#include "scheduler.hpp"
#include "pipe_fd.hpp"
#include <memory/page_allocator.hpp>
#include <memory/allocator.hpp>
#include "global.hpp"

using namespace cloudos;

extern uint32_t _kernel_virtual_base;
extern uint32_t initial_kernel_stack;
extern uint32_t initial_kernel_stack_size;

template <typename T>
static inline T *allocate_on_stack(uint32_t &useresp) {
	useresp -= sizeof(T);
	return reinterpret_cast<T*>(useresp);
}

thread::thread(process_fd *p, void *stack_location, void *auxv_address, void *entrypoint, cloudabi_tid_t t)
: process(p)
, thread_id(t)
, running(true)
, userland_stack_top(stack_location)
{
	// Initialize the process
	kernel_stack_size = 0x10000 /* 64 kb */;
	kernel_stack_bottom   = reinterpret_cast<uint8_t*>(get_allocator()->allocate_aligned(kernel_stack_size, process_fd::PAGE_SIZE));

	// initialize all registers and return state to zero
	memset(&state, 0, sizeof(state));

	// set ring 3 data, stack & code segments
	state.ds = state.ss = 0x23;
	state.cs = 0x1b;
	// set fsbase
	state.fs = 0x33;

	// stack location for new process
	state.useresp = reinterpret_cast<uint32_t>(userland_stack_top);

	// memory for the TCB pointer and area
	void **tcb_address = allocate_on_stack<void*>(state.useresp);
	cloudabi_tcb_t *tcb = allocate_on_stack<cloudabi_tcb_t>(state.useresp);
	*tcb_address = reinterpret_cast<void*>(state.useresp);
	// we don't currently use the TCB pointer, so set it to zero
	memset(tcb, 0, sizeof(*tcb));

	if(thread_id == MAIN_THREAD) {
		// initialize stack so that it looks like _start(auxv_address) is called
		*allocate_on_stack<void*>(state.useresp) = auxv_address;
		*allocate_on_stack<void*>(state.useresp) = 0;
	} else {
		// initialize stack so that it looks like threadentry_t(tid, auxv_address) is called
		*allocate_on_stack<void*>(state.useresp) = auxv_address;
		*allocate_on_stack<uint32_t>(state.useresp) = thread_id;
		*allocate_on_stack<void*>(state.useresp) = 0;
	}

	// initial instruction pointer
	state.eip = reinterpret_cast<uint32_t>(entrypoint);
	// allow interrupts
	const int INTERRUPT_ENABLE = 1 << 9;
	state.eflags = INTERRUPT_ENABLE;

	uint8_t *kernel_stack = reinterpret_cast<uint8_t*>(get_kernel_stack_top());

	/* iret frame */
	kernel_stack -= sizeof(interrupt_state_t);
	memcpy(kernel_stack, &state, sizeof(interrupt_state_t));

	/* initial kernel stack frame */
	kernel_stack -= initial_kernel_stack_size;
	memcpy(kernel_stack, &initial_kernel_stack, initial_kernel_stack_size);

	esp = kernel_stack;
}

thread::thread(process_fd *p, thread *otherthread)
: process(p)
, thread_id(MAIN_THREAD)
, running(true)
, userland_stack_top(otherthread->userland_stack_top)
{
	kernel_stack_size = otherthread->kernel_stack_size;
	kernel_stack_bottom = reinterpret_cast<uint8_t*>(get_allocator()->allocate_aligned(kernel_stack_size, process_fd::PAGE_SIZE));

	// copy execution state
	state = otherthread->state;
	memcpy(sse_state, otherthread->sse_state, sizeof(sse_state));

	uint8_t *kernel_stack = reinterpret_cast<uint8_t*>(get_kernel_stack_top());
	/* iret frame */
	kernel_stack -= sizeof(interrupt_state_t);
	memcpy(kernel_stack, &otherthread->state, sizeof(interrupt_state_t));

	/* initial kernel stack frame */
	kernel_stack -= initial_kernel_stack_size;
	memcpy(kernel_stack, &initial_kernel_stack, initial_kernel_stack_size);

	esp = kernel_stack;
}


void thread::set_return_state(interrupt_state_t *new_state) {
	state = *new_state;
}

void thread::get_return_state(interrupt_state_t *return_state) {
	*return_state = state;
}

const char *int_num_to_name(int int_no, bool *err_code);

void thread::interrupt(int int_no, int err_code)
{
	if(int_no == 0x80) {
		handle_syscall();
		return;
	}

	get_vga_stream() << "Thread " << this << " (process " << process << ", name \"" << process->name << "\") encountered fatal interrupt:\n";
	get_vga_stream() << "  " << int_num_to_name(int_no, nullptr) << " at eip=0x" << hex << state.eip << dec << "\n";

	if(int_no == 0x0e /* Page fault */) {
		auto &stream = get_vga_stream();
		if(err_code & 0x01) {
			stream << "Caused by a page-protection violation during page ";
		} else {
			stream << "Caused by a non-present page during page ";
		}
		stream << ((err_code & 0x02) ? "write" : "read");
		stream << ((err_code & 0x04) ? " in unprivileged mode" : " in kernel mode");
		if(err_code & 0x08) {
			stream << " as a result of reading a reserved field";
		}
		if(err_code & 0x10) {
			stream << " as a result of an instruction fetch";
		}
		stream << "\n";
		uint32_t address;
		asm volatile("mov %%cr2, %0" : "=a"(address));
		stream << "Virtual address accessed: 0x" << hex << address << dec << "\n";
	}

	cloudabi_signal_t sig;
	if(int_no == 0 || int_no == 4 || int_no == 16 || int_no == 19) {
		sig = CLOUDABI_SIGFPE;
	} else if(int_no == 6) {
		sig = CLOUDABI_SIGILL;
	} else if(int_no == 11 || int_no == 12 || int_no == 13 || int_no == 14) {
		sig = CLOUDABI_SIGSEGV;
	} else {
		sig = CLOUDABI_SIGKILL;
	}
	process->signal(sig);
}

void thread::handle_syscall() {
	// software interrupt

	// TODO: for all system calls: check if all pointers refer to valid
	// memory areas and if userspace has access to all of them

	int syscall = state.eax;
	if(syscall == 1) {
		// retired
		process->signal(CLOUDABI_SIGSYS);
	} else if(syscall == 2) {
		// putstring(ebx=fd, ecx=ptr, edx=size), returns eax=0 or eax=-1 on error
		int fdnum = state.ebx;
		fd_mapping_t *mapping;
		auto res = process->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FD_WRITE);
		if(res != error_t::no_error) {
			state.eax = -1;
			return;
		}

		const char *str = reinterpret_cast<const char*>(state.ecx);
		const size_t size = state.edx;

		if(reinterpret_cast<uint32_t>(str) >= _kernel_virtual_base
		|| reinterpret_cast<uint32_t>(str) + size >= _kernel_virtual_base
		|| size >= 0x40000000
		|| get_page_allocator()->to_physical_address(process, reinterpret_cast<const void*>(str)) == nullptr) {
			get_vga_stream() << "putstring() of a non-userland-accessible string\n";
			state.eax = -1;
			return;
		}

		res = mapping->fd->putstring(str, size);
		state.eax = res == error_t::no_error ? 0 : -1;
	} else if(syscall == 3) {
		// sys_fd_read(ebx=fd, ecx=ptr, edx=size), returns eax=0 or eax=-1 on error
		int fdnum = state.ebx;
		fd_mapping_t *mapping;
		auto res = process->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FD_READ);
		if(res != error_t::no_error) {
			state.eax = -1;
			return;
		}

		// TODO: store offset in fd
		void *buf = reinterpret_cast<void*>(state.ecx);
		size_t len = state.edx;
		size_t r = mapping->fd->read(0, buf, len);
		if(mapping->fd->error != error_t::no_error) {
			state.eax = -1;
			return;
		}
		state.eax = 0;
		state.edx = r;
	} else if(syscall == 4) {
		// sys_proc_file_open(ecx=parameters) returns eax=fd or eax=-1 on error
		struct args_t {
			cloudabi_lookup_t dirfd;
			const char *path;
			size_t pathlen;
			cloudabi_oflags_t oflags;
			const cloudabi_fdstat_t *fds;
			cloudabi_fd_t *fd;
		};
		args_t *args = reinterpret_cast<args_t*>(state.ecx);

		int fdnum = args->dirfd.fd;
		fd_mapping_t *mapping;
		auto res = process->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FILE_OPEN);
		if(res != error_t::no_error) {
			state.eax = -1;
			return;
		}

		// TODO: take lookup flags into account, args->dirfd.flags

		// check if fd can be created with such rights
		if((mapping->rights_inheriting & args->fds->fs_rights_base) != args->fds->fs_rights_base
		|| (mapping->rights_inheriting & args->fds->fs_rights_inheriting) != args->fds->fs_rights_inheriting) {
			get_vga_stream() << "userspace wants too many permissions\n";
			state.eax = -1;
		}

		fd_t *new_fd = mapping->fd->openat(args->path, args->pathlen, args->oflags, args->fds);
		if(!new_fd || mapping->fd->error != error_t::no_error) {
			get_vga_stream() << "failed to openat()\n";
			state.eax = -1;
			return;
		}

		int new_fdnum = process->add_fd(new_fd, args->fds->fs_rights_base, args->fds->fs_rights_inheriting);
		*(args->fd) = new_fdnum;
		state.eax = 0;
	} else if(syscall == 5) {
		// sys_fd_stat_get(ebx=fd, ecx=fdstat_t) returns eax=fd or eax=-1 on error
		int fdnum = state.ebx;
		fd_mapping_t *mapping;
		auto res = process->get_fd(&mapping, fdnum, 0);
		if(res != error_t::no_error) {
			state.eax = -1;
			return;
		}

		cloudabi_fdstat_t *stat = reinterpret_cast<cloudabi_fdstat_t*>(state.ecx);

		// TODO: check if ecx until ecx+sizeof(fdstat_t) is valid *writable* process memory
		if(reinterpret_cast<uint32_t>(stat) >= _kernel_virtual_base
		|| reinterpret_cast<uint32_t>(stat) + sizeof(cloudabi_fdstat_t) >= _kernel_virtual_base
		|| get_page_allocator()->to_physical_address(process, reinterpret_cast<const void*>(stat)) == nullptr) {
			get_vga_stream() << "sys_fd_stat_get() of a non-userland-accessible string\n";
			state.eax = -1;
			return;
		}

		stat->fs_filetype = mapping->fd->type;
		stat->fs_flags = mapping->fd->flags;
		stat->fs_rights_base = mapping->rights_base;
		stat->fs_rights_inheriting = mapping->rights_inheriting;
		state.eax = 0;
	} else if(syscall == 6) {
		// sys_fd_proc_exec(ecx=parameters) returns eax=-1 on error
		struct args_t {
			cloudabi_fd_t fd;
			const void *data;
			size_t datalen;
			const cloudabi_fd_t *fds;
			size_t fdslen;
		};

		args_t *args = reinterpret_cast<args_t*>(state.ecx);

		fd_mapping_t *mapping;
		auto res = process->get_fd(&mapping, args->fd, CLOUDABI_RIGHT_PROC_EXEC);
		if(res != error_t::no_error) {
			state.eax = -1;
			return;
		}

		fd_mapping_t *new_fds[args->fdslen];
		for(size_t i = 0; i < args->fdslen; ++i) {
			fd_mapping_t *old_mapping;
			res = process->get_fd(&old_mapping, args->fds[i], 0);
			if(res != error_t::no_error) {
				// request to map an invalid fd
				state.eax = -1;
				return;
			}
			// copy the mapping to the new process
			new_fds[i] = old_mapping;
		}

		res = process->exec(mapping->fd, args->fdslen, new_fds, args->data, args->datalen);
		if(res != error_t::no_error) {
			get_vga_stream() << "exec() failed because of " << res << "\n";
			state.eax = -1;
			return;
		}
	} else if(syscall == 7) {
		// sys_mem_map
		struct args_t {
			void *addr;
			size_t len;
			cloudabi_mprot_t prot;
			cloudabi_mflags_t flags;
			cloudabi_fd_t fd;
			cloudabi_filesize_t off;
			void **mem;
		};

		args_t *args = reinterpret_cast<args_t*>(state.ecx);
		if(!(args->flags & CLOUDABI_MAP_ANON) || args->fd != CLOUDABI_MAP_ANON_FD) {
			get_vga_stream() << "Only anonymous mappings are supported at the moment\n";
			state.eax = -1;
			return;
		}
		if(!(args->flags & CLOUDABI_MAP_PRIVATE)) {
			get_vga_stream() << "Only private mappings are supported at the moment\n";
			state.eax = -1;
			return;
		}
		void *address_requested = args->addr;
		bool fixed = args->flags & CLOUDABI_MAP_FIXED;
		// NOTE: cloudabi sys_mem_map() defines that address_requested
		// is only used when MAP_FIXED is given, but in other mmap()
		// implementations address_requested is considered a hint when
		// MAP_FIXED isn't given. We try that as well.
		// TODO: if !given and address_requested isn't free, also find
		// another free virtual range.
		if(!fixed && address_requested == nullptr) {
			address_requested = process->find_free_virtual_range(len_to_pages(args->len));
			if(address_requested == nullptr) {
				get_vga_stream() << "Failed to find virtual memory for mapping.\n";
				state.eax = -1;
				return;
			}
		}
		if((reinterpret_cast<uint32_t>(address_requested) % process_fd::PAGE_SIZE) != 0) {
			get_vga_stream() << "Address requested isn't page aligned\n";
			state.eax = -1;
			return;
		}
		mem_mapping_t *mapping = get_allocator()->allocate<mem_mapping_t>();
		new (mapping) mem_mapping_t(process, address_requested, len_to_pages(args->len), NULL, 0, args->prot);
		process->add_mem_mapping(mapping, fixed);
		// TODO: instead of completely backing, await the page fault and do it then
		mapping->ensure_completely_backed();
		memset(address_requested, 0, args->len);
		*(args->mem) = address_requested;
		state.eax = 0;
	} else if(syscall == 8) {
		// sys_mem_unmap(ecx=addr, edx=len)
		// find mapping, remove it from list, remove it from page tables
		uint8_t *addr = reinterpret_cast<uint8_t*>(state.ecx);
		size_t len = state.edx;

		process->mem_unmap(addr, len);
	} else if(syscall == 9) {
		// cloudabi_sys_proc_fork() takes no arguments, creates a new process, and:
		// * in the parent, returns eax=child_fd, ecx=thread_id
		// * in the child, returns eax=CLOUDABI_PROCESS_CHILD, ecx=MAIN_THREAD
		process_fd *newprocess = get_allocator()->allocate<process_fd>();
		new(newprocess) process_fd("forked process");

		// Change child state before fork(), as it inherits the state
		// and puts it into the interrupt frame on the kernel stack
		// immediately
		state.eax = CLOUDABI_PROCESS_CHILD;
		state.ecx = MAIN_THREAD;

		newprocess->install_page_directory();
		newprocess->fork(this);
		process->install_page_directory();

		// set return values for parent
		int fdnum = process->add_fd(newprocess, CLOUDABI_RIGHT_POLL_PROC_TERMINATE, 0);
		state.eax = fdnum;
		state.ecx = thread_id;
	} else if(syscall == 10) {
		// sys_proc_exit(ecx=rval). Doesn't return.
		process->exit(state.ecx);
		// running will be false after this, so we won't be rescheduled.
		// we'll be cleaned up when the last file descriptor to this process closes.
	} else if(syscall == 11) {
		// sys_proc_raise(ecx=signal). Returns only if signal is not fatal.
		process->signal(state.ecx);
		// like with exit, running will be false, we'll be cleaned up later
	} else if(syscall == 12) {
		// sys_thread_create(ecx=threadattr, ebx=tid_t).
		cloudabi_threadattr_t *attr = reinterpret_cast<cloudabi_threadattr_t*>(state.ecx);
		cloudabi_tid_t *tid = reinterpret_cast<cloudabi_tid_t*>(state.ebx);
		/* TODO: do something with attr->stack_size? */
		thread *thr = process->add_thread(attr->stack, attr->argument, reinterpret_cast<void*>(attr->entry_point));
		*tid = thr->thread_id;
		state.eax = 0;
	} else if(syscall == 13) {
		// sys_thread_exit(ecx=lock, ebx=lock_scope). Doesn't return.
		// TODO: unlock the lock
		thread_exit();
		get_scheduler()->thread_yield();
	} else if(syscall == 14) {
		// sys_thread_yield()
		get_scheduler()->thread_yield();
	} else if(syscall == 15) {
		// sys_fd_create2(ecx=type, ebx=fd1, edx=fd2). Returns eax=0 on success, -1 otherwise.
		if(state.ecx == CLOUDABI_FILETYPE_FIFO) {
			cloudabi_fd_t *fd1 = reinterpret_cast<cloudabi_fd_t*>(state.ebx);
			cloudabi_fd_t *fd2 = reinterpret_cast<cloudabi_fd_t*>(state.edx);
			fd_t *pfd = get_allocator()->allocate<pipe_fd>();
			new (pfd) pipe_fd(1024, "pipe_fd");

			*fd1 = process->add_fd(pfd, CLOUDABI_RIGHT_FD_READ, 0);
			*fd2 = process->add_fd(pfd, CLOUDABI_RIGHT_FD_WRITE, 0);
			state.eax = 0;
		} else {
			state.eax = -1;
		}
	} else {
		get_vga_stream() << "Syscall " << state.eax << " unknown, signalling process\n";
		process->signal(CLOUDABI_SIGSYS);
	}
}

void *thread::get_kernel_stack_top() {
	return reinterpret_cast<char*>(kernel_stack_bottom) + kernel_stack_size;
}

void *thread::get_fsbase() {
	return reinterpret_cast<void*>(reinterpret_cast<uint32_t>(userland_stack_top) - sizeof(void*));
}

void thread::save_sse_state() {
	asm volatile("fxsave %0" : "=m" (sse_state));
}

void thread::restore_sse_state() {
	asm volatile("fxrstor %0" : "=m" (sse_state));
}

void thread::thread_block() {
	if(blocked) {
		kernel_panic("Trying to block a blocked thread");
	}
	blocked = true;
	unscheduled = false;
	get_scheduler()->thread_blocked(this);
	get_scheduler()->thread_yield();
}

void thread::thread_unblock() {
	if(!blocked) {
		kernel_panic("unblock() while not blocked");
	}

	blocked = false;
	if(unscheduled) {
		// re-schedule
		get_scheduler()->thread_ready(this);
		unscheduled = false;
	}
}

bool thread::is_ready() {
	return running && process->is_running() && !blocked;
}
