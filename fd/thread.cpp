#include <fd/pipe_fd.hpp>
#include <fd/process_fd.hpp>
#include <fd/scheduler.hpp>
#include <fd/thread.hpp>
#include <global.hpp>
#include <memory/allocation.hpp>
#include <oslibc/assert.hpp>
#include <proc/syscalls.hpp>
#include <rng/rng.hpp>
#include <time/clock_store.hpp>

using namespace cloudos;

extern uint32_t _kernel_virtual_base;
extern uint32_t initial_kernel_stack;
extern uint32_t initial_kernel_stack_size;

template <typename T>
static inline T *allocate_on_stack(uint32_t &useresp) {
	useresp -= sizeof(T);
	return reinterpret_cast<T*>(useresp);
}

thread::thread(process_fd *p, void *stack_bottom, size_t stack_len, void *auxv_address, void *entrypoint, cloudabi_tid_t t)
: process(p)
, thread_id(t)
, userland_stack_top(reinterpret_cast<char*>(stack_bottom) + stack_len)
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
		*allocate_on_stack<void*>(state.useresp) = nullptr;
	} else {
		// initialize stack so that it looks like threadentry_t(tid, auxv_address) is called
		*allocate_on_stack<void*>(state.useresp) = auxv_address;
		*allocate_on_stack<uint32_t>(state.useresp) = thread_id;
		*allocate_on_stack<void*>(state.useresp) = nullptr;
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

	/* initialize SSE FXRSTOR region */
	// TODO: instead of doing this every time, do it once and mask
	// off only the important bits
	save_sse_state();
}

thread::thread(process_fd *p, shared_ptr<thread> otherthread)
: process(p)
, thread_id(MAIN_THREAD)
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

	syscall_context c(this, reinterpret_cast<void*>(state.useresp));
	cloudabi_errno_t error;

	switch(state.eax) {
	case 0:  error = syscall_clock_res_get(c); break;
	case 1:  error = syscall_clock_time_get(c); break;
	case 2:  error = syscall_condvar_signal(c); break;
	case 3:  error = syscall_fd_close(c); break;
	case 4:  error = syscall_fd_create1(c); break;
	case 5:  error = syscall_fd_create2(c); break;
	case 6:  error = syscall_fd_datasync(c); break;
	case 7:  error = syscall_fd_dup(c); break;
	case 8:  error = syscall_fd_pread(c); break;
	case 9:  error = syscall_fd_pwrite(c); break;
	case 10: error = syscall_fd_read(c); break;
	case 11: error = syscall_fd_replace(c); break;
	case 12: error = syscall_fd_seek(c); break;
	case 13: error = syscall_fd_stat_get(c); break;
	case 14: error = syscall_fd_stat_put(c); break;
	case 15: error = syscall_fd_sync(c); break;
	case 16: error = syscall_fd_write(c); break;
	case 17: error = syscall_file_advise(c); break;
	case 18: error = syscall_file_allocate(c); break;
	case 19: error = syscall_file_create(c); break;
	case 20: error = syscall_file_link(c); break;
	case 21: error = syscall_file_open(c); break;
	case 22: error = syscall_file_readdir(c); break;
	case 23: error = syscall_file_readlink(c); break;
	case 24: error = syscall_file_rename(c); break;
	case 25: error = syscall_file_stat_fget(c); break;
	case 26: error = syscall_file_stat_fput(c); break;
	case 27: error = syscall_file_stat_get(c); break;
	case 28: error = syscall_file_stat_put(c); break;
	case 29: error = syscall_file_symlink(c); break;
	case 30: error = syscall_file_unlink(c); break;
	case 31: error = syscall_lock_unlock(c); break;
	case 32: error = syscall_mem_advise(c); break;
	case 33: error = syscall_mem_map(c); break;
	case 34: error = syscall_mem_protect(c); break;
	case 35: error = syscall_mem_sync(c); break;
	case 36: error = syscall_mem_unmap(c); break;
	case 37: error = syscall_poll(c); break;
	case 38: error = syscall_poll_fd(c); break;
	case 39: error = syscall_proc_exec(c); break;
	case 40: error = syscall_proc_exit(c); break;
	case 41: error = syscall_proc_fork(c); break;
	case 42: error = syscall_proc_raise(c); break;
	case 43: error = syscall_random_get(c); break;
	case 44: error = syscall_sock_accept(c); break;
	case 45: error = syscall_sock_bind(c); break;
	case 46: error = syscall_sock_connect(c); break;
	case 47: error = syscall_sock_listen(c); break;
	case 48: error = syscall_sock_recv(c); break;
	case 49: error = syscall_sock_send(c); break;
	case 50: error = syscall_sock_shutdown(c); break;
	case 51: error = syscall_sock_stat_get(c); break;
	case 52: error = syscall_thread_create(c); break;
	case 53: error = syscall_thread_exit(c); break;
	case 54: error = syscall_thread_yield(c); break;
	default:
		get_vga_stream() << "Syscall " << state.eax << " unknown, signalling process\n";
		process->signal(CLOUDABI_SIGSYS);
		error = ENOSYS;
	}

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
