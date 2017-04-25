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
		IDLE, BOUND, LISTENING, CONNECTING, CONNECTED, SHUTDOWN
	};

	size_t read(void *dest, size_t count) override;
	void putstring(const char *str, size_t count) override;

protected:
	sockstatus_t status = sockstatus_t::IDLE;
};

}
