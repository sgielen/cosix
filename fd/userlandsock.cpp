#include <fd/userlandsock.hpp>
#include <fd/pseudo_fd.hpp>
#include <fd/rawsock.hpp>
#include <fd/reverse_fd.hpp>
#include <fd/scheduler.hpp>
#include <fd/unixsock.hpp>
#include <oslibc/numeric.h>
#include <oslibc/string.h>
#include <oslibc/iovec.hpp>

using namespace cloudos;

userlandsock::userlandsock(const char *n)
: sock_t(CLOUDABI_FILETYPE_SOCKET_DGRAM, 0, n)
{
	status = sockstatus_t::CONNECTED;
}

userlandsock::~userlandsock()
{
	if(has_message) {
		clear_response();
	}
}

void userlandsock::sock_shutdown(cloudabi_sdflags_t how)
{
	// ignore CLOUDABI_SHUT_RD
	if(how & CLOUDABI_SHUT_WR) {
		status = sockstatus_t::SHUTDOWN;
	}
	error = 0;
}

void userlandsock::sock_recv(const cloudabi_recv_in_t* in, cloudabi_recv_out_t *out)
{
	if(!has_message && (flags & CLOUDABI_FDFLAG_NONBLOCK)) {
		error = EAGAIN;
		return;
	}

	while(!has_message) {
		read_cv.wait();
	}

	size_t bytes_copied = veccpy(in->ri_data, in->ri_data_len, message_buf, 0);
	out->ro_flags = bytes_copied < message_buf.size ? CLOUDABI_SOCK_RECV_DATA_TRUNCATED : 0;

	size_t fds_set = 0;
	while(message_fds != nullptr && fds_set < in->ri_fds_len) {
		in->ri_fds[fds_set] = message_fds->data;
		fds_set++;

		auto d = message_fds;
		message_fds = message_fds->next;
		deallocate(d);
	}

	if(message_fds != nullptr) {
		out->ro_flags |= CLOUDABI_SOCK_RECV_FDS_TRUNCATED;
	}

	out->ro_datalen = bytes_copied;
	out->ro_fdslen = fds_set;

	clear_response();
	error = 0;
}

void userlandsock::sock_send(const cloudabi_send_in_t* in, cloudabi_send_out_t *out)
{
	if(status == sockstatus_t::SHUTDOWN) {
		error = EPIPE;
		return;
	}
	assert(status == sockstatus_t::CONNECTED);

	if(has_message && (flags & CLOUDABI_FDFLAG_NONBLOCK)) {
		error = EAGAIN;
		return;
	}

	while(has_message) {
		write_cv.wait();
	}

	char message[80];

	size_t read = 0;
	for(size_t i = 0; i < in->si_data_len; ++i) {
		size_t remaining = sizeof(message) - read - 1 /* terminator */;
		size_t buf_len = in->si_data[i].buf_len;
		if(buf_len > remaining) {
			error = EMSGSIZE;
			return;
		}
		memcpy(message + read, in->si_data[i].buf, buf_len);
		read += buf_len;
	}

	assert(read < sizeof(message));
	out->so_datalen = read;
	message[read] = 0;

	char *command = message;
	char *arg = strsplit(command, ' ');

	handle_command(command, arg);
	assert(has_message);
	error = 0;
}

void userlandsock::set_response(const char *response)
{
	assert(!has_message);
	assert(message_fds == nullptr);

	message_buf = allocate(strlen(response));
	memcpy(message_buf.ptr, response, message_buf.size);
	has_message = true;
	read_cv.notify();
}

void userlandsock::add_fd_to_response(int fd)
{
	append(&message_fds, allocate<linked_list<int>>(fd));
}

void userlandsock::clear_response()
{
	assert(has_message);

	while(message_fds != nullptr) {
		auto d = message_fds;
		message_fds = message_fds->next;
		deallocate(d);
	}

	deallocate(message_buf);
	has_message = false;
	write_cv.notify();
}
