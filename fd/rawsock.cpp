#include <fd/rawsock.hpp>
#include <net/interface.hpp>

using namespace cloudos;

rawsock::rawsock(interface *i, const char *n)
: sock_t(CLOUDABI_FILETYPE_SOCKET_DGRAM, n)
, iface(i)
{
	status = sockstatus_t::CONNECTED;
}

rawsock::~rawsock()
{
}

void rawsock::init()
{
	iface->subscribe(weak_from_this());
}

void rawsock::sock_shutdown(cloudabi_sdflags_t how)
{
	// ignore CLOUDABI_SHUT_RD
	if(how & CLOUDABI_SHUT_WR) {
		status = sockstatus_t::SHUTDOWN;
	}
	error = 0;
}

void rawsock::sock_stat_get(cloudabi_sockstat_t* buf, cloudabi_ssflags_t flags)
{
	assert(buf);
	buf->ss_peername.sa_family = CLOUDABI_AF_INET;
	buf->ss_error = error;
	buf->ss_state = 0;

	if(flags & CLOUDABI_SOCKSTAT_CLEAR_ERROR) {
		error = 0;
	}
}

void rawsock::sock_recv(const cloudabi_recv_in_t* in, cloudabi_recv_out_t *out)
{
	while(messages != nullptr) {
		read_cv.wait();
	}

	Blk message_buf = messages->data;
	messages = messages->next;

	// TODO: a generic function to copy iovecs/linked-list-of-buffers over
	// iovecs
	char *buffer = reinterpret_cast<char*>(message_buf.ptr);
	size_t datalen = 0;
	size_t buffer_size_remaining = message_buf.size;
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

	out->ro_datalen = datalen;
	out->ro_fdslen = 0;
	out->ro_sockname.sa_family = CLOUDABI_AF_INET;
	out->ro_peername.sa_family = CLOUDABI_AF_INET;
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
	uint8_t *message = reinterpret_cast<uint8_t*>(message_blk.ptr);

	size_t read = 0;
	for(size_t i = 0; i < in->si_data_len; ++i) {
		size_t buf_len = in->si_data[i].buf_len;
		memcpy(message + read, in->si_data[i].buf, buf_len);
		read += buf_len;
	}

	assert(read == size);
	error = iface->send_frame(message, size);
	out->so_datalen = size;
	deallocate(message_blk);
}

void rawsock::interface_recv(uint8_t *frame, size_t frame_length, protocol_t, size_t)
{
	Blk b = allocate(frame_length);
	memcpy(b.ptr, frame, frame_length);
	linked_list<Blk> *item = allocate<linked_list<Blk>>(b);
	append(&messages, item);
	read_cv.notify();
}
