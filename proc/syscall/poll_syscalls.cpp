#include <proc/syscalls.hpp>
#include <concur/condition.hpp>
#include <time/clock_store.hpp>
#include <global.hpp>
#include <fd/process_fd.hpp>

using namespace cloudos;

static bool return_true(void*, thread_condition*) {
	return true;
}

cloudabi_errno_t cloudos::syscall_poll(syscall_context &c)
{
	auto args = arguments_t<const cloudabi_subscription_t*, cloudabi_event_t*, size_t, size_t*>(c);
	auto in = args.first();
	auto out = args.second();
	size_t nsubscriptions = args.third();
	// There are a limited number of valid options for the contents of 'in':
	// - empty
	// - [0] lock_rdlock/lock_wrlock, and optionally [1] a clock
	// - [0] condvar, and optionally [1] a clock
	// - any number of clock/fd_read/fd_write/proc_terminate
	if(nsubscriptions == 0) {
		return 0;
	}
	cloudabi_eventtype_t first_event = in[0].type;
	if(first_event == CLOUDABI_EVENTTYPE_LOCK_RDLOCK
	|| first_event == CLOUDABI_EVENTTYPE_LOCK_WRLOCK
	|| first_event == CLOUDABI_EVENTTYPE_CONDVAR) {
		if(nsubscriptions == 2) {
			// second event must be clock
			if(in[1].type != CLOUDABI_EVENTTYPE_CLOCK) {
				return EINVAL;
			}
		} else if(nsubscriptions > 2) {
			// must be at most 2 events
			return EINVAL;
		}
	} else {
		// none of the events may be LOCK_RDLOCK/LOCK_WRLOCK/CONDVAR
		for(size_t i = 0; i < nsubscriptions; ++i) {
			auto type = in[i].type;
			if(type == CLOUDABI_EVENTTYPE_LOCK_RDLOCK
			|| type == CLOUDABI_EVENTTYPE_LOCK_WRLOCK
			|| type == CLOUDABI_EVENTTYPE_CONDVAR) {
				return EINVAL;
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
			return ENOSYS;
		}
		if(in[0].lock.lock_scope != CLOUDABI_SCOPE_PRIVATE) {
			get_vga_stream() << "poll(): non-private locks are not supported yet\n";
			return ENOSYS;
		}
		// this call blocks this thread until the lock is acquired
		c.thread->acquire_userspace_lock(lock, in[0].type);
		out[0].userdata = in[0].userdata;
		out[0].error = 0;
		out[0].type = in[0].type;
		c.result = 1;
		return 0;
	} else if(first_event == CLOUDABI_EVENTTYPE_CONDVAR) {
		// release the lock, wait() for the condvar, and
		// re-acquire the lock when it is notified, optionally
		// timing out when the timeout passes.
		auto *condvar = in[0].condvar.condvar;
		auto *lock = in[0].condvar.lock;
		if(nsubscriptions == 2) {
			get_vga_stream() << "poll(): clocks for condvars are not supported yet\n";
			return ENOSYS;
		}
		if(in[0].condvar.condvar_scope != CLOUDABI_SCOPE_PRIVATE || in[0].condvar.lock_scope != CLOUDABI_SCOPE_PRIVATE) {
			get_vga_stream() << "poll(): non-private locks or condvars are not supported yet\n";
			return ENOSYS;
		}
		// this call blocks this thread until the condition variable is notified and the lock is obtained
		c.thread->wait_userspace_cv(lock, condvar);
		out[0].userdata = in[0].userdata;
		out[0].error = 0;
		out[0].type = in[0].type;
		c.result = 1;
		return 0;
	}

	size_t nevents = 0;

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
		case CLOUDABI_EVENTTYPE_FD_READ: {
			auto fdnum = i.fd_readwrite.fd;
			fd_mapping_t *proc_mapping = nullptr;
			auto res = c.process()->get_fd(&proc_mapping, fdnum, CLOUDABI_RIGHT_POLL_FD_READWRITE | CLOUDABI_RIGHT_FD_READ);
			if(res != 0) {
				userdata->error = res;
				signaler = &null_signaler;
			} else {
				res = proc_mapping->fd->get_read_signaler(&signaler);
				if(res != 0) {
					userdata->error = res;
					signaler = &null_signaler;
				}
			}
			break;
		}
		case CLOUDABI_EVENTTYPE_FD_WRITE: {
			auto fdnum = i.fd_readwrite.fd;
			fd_mapping_t *proc_mapping = nullptr;
			auto res = c.process()->get_fd(&proc_mapping, fdnum, CLOUDABI_RIGHT_POLL_FD_READWRITE | CLOUDABI_RIGHT_FD_WRITE);
			if(res != 0) {
				userdata->error = res;
				signaler = &null_signaler;
			} else {
				res = proc_mapping->fd->get_write_signaler(&signaler);
				if(res == EINVAL) {
					// TODO: this eventtype is unimplemented for this FD type; for now,
					// assume fd is writeable
					signaler = &null_signaler;
				} else if(res != 0) {
					userdata->error = res;
					signaler = &null_signaler;
				}
			}
			break;
		}
		case CLOUDABI_EVENTTYPE_PROC_TERMINATE: {
			cloudabi_fd_t proc_fdnum = i.proc_terminate.fd;
			fd_mapping_t *proc_mapping;
			auto res = c.process()->get_fd(&proc_mapping, proc_fdnum, CLOUDABI_RIGHT_POLL_PROC_TERMINATE);
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
			break;
		}
		}
		assert(signaler);
		new (&condition) thread_condition(signaler);
		condition.userdata = reinterpret_cast<void*>(userdata);
		w.add_condition(&condition);
	}

	w.wait();

	thread_condition_list *satisfied = w.finish();
	iterate(satisfied, [&](thread_condition_list *item) {
		cloudabi_event_t &o = out[nevents++];
		assert(nevents <= nsubscriptions);

		thread_condition_userdata *userdata = reinterpret_cast<thread_condition_userdata*>(item->data->userdata);
		auto *i = userdata->subscription;
		o.userdata = i->userdata;
		o.type = i->type;
		o.error = userdata->error;
		if(i->type == CLOUDABI_EVENTTYPE_PROC_TERMINATE && o.error == 0) {
			cloudabi_fd_t proc_fdnum = i->proc_terminate.fd;
			// TODO: store signal and exitcode in the thread_condition when the process dies,
			// so that if we closed the fd in the meantime, we don't error out here
			fd_mapping_t *proc_mapping;
			auto res = c.process()->get_fd(&proc_mapping, proc_fdnum, 0);
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
		} else if(i->type == CLOUDABI_EVENTTYPE_FD_READ || i->type == CLOUDABI_EVENTTYPE_FD_WRITE) {
			// TODO set nbytes and flags correctly
			o.fd_readwrite.nbytes = o.error == 0 ? 0xffff : 0;
			o.fd_readwrite.flags = 0;
		}
	});
	remove_all(&satisfied, [](thread_condition_list*){
		return true;
	});
	for(size_t subi = 0; subi < nsubscriptions; ++subi) {
		thread_condition &condition = conditions[subi];
		deallocate(reinterpret_cast<thread_condition_userdata*>(condition.userdata));
		condition.~thread_condition();
	}
	deallocate(conditions_alloc);
	c.result = nevents;
	return 0;
}
