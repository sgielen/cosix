#pragma once
#include <fd/fd.hpp>
#include <oslibc/list.hpp>
#include <fd/process_fd.hpp>

namespace cloudos {

struct sock_t : public fd_t {
	sock_t(cloudabi_filetype_t sockettype, const char *n);

	enum sockstatus_t {
		// If this socket is in SHUTDOWN, it cannot send() anymore, but
		// can still recv(). If othersock->status is SHUTDOWN, we can
		// send(), but the other side can't.
		IDLE, CONNECTING, CONNECTED, SHUTDOWN
	};

	size_t read(void *dest, size_t count) override;
	size_t write(const char *str, size_t count) override;

	void sock_shutdown(cloudabi_sdflags_t /*how*/) override
	{
		if(status == sockstatus_t::CONNECTED) {
			error = EINVAL;
		} else {
			error = ENOTCONN;
		}
	}

	void sock_recv(const cloudabi_recv_in_t* /*in*/, cloudabi_recv_out_t* /*out*/) override
	{
		if(status == sockstatus_t::CONNECTED || status == sockstatus_t::SHUTDOWN) {
			error = EINVAL;
		} else {
			error = ENOTCONN;
		}
	}

	void sock_send(const cloudabi_send_in_t* /*in*/, cloudabi_send_out_t* /*out*/) override
	{
		if(status == sockstatus_t::SHUTDOWN) {
			error = EPIPE;
		} else if(status == sockstatus_t::CONNECTED) {
			error = EINVAL;
		} else {
			error = ENOTCONN;
		}
	}

protected:
	sockstatus_t status = sockstatus_t::IDLE;
};

}
