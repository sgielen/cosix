#include <fd/unixsock.hpp>
#include <fd/scheduler.hpp>

using namespace cloudos;

unixsock_listen_store::~unixsock_listen_store() {
	remove_all(&socks, [&](unixsock_list *) {
		return true;
	});
}

void unixsock_listen_store::register_unixsock(cloudabi_device_t dev, cloudabi_inode_t inode, shared_ptr<unixsock> listensock) {
	assert(!get_unixsock(dev, inode));

	auto *item = allocate<unixsock_list>(unixsock_registration{dev, inode, listensock});
	append(&socks, item);
	assert(get_unixsock(dev, inode));
}

void unixsock_listen_store::unregister_unixsock(cloudabi_device_t dev, cloudabi_inode_t inode) {
	remove_one(&socks, [&](unixsock_list *item) {
		return item->data.dev == dev && item->data.inode == inode;
	});
	assert(!get_unixsock(dev, inode));
}

shared_ptr<unixsock> unixsock_listen_store::get_unixsock(cloudabi_device_t dev, cloudabi_inode_t inode) {
	for(auto *s = socks; s; s = s->next) {
		if(s->data.dev == dev && s->data.inode == inode) {
			auto res = s->data.listensock.lock();
			assert(res);
			return res;
		}
	}
	return nullptr;
}

unixsock::unixsock(cloudabi_filetype_t sockettype, const char *n)
: sock_t(sockettype, n)
{
}

unixsock::~unixsock()
{
	if(status == sockstatus_t::LISTENING) {
		get_unixsock_listen_store()->unregister_unixsock(listen_device, listen_inode);

		remove_all(&listenqueue, [&](unixsock_list *) {
			return true;
		});
		backlog = 0;
	}

	// closing a socket while it's in the sock_connect() could be possible,
	// but the object is kept alive in the kernel until the listen socket
	// closes as well. At that point, the socket is set back to IDLE with
	// error set, after which it would be destructed.
	assert(status != sockstatus_t::CONNECTING);

	if(status == sockstatus_t::CONNECTED
	|| status == sockstatus_t::SHUTDOWN) {
		sock_shutdown(CLOUDABI_SHUT_RD | CLOUDABI_SHUT_WR);
		auto o = othersock.lock();
		if(o) {
			o->error = ECONNRESET;
		}
		remove_all(&recv_messages, [&](unixsock_message_list *) {
			return true;
		}, [&](unixsock_message_list *item) {
			remove_all(&item->data->fd_list, [&](linked_list<fd_mapping_t> *) {
				return true;
			});
			deallocate(item->data->buf);
			deallocate(item->data);
			deallocate(item);
		});
	}

	assert(recv_messages == nullptr);
}

void unixsock::socketpair(shared_ptr<unixsock> other)
{
	assert(status == sockstatus_t::IDLE);
	assert(other->status == sockstatus_t::IDLE);
	assert(type == other->type);

	status = other->status = sockstatus_t::CONNECTED;
	othersock = other;
	other->othersock = weak_from_this();
}

size_t unixsock::read(void *dest, size_t count)
{
	cloudabi_iovec_t iovec[1];
	iovec[0].buf = dest;
	iovec[0].buf_len = count;

	cloudabi_recv_in_t recv_in[1];
	recv_in[0].ri_data = &iovec[0];
	recv_in[0].ri_data_len = 1;
	recv_in[0].ri_fds = nullptr;
	recv_in[0].ri_fds_len = 0;
	recv_in[0].ri_flags = 0;

	cloudabi_recv_out_t recv_out[1];
	recv_out[0].ro_datalen = 0;
	recv_out[0].ro_fdslen = 0;

	sock_recv(recv_in, recv_out);
	assert(recv_out[0].ro_fdslen == 0);
	return recv_out[0].ro_datalen;
}

size_t unixsock::write(const char *str, size_t count)
{
	cloudabi_ciovec_t iovec[1];
	iovec[0].buf = str;
	iovec[0].buf_len = count;

	cloudabi_send_in_t send_in[1];
	send_in[0].si_data = &iovec[0];
	send_in[0].si_data_len = 1;
	send_in[0].si_fds = nullptr;
	send_in[0].si_fds_len = 0;
	send_in[0].si_flags = 0;

	cloudabi_send_out_t send_out[1];
	send_out[0].so_datalen = 0;

	sock_send(send_in, send_out);
	return send_out[0].so_datalen;
}

void unixsock::sock_bind(cloudabi_sa_family_t family, shared_ptr<fd_t> fd, void *a, size_t address_len)
{
	if(status != sockstatus_t::IDLE) {
		error = EINVAL;
		return;
	}
	if(family != CLOUDABI_AF_UNIX) {
		error = EAFNOSUPPORT;
		return;
	}
	assert(fd);
	char *path = reinterpret_cast<char*>(a);
	auto inode = fd->file_create(path, address_len, type);
	if(fd->error == EEXIST) {
		error = EADDRINUSE;
		return;
	} else if(fd->error) {
		error = fd->error;
		return;
	} else if(fd->device == 0 /* TODO: or file system is not local */) {
		// it's possible fd->device was not yet known; after file_create
		// it must be known
		error = EADDRNOTAVAIL;
		return;
	}
	listen_device = fd->device;
	listen_inode = inode;
	assert(listen_device > 0);
	status = sockstatus_t::BOUND;
	error = 0;
}

void unixsock::sock_connect(cloudabi_sa_family_t family, shared_ptr<fd_t> fd, void *address, size_t address_len)
{
	if(status == sockstatus_t::CONNECTING || status == sockstatus_t::CONNECTED || status == sockstatus_t::SHUTDOWN) {
		error = EISCONN;
		return;
	}
	if(status == sockstatus_t::BOUND || status == sockstatus_t::LISTENING) {
		/* TODO: POSIX says we should use EOPNOTSUPP here, but
		 * it is not set in CloudABI, so EINVAL */
		error = EINVAL;
		return;
	}
	assert(status == sockstatus_t::IDLE);

	if(family != CLOUDABI_AF_UNIX) {
		error = EAFNOSUPPORT;
		return;
	}

	cloudabi_filestat_t stat;
	status = sockstatus_t::CONNECTING;
	fd->file_stat_get(CLOUDABI_LOOKUP_SYMLINK_FOLLOW,
		reinterpret_cast<const char*>(address), address_len, &stat);
	if(fd->error) {
		error = fd->error;
		status = sockstatus_t::IDLE;
		return;
	}

	if(stat.st_filetype != type) {
		error = EPROTOTYPE;
		status = sockstatus_t::IDLE;
		return;
	}

	auto device = stat.st_dev;
	auto inode = stat.st_ino;

	assert(device == fd->device);
	auto listensock = get_unixsock_listen_store()->get_unixsock(device, inode);
	if(!listensock) {
		error = ECONNREFUSED;
		status = sockstatus_t::IDLE;
		return;
	}

	error = 0;
	assert(status == sockstatus_t::CONNECTING);
	listensock->queue_connect(shared_from_this());
	if(status == sockstatus_t::CONNECTED) {
		assert(error == 0);
		return;
	} else {
		// status and error were set by the one who rejected the connect
		assert(status == sockstatus_t::IDLE);
		assert(error != 0);
		return;
	}
}

void unixsock::sock_listen(cloudabi_backlog_t b)
{
	if(status == sockstatus_t::IDLE) {
		error = EDESTADDRREQ;
		return;
	}
	if(status == sockstatus_t::CONNECTING || status == sockstatus_t::CONNECTED || status == sockstatus_t::SHUTDOWN) {
		error = EINVAL;
		return;
	}
	if(b == 0) {
		b = -1;
	}
	assert(status == sockstatus_t::BOUND || status == sockstatus_t::LISTENING);
	if(status == sockstatus_t::BOUND) {
		get_unixsock_listen_store()->register_unixsock(listen_device, listen_inode, shared_from_this());
		status = sockstatus_t::LISTENING;
		assert(backlog == 0);
		backlog = b;
	} else {
		if(size(listenqueue) <= b) {
			backlog = b;
		} else {
			unixsock_list *q = listenqueue;
			size_t skip = b;
			// skip b-1 connections so that we arrive at the
			// last one that will survive
			while(--skip > 0) {
				q = q->next;
				assert(q);
			}
			auto *n = q->next;
			q->next = nullptr;
			// drop the rest
			while(n) {
				auto *next = n->next;
				deallocate(n);
				n = next;
			}
			assert(size(listenqueue) == b);
			backlog = b;
		}
	}
}

void unixsock::queue_connect(shared_ptr<unixsock> connectingsock)
{
	assert(type == connectingsock->type);
	if(size(listenqueue) == backlog) {
		connectingsock->status = sockstatus_t::IDLE;
		connectingsock->error = ECONNREFUSED;
		return;
	}

	// In unix sockets, connect() by default returns immediately and
	// creates an accepting socket for accept() to return. On most OSes,
	// this can be changed with a setsockopt, but CloudABI doesn't have
	// setsockopt, so this is the only mode we support.
	assert(status == sockstatus_t::LISTENING);
	assert(connectingsock->status == sockstatus_t::CONNECTING);
	auto accepting = make_shared<unixsock>(type, "accepted unixsock");
	connectingsock->status = accepting->status = sockstatus_t::CONNECTED;
	connectingsock->othersock = accepting;
	accepting->othersock = connectingsock;

	auto item = allocate<unixsock_list>(accepting);
	append(&listenqueue, item);
	listenqueue_cv.notify();
}

shared_ptr<fd_t> unixsock::sock_accept(cloudabi_sa_family_t family, void *, size_t *address_len)
{
	if(status != sockstatus_t::LISTENING) {
		error = EINVAL;
		return nullptr;
	}
	if(family != CLOUDABI_AF_UNIX) {
		error = EAFNOSUPPORT;
		return nullptr;
	}

	while(listenqueue == nullptr) {
		listenqueue_cv.wait();
	}
	unixsock_list *item = listenqueue;
	listenqueue = item->next;
	shared_ptr<unixsock> accepting = item->data;
	deallocate(item);

	assert(accepting->type == type);
	assert(accepting->status == sockstatus_t::CONNECTED || accepting->status == sockstatus_t::SHUTDOWN);

	error = 0;
	if(address_len != nullptr) {
		*address_len = 0;
	}
	return accepting;
}

void unixsock::sock_shutdown(cloudabi_sdflags_t how)
{
	if(status != sockstatus_t::CONNECTED) {
		error = ENOTCONN;
		return;
	}
	auto other = othersock.lock();
	if(other && (how & CLOUDABI_SHUT_RD)) {
		other->sock_shutdown(CLOUDABI_SHUT_WR);
	}
	if(how & CLOUDABI_SHUT_WR) {
		status = sockstatus_t::SHUTDOWN;
	}
	error = 0;
}

void unixsock::sock_stat_get(cloudabi_sockstat_t* buf, cloudabi_ssflags_t flags)
{
	assert(buf);
	buf->ss_sockname.sa_family = CLOUDABI_AF_UNIX;
	if(status == sockstatus_t::CONNECTING || status == sockstatus_t::CONNECTED) {
		buf->ss_peername.sa_family = CLOUDABI_AF_UNIX;
	} else {
		buf->ss_peername.sa_family = CLOUDABI_AF_UNSPEC;
	}
	buf->ss_error = error;
	buf->ss_state = status == sockstatus_t::LISTENING ? CLOUDABI_SOCKSTATE_ACCEPTCONN : 0;

	if(flags & CLOUDABI_SOCKSTAT_CLEAR_ERROR) {
		error = 0;
	}
}

void unixsock::sock_recv(const cloudabi_recv_in_t* in, cloudabi_recv_out_t *out)
{
	out->ro_sockname.sa_family = CLOUDABI_AF_UNIX;
	out->ro_peername.sa_family = CLOUDABI_AF_UNIX;
	out->ro_flags = 0;
	out->ro_datalen = 0;
	out->ro_fdslen = 0;

	if(status != sockstatus_t::CONNECTED && status != sockstatus_t::SHUTDOWN) {
		error = ENOTCONN;
		return;
	}

	assert(type == CLOUDABI_FILETYPE_SOCKET_DGRAM
	    || type == CLOUDABI_FILETYPE_SOCKET_STREAM);

	if(recv_messages == nullptr) {
		auto other = othersock.lock();
		if(!other) {
			// othersock is already destroyed
			error = 0;
			return;
		}
		assert(other->othersock.lock().get() == this);

		assert(other->status == sockstatus_t::CONNECTED || other->status == sockstatus_t::SHUTDOWN);

		// wait until there is at least one more message
		while(other->status == sockstatus_t::CONNECTED && recv_messages == nullptr) {
			recv_messages_cv.wait();
		}

		if(recv_messages == nullptr) {
			// other socket is in shutdown and there are no messages
			error = 0;
			return;
		}
	}
	assert(recv_messages);

	if(type == CLOUDABI_FILETYPE_SOCKET_DGRAM) {
		// Datagram receiving: take next message; fill current buffers
		// with only it
		auto item = recv_messages;
		auto message = item->data;
		recv_messages = item->next;
		deallocate(item);

		char *buffer = reinterpret_cast<char*>(message->buf.ptr);
		size_t datalen = 0;
		size_t buffer_size_remaining = message->buf.size;
		for(size_t i = 0; i < in->ri_data_len; ++i) {
			auto &iovec = in->ri_data[i];
			if(iovec.buf_len < buffer_size_remaining) {
				memcpy(iovec.buf, buffer, iovec.buf_len);
				datalen += iovec.buf_len;
				buffer += iovec.buf_len;
				buffer_size_remaining -= iovec.buf_len;
			} else {
				memcpy(iovec.buf, buffer, buffer_size_remaining);
				datalen += buffer_size_remaining;
				buffer_size_remaining = 0;
				break;
			}
		}

		if(buffer_size_remaining > 0) {
			// TODO: message is truncated
		}

		auto *fd_item = message->fd_list;
		size_t fds_set = 0;
		auto process = get_scheduler()->get_running_thread()->get_process();
		while(fd_item) {
			if(fds_set < in->ri_fds_len) {
				fd_mapping_t &fd_map = fd_item->data;
				in->ri_fds[fds_set] = process->add_fd(fd_map.fd, fd_map.rights_base, fd_map.rights_inheriting);
				fds_set++;
			}
			auto d = fd_item;
			fd_item = fd_item->next;
			deallocate(d);
		}

		// TODO: what if fds are truncated? move to next message?
		out->ro_datalen = datalen;
		out->ro_fdslen = fds_set;
		error = 0;

		deallocate(message->buf);
		deallocate(message);
	} else if(type == CLOUDABI_FILETYPE_SOCKET_STREAM) {
		// Stream receiving: while the current buffers aren't full,
		// fill them with parts of the next message, taking them off
		// the list when they are fully consumed

		auto *recv_message = recv_messages;
		size_t total_written = 0;
		for(size_t i = 0; i < in->ri_data_len && recv_message; ++i) {
			auto &iovec = in->ri_data[i];
			size_t written = 0;
			while(written < iovec.buf_len && recv_message) {
				auto *body = recv_message->data;
				assert(body->buf.size >= body->stream_data_recv);
				size_t message_remaining = body->buf.size - body->stream_data_recv;
				if(message_remaining > 0) {
					size_t iovec_remaining = iovec.buf_len - written;
					size_t copy = iovec_remaining < message_remaining ? iovec_remaining : message_remaining;
					memcpy(reinterpret_cast<char*>(iovec.buf) + written,
						reinterpret_cast<char*>(body->buf.ptr) + body->stream_data_recv, copy);
					body->stream_data_recv += copy;
					written += copy;
					message_remaining -= copy;
				}
				if(message_remaining == 0) {
					// no data left in this buffer
					recv_message = recv_message->next;
				}
			}
			total_written += written;
		}

		recv_message = recv_messages;
		size_t fds_set = 0;
		auto process = get_scheduler()->get_running_thread()->get_process();
		while(fds_set < in->ri_fds_len && recv_message) {
			auto *body = recv_message->data;
			auto *fd_item = body->fd_list;
			while(fds_set < in->ri_fds_len && fd_item) {
				fd_mapping_t &fd_map = fd_item->data;
				in->ri_fds[fds_set] = process->add_fd(fd_map.fd, fd_map.rights_base, fd_map.rights_inheriting);
				fds_set++;
				auto d = fd_item;
				fd_item = fd_item->next;
				deallocate(d);
			}
			body->fd_list = fd_item;
			recv_message = recv_message->next;
		}

		// deallocate all messages without fd's or body
		remove_all(&recv_messages, [&](unixsock_message_list *item) {
			auto *body = item->data;
			assert(body->buf.size >= body->stream_data_recv);
			return body->buf.size == body->stream_data_recv && body->fd_list == nullptr;
		}, [&](unixsock_message_list *item) {
			deallocate(item->data->buf);
			deallocate(item->data);
			deallocate(item);
		});

		out->ro_datalen = total_written;
		out->ro_fdslen = fds_set;
		error = 0;
	}
}

void unixsock::sock_send(const cloudabi_send_in_t* in, cloudabi_send_out_t *out)
{
	if(status == sockstatus_t::SHUTDOWN) {
		error = EPIPE;
		return;
	}
	if(status != sockstatus_t::CONNECTED) {
		error = ENOTCONN;
		return;
	}
	auto other = othersock.lock();
	assert(other);
	assert(other->othersock.lock());

	assert(type == CLOUDABI_FILETYPE_SOCKET_DGRAM
	    || type == CLOUDABI_FILETYPE_SOCKET_STREAM);

	size_t total_message_size = 0;
	for(size_t i = 0; i < in->si_data_len; ++i) {
		const cloudabi_ciovec_t &data = in->si_data[i];
		// TODO: guard against overflow
		total_message_size += data.buf_len;
	}

	if(total_message_size + other->num_recv_bytes > MAX_SIZE_BUFFERS) {
		// TODO: block for the messages to be read off the socket?
		error = ENOBUFS;
		return;
	}

	auto *message = allocate<unixsock_message>();
	message->buf = allocate(total_message_size);

	char *buffer = reinterpret_cast<char*>(message->buf.ptr);
	for(size_t i = 0; i < in->si_data_len; ++i) {
		const cloudabi_ciovec_t &data = in->si_data[i];
		memcpy(buffer, data.buf, data.buf_len);
		buffer += data.buf_len;
	}
	assert(buffer == reinterpret_cast<char*>(message->buf.ptr) + message->buf.size);

	auto process = get_scheduler()->get_running_thread()->get_process();
	for(size_t i = 0; i < in->si_fds_len; ++i) {
		cloudabi_fd_t fdnum = in->si_fds[i];
		fd_mapping_t *fd_mapping;
		error = process->get_fd(&fd_mapping, fdnum, 0);
		if(error != 0) {
			deallocate(message->buf);
			deallocate(message);
			return;
		}
		fd_mapping_t fd_mapping_copy = *fd_mapping;
		auto *fd_item = allocate<linked_list<fd_mapping_t>>(fd_mapping_copy);
		append(&message->fd_list, fd_item);
	}

	auto *message_item = allocate<unixsock_message_list>(message);
	append(&other->recv_messages, message_item);
	other->num_recv_bytes += total_message_size;
	other->recv_messages_cv.notify();
	out->so_datalen = total_message_size;
	error = 0;
}
