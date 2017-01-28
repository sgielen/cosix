#include "thread.hpp"
#include "process_fd.hpp"
#include "scheduler.hpp"
#include "pipe_fd.hpp"
#include <memory/page_allocator.hpp>
#include <memory/allocator.hpp>
#include "global.hpp"
#include <rng/rng.hpp>
#include <oslibc/assert.hpp>
#include <time/clock_store.hpp>
#include <proc/syscalls.hpp>

using namespace cloudos;

extern uint32_t _kernel_virtual_base;
extern uint32_t initial_kernel_stack;
extern uint32_t initial_kernel_stack_size;

template <typename T>
static inline T *allocate_on_stack(uint32_t &useresp) {
	useresp -= sizeof(T);
	return reinterpret_cast<T*>(useresp);
}

static bool return_true(void*, thread_condition*) {
	return true;
}

thread::thread(process_fd *p, void *stack_location, void *auxv_address, void *entrypoint, cloudabi_tid_t t)
: process(p)
, thread_id(t)
, exited(false)
, userland_stack_top(stack_location)
{
	if((thread_id & 0x80000000) != 0) {
		kernel_panic("Upper 2 bits of the thread ID must not be set");
	}

	// initialize the stack
	kernel_stack_size = 0x10000 /* 64 kb */;
	kernel_stack_alloc = allocate_aligned(kernel_stack_size, process_fd::PAGE_SIZE);
	if(kernel_stack_alloc.ptr == nullptr) {
		kernel_panic("Failed to allocate kernel stack");
	}

	// initialize all registers and return state to zero
	memset(&state, 0, sizeof(state));

	// set ring 3 data, stack & code segments
	state.ds = state.ss = 0x23;
	state.cs = 0x1b;
	// set fsbase
	state.fs = 0x33;

	// stack location for new thread
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

thread::thread(process_fd *p, shared_ptr<thread> otherthread)
: process(p)
, thread_id(MAIN_THREAD)
, exited(false)
, userland_stack_top(otherthread->userland_stack_top)
{
	kernel_stack_size = otherthread->kernel_stack_size;
	kernel_stack_alloc = allocate_aligned(kernel_stack_size, process_fd::PAGE_SIZE);

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

thread::~thread() {
	assert(exited);
	assert(get_scheduler()->get_running_thread().get() != this);
	bool on_stack;
	assert(reinterpret_cast<uintptr_t>(&on_stack) < reinterpret_cast<uintptr_t>(kernel_stack_alloc.ptr)
	    || reinterpret_cast<uintptr_t>(&on_stack) >= reinterpret_cast<uintptr_t>(kernel_stack_alloc.ptr) + kernel_stack_alloc.size);
	UNUSED(on_stack);
	deallocate(kernel_stack_alloc);
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

	// All system calls return eax=0 on success, or eax=cloudabi_error_t on failure.

	int syscall = state.eax;
	if(syscall == 1) {
		// retired
		process->signal(CLOUDABI_SIGSYS);
	} else if(syscall == 2) {
		// putstring(ebx=fd, ecx=ptr, edx=size)
		int fdnum = state.ebx;
		fd_mapping_t *mapping;
		auto res = process->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FD_WRITE);
		if(res != 0) {
			state.eax = res;
			return;
		}

		const char *str = reinterpret_cast<const char*>(state.ecx);
		const size_t size = state.edx;

		if(reinterpret_cast<uint32_t>(str) >= _kernel_virtual_base
		|| reinterpret_cast<uint32_t>(str) + size >= _kernel_virtual_base
		|| size >= 0x40000000
		|| get_map_virtual()->to_physical_address(process, reinterpret_cast<const void*>(str)) == nullptr) {
			get_vga_stream() << "putstring() of a non-userland-accessible string\n";
			state.eax = EFAULT;
			return;
		}

		mapping->fd->putstring(str, size);
		state.eax = mapping->fd->error;
	} else if(syscall == 3) {
		// sys_fd_read(ebx=fd, ecx=ptr, edx=size)
		int fdnum = state.ebx;
		fd_mapping_t *mapping;
		auto res = process->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FD_READ);
		if(res != 0) {
			state.eax = res;
			return;
		}

		// TODO: store offset in fd
		void *buf = reinterpret_cast<void*>(state.ecx);
		size_t len = state.edx;
		size_t r = mapping->fd->read(buf, len);
		state.eax = mapping->fd->error;
		state.edx = r;
	} else if(syscall == 4) {
		// sys_proc_file_open(ecx=parameters)
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
		if(res != 0) {
			state.eax = res;
			return;
		}

		// TODO: take lookup flags into account, args->dirfd.flags

		// check if fd can be created with such rights
		if((mapping->rights_inheriting & args->fds->fs_rights_base) != args->fds->fs_rights_base
		|| (mapping->rights_inheriting & args->fds->fs_rights_inheriting) != args->fds->fs_rights_inheriting) {
			get_vga_stream() << "userspace wants too many permissions\n";
			state.eax = ENOTCAPABLE;
		}

		auto new_fd = mapping->fd->openat(args->path, args->pathlen, args->oflags, args->fds);
		if(!new_fd || mapping->fd->error != 0) {
			get_vga_stream() << "failed to openat()\n";
			state.eax = mapping->fd->error;
			return;
		}

		// Depending on filetype, drop inheriting rights if they don't make sense
		auto inheriting = args->fds->fs_rights_inheriting;
		if(new_fd->type == CLOUDABI_FILETYPE_REGULAR_FILE) {
			inheriting = 0;
		}

		int new_fdnum = process->add_fd(new_fd, args->fds->fs_rights_base, inheriting);
		*(args->fd) = new_fdnum;
		state.eax = 0;
	} else if(syscall == 5) {
		// sys_fd_stat_get(ebx=fd, ecx=fdstat_t)
		int fdnum = state.ebx;
		fd_mapping_t *mapping;
		auto res = process->get_fd(&mapping, fdnum, 0);
		if(res != 0) {
			state.eax = res;
			return;
		}

		cloudabi_fdstat_t *stat = reinterpret_cast<cloudabi_fdstat_t*>(state.ecx);

		// TODO: check if ecx until ecx+sizeof(fdstat_t) is valid *writable* process memory
		if(reinterpret_cast<uint32_t>(stat) >= _kernel_virtual_base
		|| reinterpret_cast<uint32_t>(stat) + sizeof(cloudabi_fdstat_t) >= _kernel_virtual_base
		|| get_map_virtual()->to_physical_address(process, reinterpret_cast<const void*>(stat)) == nullptr) {
			get_vga_stream() << "sys_fd_stat_get() of a non-userland-accessible string\n";
			state.eax = EFAULT;
			return;
		}

		stat->fs_filetype = mapping->fd->type;
		stat->fs_flags = mapping->fd->flags;
		stat->fs_rights_base = mapping->rights_base;
		stat->fs_rights_inheriting = mapping->rights_inheriting;
		state.eax = 0;
	} else if(syscall == 6) {
		// sys_proc_exec(ecx=parameters)
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
		if(res != 0) {
			state.eax = res;
			return;
		}

		fd_mapping_t *new_fds[args->fdslen];
		for(size_t i = 0; i < args->fdslen; ++i) {
			fd_mapping_t *old_mapping;
			res = process->get_fd(&old_mapping, args->fds[i], 0);
			if(res != 0) {
				// request to map an invalid fd
				state.eax = res;
				return;
			}
			// copy the mapping to the new process
			new_fds[i] = old_mapping;
		}

		res = process->exec(mapping->fd, args->fdslen, new_fds, args->data, args->datalen);
		if(res != 0) {
			get_vga_stream() << "exec() failed because of " << res << "\n";
			state.eax = res;
			return;
		}
		assert(exited);
		get_scheduler()->thread_yield();
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
		if(!(args->flags & CLOUDABI_MAP_ANON)) {
			get_vga_stream() << "Only anonymous mappings are supported at the moment\n";
			state.eax = ENOSYS;
			return;
		}
		if(!(args->flags & CLOUDABI_MAP_PRIVATE)) {
			get_vga_stream() << "Only private mappings are supported at the moment\n";
			state.eax = ENOSYS;
			return;
		}
		if(args->flags & CLOUDABI_MAP_ANON && args->fd != CLOUDABI_MAP_ANON_FD) {
			state.eax = EINVAL;
			return;
		}
		void *address_requested = args->addr;
		bool fixed = args->flags & CLOUDABI_MAP_FIXED;
		auto prot = args->prot;
		if((prot & CLOUDABI_PROT_EXEC) && (prot & CLOUDABI_PROT_WRITE)) {
			// CloudABI enforces W xor X
			state.eax = ENOTSUP;
			return;
		}
		if(prot & ~(CLOUDABI_PROT_EXEC | CLOUDABI_PROT_READ | CLOUDABI_PROT_WRITE)) {
			// invalid protection bits
			state.eax = ENOTSUP;
			return;
		}
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
				state.eax = ENOMEM;
				return;
			}
		}
		if((reinterpret_cast<uint32_t>(address_requested) % process_fd::PAGE_SIZE) != 0) {
			get_vga_stream() << "Address requested isn't page aligned\n";
			state.eax = EINVAL;
			return;
		}
		mem_mapping_t *mapping = get_allocator()->allocate<mem_mapping_t>();
		new (mapping) mem_mapping_t(process, address_requested, len_to_pages(args->len), NULL, 0, prot);
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
		state.eax = 0;
	} else if(syscall == 9) {
		// cloudabi_sys_proc_fork() takes no arguments, creates a new process, and:
		// * in the parent, returns ebx=child_fd, ecx=thread_id
		// * in the child, returns ebx=CLOUDABI_PROCESS_CHILD, ecx=MAIN_THREAD
		auto newprocess = make_shared<process_fd>("initializing process");

		// Change child state before fork(), as it inherits the state
		// and puts it into the interrupt frame on the kernel stack
		// immediately
		state.eax = 0;
		state.ebx = CLOUDABI_PROCESS_CHILD;
		state.ecx = MAIN_THREAD;

		newprocess->install_page_directory();
		newprocess->fork(shared_from_this());
		process->install_page_directory();

		// set return values for parent
		int fdnum = process->add_fd(newprocess, CLOUDABI_RIGHT_POLL_PROC_TERMINATE, 0);
		state.eax = 0;
		state.ebx = fdnum;
		state.ecx = thread_id;
	} else if(syscall == 10) {
		// sys_proc_exit(ecx=rval). Doesn't return.
		process->exit(state.ecx);
		// exited will be true after this, so we won't be rescheduled.
		// we'll be cleaned up when the last file descriptor to this process closes.
		get_scheduler()->thread_yield();
	} else if(syscall == 11) {
		// sys_proc_raise(ecx=signal). Returns only if signal is not fatal.
		process->signal(state.ecx);
		state.eax = 0;
		// like with exit, if signal is fatal exited will be true, we'll be cleaned up later
		get_scheduler()->thread_yield();
	} else if(syscall == 12) {
		// sys_thread_create(ecx=threadattr, ebx=tid_t).
		cloudabi_threadattr_t *attr = reinterpret_cast<cloudabi_threadattr_t*>(state.ecx);
		cloudabi_tid_t *tid = reinterpret_cast<cloudabi_tid_t*>(state.ebx);
		/* TODO: do something with attr->stack_size? */
		shared_ptr<thread> thr = process->add_thread(attr->stack, attr->argument, reinterpret_cast<void*>(attr->entry_point));
		*tid = thr->thread_id;
		state.eax = 0;
	} else if(syscall == 13) {
		// sys_thread_exit(ecx=lock, ebx=lock_scope). Doesn't return.
		auto *lock = reinterpret_cast<_Atomic(cloudabi_condvar_t)*>(state.ecx);
		auto scope = state.ebx;
		if(scope != CLOUDABI_SCOPE_PRIVATE) {
			get_vga_stream() << "thread_exit(): non-private locks are not supported yet\n";
			state.eax = ENOSYS;
			state.edx = 0;
			return;
		}
		thread_exit();
		drop_userspace_lock(lock);
		get_scheduler()->thread_yield();
	} else if(syscall == 14) {
		// sys_thread_yield()
		get_scheduler()->thread_yield();
		state.eax = 0;
	} else if(syscall == 15) {
		// sys_fd_create2(ecx=type, ebx=fd1, edx=fd2). Returns eax=0 on success, -1 otherwise.
		if(state.ecx == CLOUDABI_FILETYPE_FIFO) {
			cloudabi_fd_t *fd1 = reinterpret_cast<cloudabi_fd_t*>(state.ebx);
			cloudabi_fd_t *fd2 = reinterpret_cast<cloudabi_fd_t*>(state.edx);
			auto pfd = make_shared<pipe_fd>(1024, "pipe_fd");

			auto pipe_rights = CLOUDABI_RIGHT_POLL_FD_READWRITE
			                 | CLOUDABI_RIGHT_FD_STAT_PUT_FLAGS
			                 | CLOUDABI_RIGHT_FILE_STAT_FGET;
			*fd1 = process->add_fd(pfd, pipe_rights | CLOUDABI_RIGHT_FD_READ, 0);
			*fd2 = process->add_fd(pfd, pipe_rights | CLOUDABI_RIGHT_FD_WRITE, 0);
			state.eax = 0;
		} else {
			state.eax = ENOSYS;
		}
	} else if(syscall == 16) {
		// sys_fd_close(ecx=fd)
		int fdnum = state.ecx;
		state.eax = process->close_fd(fdnum);
	} else if(syscall == 17) {
		// sys_file_create(ecx=fd, ebx=path, edx=pathlen, esi=type)
		cloudabi_filetype_t type = state.esi;
		/* TODO remove this */
		type = CLOUDABI_FILETYPE_DIRECTORY;
		cloudabi_rights_t right_needed = 0;
		if(type == CLOUDABI_FILETYPE_DIRECTORY) {
			right_needed = CLOUDABI_RIGHT_FILE_CREATE_DIRECTORY;
		} else {
			get_vga_stream() << "Unknown file type to create, failing\n";
			state.eax = -1;
			return;
		}

		int fdnum = state.ecx;
		fd_mapping_t *mapping;
		auto res = process->get_fd(&mapping, fdnum, right_needed);
		if(res != 0) {
			state.eax = res;
			return;
		}

		const char *path = reinterpret_cast<const char*>(state.ebx);
		size_t pathlen = state.edx;

		mapping->fd->file_create(path, pathlen, type);
		state.eax = mapping->fd->error;
	} else if(syscall == 18) {
		// sys_random_get(ecx=buf, edx=nbyte)
		char *buf = reinterpret_cast<char*>(state.ecx);
		size_t nbyte = state.edx;
		get_random()->get(buf, nbyte);
		state.eax = 0;
	} else if(syscall == 19) {
		// sys_file_readdir
		struct args_t {
			cloudabi_fd_t fd;
			char *buf;
			size_t nbyte;
			cloudabi_dircookie_t cookie;
			size_t *bufused;
		};

		args_t *args = reinterpret_cast<args_t*>(state.ecx);
		int fdnum = args->fd;
		fd_mapping_t *mapping;
		auto res = process->get_fd(&mapping, fdnum, CLOUDABI_RIGHT_FILE_READDIR);
		if(res != 0) {
			state.eax = res;
			return;
		}

		*args->bufused = mapping->fd->readdir(args->buf, args->nbyte, args->cookie);
		state.eax = mapping->fd->error;
	} else if(syscall == 20) {
		// sys_fd_dup(ebx=from, ecx=to)
		int fdnum = state.ebx;
		fd_mapping_t *mapping;
		auto res = process->get_fd(&mapping, fdnum, 0);
		if(res != 0) {
			state.eax = res;
			return;
		}

		int *fd_to = reinterpret_cast<int*>(state.ecx);
		*fd_to = process->add_fd(mapping->fd, mapping->rights_base, mapping->rights_inheriting);
		state.eax = 0;
	} else if(syscall == 21) {
		// sys_poll(ebx=in, ecx=out, edx=nsubscriptions)
		// sets edx to nevents
		auto *in = reinterpret_cast<const cloudabi_subscription_t *>(state.ebx);
		auto *out = reinterpret_cast<cloudabi_event_t *>(state.ecx);
		size_t nsubscriptions = reinterpret_cast<size_t>(state.edx);
		// There are a limited number of valid options for the contents of 'in':
		// - empty
		// - [0] lock_rdlock/lock_wrlock, and optionally [1] a clock
		// - [0] condvar, and optionally [1] a clock
		// - any number of clock/fd_read/fd_write/proc_terminate
		if(nsubscriptions == 0) {
			state.edx = 0;
			state.eax = 0;
			return;
		}
		cloudabi_eventtype_t first_event = in[0].type;
		if(first_event == CLOUDABI_EVENTTYPE_LOCK_RDLOCK
		|| first_event == CLOUDABI_EVENTTYPE_LOCK_WRLOCK
		|| first_event == CLOUDABI_EVENTTYPE_CONDVAR) {
			if(nsubscriptions == 2) {
				// second event must be clock
				if(in[1].type != CLOUDABI_EVENTTYPE_CLOCK) {
					state.edx = 0;
					state.eax = EINVAL;
					return;
				}
			} else if(nsubscriptions > 2) {
				// must be at most 2 events
				state.edx = 0;
				state.eax = EINVAL;
				return;
			}
		} else {
			// none of the events may be LOCK_RDLOCK/LOCK_WRLOCK/CONDVAR
			for(size_t i = 0; i < nsubscriptions; ++i) {
				auto type = in[i].type;
				if(type == CLOUDABI_EVENTTYPE_LOCK_RDLOCK
				|| type == CLOUDABI_EVENTTYPE_LOCK_WRLOCK
				|| type == CLOUDABI_EVENTTYPE_CONDVAR) {
					state.edx = 0;
					state.eax = EINVAL;
					return;
				}
			}
		}

		if(first_event == CLOUDABI_EVENTTYPE_LOCK_RDLOCK
		|| first_event == CLOUDABI_EVENTTYPE_LOCK_WRLOCK) {
			// acquire the lock, optionally timing out when the
			// timeout passes.
			auto *lock = in[0].lock.lock;
			if(nsubscriptions == 2) {
				get_vga_stream() << "poll(): clocks for locks are not supported yet\n";
				state.edx = 0;
				state.eax = ENOSYS;
				return;
			}
			if(in[0].lock.lock_scope != CLOUDABI_SCOPE_PRIVATE) {
				get_vga_stream() << "poll(): non-private locks are not supported yet\n";
				state.edx = 0;
				state.eax = ENOSYS;
				return;
			}
			// this call blocks this thread until the lock is acquired
			acquire_userspace_lock(lock, in[0].type);
			out[0].userdata = in[0].userdata;
			out[0].error = 0;
			out[0].type = in[0].type;
			out[0].lock.lock = in[0].lock.lock;
			state.edx = 1;
			state.eax = 0;
		} else if(first_event == CLOUDABI_EVENTTYPE_CONDVAR) {
			// release the lock, wait() for the condvar, and
			// re-acquire the lock when it is notified, optionally
			// timing out when the timeout passes.
			auto *condvar = in[0].condvar.condvar;
			auto *lock = in[0].condvar.lock;
			if(nsubscriptions == 2) {
				get_vga_stream() << "poll(): clocks for condvars are not supported yet\n";
				state.edx = 0;
				state.eax = ENOSYS;
				return;
			}
			if(in[0].condvar.condvar_scope != CLOUDABI_SCOPE_PRIVATE || in[0].condvar.lock_scope != CLOUDABI_SCOPE_PRIVATE) {
				get_vga_stream() << "poll(): non-private locks or condvars are not supported yet\n";
				state.edx = 0;
				state.eax = ENOSYS;
				return;
			}
			// this call blocks this thread until the condition variable is notified
			// TODO: this currently does not cause a race because we are UP and without
			// kernel preemption, but will cause a race later
			drop_userspace_lock(lock);
			wait_userspace_cv(condvar);
			acquire_userspace_lock(lock, CLOUDABI_EVENTTYPE_LOCK_WRLOCK);
			out[0].userdata = in[0].userdata;
			out[0].error = 0;
			out[0].type = in[0].type;
			out[0].lock.lock = in[0].condvar.lock;
			state.edx = 1;
			state.eax = 0;
		} else {
			state.edx = 0;

			// return an event when any of these subscriptions happen.
			thread_condition_waiter w;
			auto conditions_alloc = allocate(sizeof(thread_condition) * nsubscriptions);
			thread_condition *conditions = reinterpret_cast<thread_condition*>(conditions_alloc.ptr);

			struct thread_condition_userdata {
				const cloudabi_subscription_t *subscription;
				cloudabi_errno_t error;
			};

			// This signaler is always 'already satisfied', so if it is used, it will inhibit
			// the actual wait(). Therefore, it can be used when poll() should immediately
			// return, e.g. because of an error in the parameters.
			thread_condition_signaler null_signaler;
			null_signaler.set_already_satisfied_function(return_true, nullptr);

			for(size_t subi = 0; subi < nsubscriptions; ++subi) {
				cloudabi_subscription_t const &i = in[subi];
				thread_condition &condition = conditions[subi];
				thread_condition_userdata *userdata = allocate<thread_condition_userdata>();
				userdata->subscription = &i;
				userdata->error = 0;

				thread_condition_signaler *signaler = nullptr;
				switch(i.type) {
				case CLOUDABI_EVENTTYPE_CONDVAR:
				case CLOUDABI_EVENTTYPE_LOCK_RDLOCK:
				case CLOUDABI_EVENTTYPE_LOCK_WRLOCK:
					kernel_panic("Eventtype cannot exist here");
				case CLOUDABI_EVENTTYPE_CLOCK: {
					auto clock = get_clock_store()->get_clock(i.clock.clock_id);
					if(clock == nullptr) {
						get_vga_stream() << "Unknown clock ID " << i.clock.clock_id << "\n";
						userdata->error = ENOSYS;
						signaler = &null_signaler;
						break;
					}
					auto timeout = i.clock.timeout;
					auto time = clock->get_time(i.clock.precision);
					if(!(i.clock.flags == CLOUDABI_SUBSCRIPTION_CLOCK_ABSTIME)) {
						timeout += time;
					}
					if(timeout <= time) {
						// already satisfied
						signaler = &null_signaler;
					} else {
						signaler = clock->get_signaler(timeout, i.clock.precision);
					}
					break;
				}
				case CLOUDABI_EVENTTYPE_FD_READ:
					get_vga_stream() << "Unimplemented fd-read eventtype, failing\n";
					userdata->error = ENOSYS;
					signaler = &null_signaler;
					break;
				case CLOUDABI_EVENTTYPE_FD_WRITE:
					get_vga_stream() << "Unimplemented fd-write eventtype, failing\n";
					userdata->error = ENOSYS;
					signaler = &null_signaler;
					break;
				case CLOUDABI_EVENTTYPE_PROC_TERMINATE: {
					cloudabi_fd_t proc_fdnum = i.proc_terminate.fd;
					fd_mapping_t *proc_mapping;
					auto res = process->get_fd(&proc_mapping, proc_fdnum, CLOUDABI_RIGHT_POLL_PROC_TERMINATE);
					if(res != 0) {
						userdata->error = res;
						signaler = &null_signaler;
					} else {
						shared_ptr<fd_t> proc_fd = proc_mapping->fd;
						if(proc_fd->type != CLOUDABI_FILETYPE_PROCESS) {
							userdata->error = EBADF;
							signaler = &null_signaler;
						} else {
							shared_ptr<process_fd> proc = proc_fd.reinterpret_as<process_fd>();
							signaler = proc->get_termination_signaler();
						}
					}
				}
				}
				new (&condition) thread_condition(signaler);
				condition.userdata = reinterpret_cast<void*>(userdata);
				w.add_condition(&condition);
			}

			w.wait();

			thread_condition_list *satisfied = w.finish();
			state.eax = 0;
			iterate(satisfied, [&](thread_condition_list *item) {
				cloudabi_event_t &o = out[state.edx++];
				if(state.edx > nsubscriptions) {
					kernel_panic("Too many out events");
				}
				thread_condition_userdata *userdata = reinterpret_cast<thread_condition_userdata*>(item->data->userdata);
				auto *i = userdata->subscription;
				o.userdata = i->userdata;
				o.type = i->type;
				o.error = userdata->error;
				if(i->type == CLOUDABI_EVENTTYPE_CLOCK) {
					o.clock.identifier = i->clock.identifier;
				} else if(i->type == CLOUDABI_EVENTTYPE_PROC_TERMINATE && o.error == 0) {
					cloudabi_fd_t proc_fdnum = o.proc_terminate.fd = i->proc_terminate.fd;
					// TODO: store signal and exitcode in the thread_condition when the process dies,
					// so that if we closed the fd in the meantime, we don't error out here
					fd_mapping_t *proc_mapping;
					auto res = process->get_fd(&proc_mapping, proc_fdnum, 0);
					if(res != 0) {
						o.error = res;
						return;
					}
					shared_ptr<fd_t> proc_fd = proc_mapping->fd;
					if(proc_fd->type != CLOUDABI_FILETYPE_PROCESS) {
						o.error = EINVAL;
						return;
					}
					auto proc = proc_fd.reinterpret_as<process_fd>();
					if(!proc->is_terminated(o.proc_terminate.exitcode, o.proc_terminate.signal)) {
						o.error = EINVAL;
						return;
					}
				}
			});
			remove_all(&satisfied, [](thread_condition_list*){
				return true;
			});
			for(size_t subi = 0; subi < nsubscriptions; ++subi) {
				thread_condition &condition = conditions[subi];
				deallocate(reinterpret_cast<thread_condition_userdata*>(condition.userdata));
			}
			deallocate(conditions_alloc);
		}
	} else if(syscall == 22) {
		// lock_unlock(ebx=lock, ecx=scope)
		auto *lock = reinterpret_cast<_Atomic(cloudabi_lock_t)*>(state.ebx);
		auto scope = state.ecx;
		if(scope != CLOUDABI_SCOPE_PRIVATE) {
			get_vga_stream() << "lock_unlock(): non-private locks are not supported yet\n";
			state.edx = 0;
			state.eax = ENOSYS;
			return;
		}
		drop_userspace_lock(lock);
		state.eax = 0;
	} else if(syscall == 23) {
		// condvar_signal(ebx=condvar, ecx=scope, edx=nwaiters)
		auto *condvar = reinterpret_cast<_Atomic(cloudabi_condvar_t)*>(state.ebx);
		auto scope = state.ecx;
		auto nwaiters = reinterpret_cast<cloudabi_nthreads_t>(state.edx);
		if(scope != CLOUDABI_SCOPE_PRIVATE) {
			get_vga_stream() << "condvar_signal(): non-private condition variables are not supported yet\n";
			state.edx = 0;
			state.eax = ENOSYS;
			return;
		}
		signal_userspace_cv(condvar, nwaiters);
		state.eax = 0;
	} else if(syscall == 24) {
		// sys_fd_stat_put(ebx=fd, ecx=fdstat_t, edx=fsflags)
		int fdnum = state.ebx;
		fd_mapping_t *mapping;
		auto res = process->get_fd(&mapping, fdnum, 0);
		if(res != 0) {
			state.eax = res;
			return;
		}

		const cloudabi_fdstat_t *stat = reinterpret_cast<const cloudabi_fdstat_t*>(state.ecx);
		cloudabi_fdsflags_t flags = state.edx;

		if(flags & CLOUDABI_FDSTAT_FLAGS) {
			mapping->fd->flags = stat->fs_flags;
		}
		if(flags & CLOUDABI_FDSTAT_RIGHTS) {
			mapping->rights_base = mapping->rights_base & stat->fs_rights_base;
			mapping->rights_inheriting = mapping->rights_inheriting & stat->fs_rights_inheriting;
		}

		state.eax = 0;
	} else if(syscall == 25) {
		// sys_fd_seek(ebx=fd, ecx=offset, edx=whence), returns newoffset as ecx
		cloudabi_fd_t fdnum = state.ebx;
		cloudabi_filedelta_t offset = state.ecx;
		cloudabi_whence_t whence = state.edx;

		cloudabi_rights_t rights_needed = CLOUDABI_RIGHT_FD_TELL;
		if(whence != CLOUDABI_WHENCE_CUR || offset != 0) {
			rights_needed |= CLOUDABI_RIGHT_FD_SEEK;
		}
		fd_mapping_t *mapping;
		auto res = process->get_fd(&mapping, fdnum, rights_needed);
		if(res != 0) {
			state.eax = res;
			return;
		}

		state.ecx = mapping->fd->seek(offset, whence);
		state.eax = mapping->fd->error;
	} else if(syscall == 26) {
		// sys_clock_res_get
		syscall_context c(reinterpret_cast<void*>(state.esp));
		auto error = syscall_clock_res_get(c);
		if(error) {
			// failed, so set carry bit
			state.eflags |= 0x1;
			state.eax = error;
			state.edx = 0;
		} else {
			// succeeded, unset carry bit
			state.eflags &= ~0x1;
			state.eax = c.result & 0xffffffff;
			state.edx = c.result >> 32;
		}
	} else if(syscall == 27) {
		// sys_clock_time_get
		syscall_context c(reinterpret_cast<void*>(state.esp));
		auto error = syscall_clock_time_get(c);
		if(error) {
			// failed, so set carry bit
			state.eflags |= 0x1;
			state.eax = error;
			state.edx = 0;
		} else {
			// succeeded, unset carry bit
			state.eflags &= ~0x1;
			state.eax = c.result & 0xffffffff;
			state.edx = c.result >> 32;
		}
	} else {
		get_vga_stream() << "Syscall " << state.eax << " unknown, signalling process\n";
		process->signal(CLOUDABI_SIGSYS);
	}
}

void *thread::get_kernel_stack_top() {
	return reinterpret_cast<char*>(kernel_stack_alloc.ptr) + kernel_stack_size;
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

void thread::thread_exit() {
	assert(!exited);
	exited = true;
	process->remove_thread(shared_from_this());
	get_scheduler()->thread_exiting(shared_from_this());
}

void thread::thread_block() {
	assert(!blocked && !exited);
	blocked = true;
	unscheduled = false;
	get_scheduler()->thread_blocked(shared_from_this());
	get_scheduler()->thread_yield();
}

void thread::thread_unblock() {
	if(!blocked) {
		kernel_panic("unblock() while not blocked");
	}

	blocked = false;
	if(unscheduled) {
		// re-schedule
		get_scheduler()->thread_ready(shared_from_this());
		unscheduled = false;
	}
}

bool thread::is_ready() {
	return !exited && process->is_running() && !blocked;
}

void thread::acquire_userspace_lock(_Atomic(cloudabi_lock_t) *lock, cloudabi_eventtype_t locktype)
{
	bool is_write_locked = (*lock & CLOUDABI_LOCK_WRLOCKED) != 0;
	bool want_write_lock = locktype == CLOUDABI_EVENTTYPE_LOCK_WRLOCK;

	// TODO: kernel-lock this userland lock

	if((*lock & 0x3fffffff) == 0) {
		// The lock is unlocked, assume no contention
		if(want_write_lock) {
			// Make this thread writer
			*lock = CLOUDABI_LOCK_WRLOCKED | (thread_id & 0x3fffffff);
		} else {
			// Make this thread reader
			*lock = 1;
		}
		return;
	}

	userland_lock_waiters_t *lock_info = process->get_userland_lock_info(lock);

	if(!is_write_locked && !want_write_lock && (lock_info == nullptr || lock_info->waiting_writers == nullptr)) {
		// The lock is read-locked, this thread wants a read-lock, there are no waiting writers
		// Add it as a reader, still not kernel-managed as this could have been done in userspace as well
		*lock += 1;
		return;
	}

	// All other cases:
	// - The lock could be read-locked, this thread wants a readlock, there are waiting writers
	// - The lock could be read-locked, this thread wants a writelock
	// - The lock could be write-locked
	// In all three cases, this thread needs to wait for its turn. The lock
	// becomes kernel-managed, so that the kernel always notices when the
	// relevant unlocks happen.

	*lock = *lock | CLOUDABI_LOCK_KERNEL_MANAGED;
	if(lock_info == nullptr) {
		lock_info = process->get_or_create_userland_lock_info(lock);
	}

	if(want_write_lock) {
		thread_weaklist *t = allocate<thread_weaklist>(weak_from_this());
		append(&(lock_info->waiting_writers), t);
		thread_block();
	} else {
		lock_info->number_of_readers += 1;
		lock_info->readers_cv.wait();
	}

	// Verify that this thread has the lock now
	// NOTE: it is possible that this thread is only scheduled after another thread already changed the
	// lock value. For example, for pthread_once(), another userland thread may set the readcount to 0
	// even if this thread just did a readlock, before this thread is scheduled again to do the check below.
	// So, we'll warn because it helps to find potential bugs, but don't assert().
	if(want_write_lock) {
		if((*lock & CLOUDABI_LOCK_WRLOCKED) == 0 || (*lock & 0x3fffffff) != thread_id) {
			get_vga_stream() << "Warning: Thought I had a writelock, but it's not writelocked or thread ID isn't mine\n";
		}
	} else {
		if((*lock & 0x3fffffff) == 0) {
			get_vga_stream() << "Warning: Thought I had a readlock, but readcount is 0.\n";
		}
	}
}

void thread::drop_userspace_lock(_Atomic(cloudabi_lock_t) *lock)
{
	// as implemented by cloudlibc:
	// if userspace wants to drop a readlock, they can freely do so if
	// there are still other readers left. the last reader converts his
	// lock into a write-lock, then unlocks it. so, we only support
	// unlocking write-locks.
	if((*lock & CLOUDABI_LOCK_WRLOCKED) == 0) {
		get_vga_stream() << "drop_userspace_lock: lock not acquired for writing\n";
		return;
	}

	if((*lock & 0x3fffffff) != thread_id) {
		get_vga_stream() << "drop_userspace_lock: lock not acquired by this thread\n";
		return;
	}

	// are there any write-waiters for this lock?
	userland_lock_waiters_t *lock_info = process->get_userland_lock_info(lock);
	if(lock_info != nullptr && lock_info->waiting_writers != nullptr) {
		thread_weaklist *first_thread = lock_info->waiting_writers;
		lock_info->waiting_writers = first_thread->next;
		shared_ptr<thread> new_owner = first_thread->data.lock();
		// TODO: is this assertion correct, or should we continue to the next one if unset?
		assert(new_owner);
		*lock = CLOUDABI_LOCK_WRLOCKED | (new_owner->thread_id & 0x3fffffff);
		deallocate(first_thread);

		// no more readers and writers?
		if(lock_info->waiting_writers == nullptr && lock_info->number_of_readers == 0) {
			// lock is now contention-free
			lock_info = nullptr;
			process->forget_userland_lock_info(lock);
		} else {
			// lock is still kernel managed
			*lock |= CLOUDABI_LOCK_KERNEL_MANAGED;
		}

		new_owner->thread_unblock();
		return;
	}

	// lock is no longer kernel-managed, because it's now contention-free
	if(lock_info != nullptr) {
		*lock = lock_info->number_of_readers;
		lock_info->readers_cv.broadcast();
		process->forget_userland_lock_info(lock);
	} else {
		*lock = 0;
	}
}

void thread::wait_userspace_cv(_Atomic(cloudabi_condvar_t) *condvar)
{
	userland_condvar_waiters_t *condvar_cv = process->get_or_create_userland_condvar_cv(condvar);
	condvar_cv->waiters += 1;
	*condvar = 1;
	condvar_cv->cv.wait();
}

void thread::signal_userspace_cv(_Atomic(cloudabi_condvar_t) *condvar, cloudabi_nthreads_t nwaiters)
{
	userland_condvar_waiters_t *condvar_cv = process->get_userland_condvar_cv(condvar);
	if(!condvar_cv) {
		// no waiters
		return;
	}

	// TODO: add the waked threads to the lock writers-waiting list, so that if the
	// thread wakes up, it already atomically has the lock
	if(condvar_cv->waiters <= nwaiters) {
		*condvar = 0;
		condvar_cv->cv.broadcast();
		process->forget_userland_condvar_cv(condvar);
	} else {
		while(nwaiters-- > 0) {
			condvar_cv->waiters -= 1;
			condvar_cv->cv.notify();
		}
	}
}
