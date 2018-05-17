#include <fd/rawsock.hpp>
#include <net/interface.hpp>
#include <oslibc/iovec.hpp>

using namespace cloudos;

static bool rawsock_is_readable(void *r, thread_condition*, thread_condition_data**) {
	auto *rawsock = reinterpret_cast<struct rawsock*>(r);
	return rawsock->has_messages();
}

rawsock::rawsock(interface *i, cloudabi_fdflags_t f, const char *n)
: sock_t(CLOUDABI_FILETYPE_SOCKET_DGRAM, f, n)
, iface(i)
{
	status = sockstatus_t::CONNECTED;
	read_signaler.set_already_satisfied_function(rawsock_is_readable, this);
}

rawsock::~rawsock()
{
}

void rawsock::init()
{
	iface->subscribe(weak_from_this());
}

bool rawsock::has_messages() const
{
	return messages != nullptr;
}

void rawsock::sock_shutdown(cloudabi_sdflags_t how)
{
	// ignore CLOUDABI_SHUT_RD
	if(how & CLOUDABI_SHUT_WR) {
		status = sockstatus_t::SHUTDOWN;
	}
	error = 0;
}

void rawsock::sock_recv(const cloudabi_recv_in_t* in, cloudabi_recv_out_t *out)
{
	if(messages == nullptr && (flags & CLOUDABI_FDFLAG_NONBLOCK)) {
		error = EAGAIN;
		return;
	}

	while(messages == nullptr) {
		read_cv.wait();
	}

	auto *message = messages;
	messages = message->next;
	Blk message_buf = message->data;
	deallocate(message);

	size_t bytes_copied = veccpy(in->ri_data, in->ri_data_len, message_buf, 0);
	if(bytes_copied < message_buf.size) {
		// TODO: message is truncated
	}

	out->ro_datalen = bytes_copied;
	out->ro_fdslen = 0;
	out->ro_flags = 0;

	deallocate(message_buf);
	error = 0;
}

void rawsock::sock_send(const cloudabi_send_in_t* in, cloudabi_send_out_t *out)
{
	if(status == sockstatus_t::SHUTDOWN) {
		error = EPIPE;
		return;
	}
	assert(status == sockstatus_t::CONNECTED);

	size_t size = 0;
	for(size_t i = 0; i < in->si_data_len; ++i) {
		size += in->si_data[i].buf_len;
	}

	if(size > 1500 /* TODO: mtu */) {
		error = EMSGSIZE;
		return;
	}

	Blk message_blk = allocate(size);
	size_t bytes_copied = veccpy(message_blk, in->si_data, in->si_data_len, 0);
	assert(bytes_copied == size);

	error = iface->send_frame(reinterpret_cast<uint8_t*>(message_blk.ptr), size);
	out->so_datalen = size;
	deallocate(message_blk);
}

void rawsock::frame_received(uint8_t *frame, size_t frame_length)
{
	Blk b = allocate(frame_length);
	memcpy(b.ptr, frame, frame_length);
	linked_list<Blk> *item = allocate<linked_list<Blk>>(b);
	append(&messages, item);
	read_signaler.condition_broadcast();
	read_cv.notify();
}

cloudabi_errno_t rawsock::get_read_signaler(thread_condition_signaler **s)
{
	*s = &read_signaler;
	return 0;
}
