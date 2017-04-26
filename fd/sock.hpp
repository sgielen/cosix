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

	void sock_bind(cloudabi_sa_family_t /*family*/, shared_ptr<fd_t> /*fd*/, void * /*address*/, size_t /*address_len*/) override
	{
		error = EINVAL;
	}

	void sock_connect(cloudabi_sa_family_t /*family*/, shared_ptr<fd_t> /*fd*/, void * /*address*/, size_t /*address_len*/) override
	{
		if(status == sockstatus_t::CONNECTING || status == sockstatus_t::CONNECTED || status == sockstatus_t::SHUTDOWN) {
			error = EISCONN;
		} else {
			error = EINVAL;
		}
	}

	void sock_listen(cloudabi_backlog_t /*backlog*/) override
	{
		if(status == sockstatus_t::IDLE) {
			error = EDESTADDRREQ;
		} else {
			error = EINVAL;
		}
	}

	shared_ptr<fd_t> sock_accept(cloudabi_sa_family_t /*family*/, void * /*address*/, size_t* /*address_len*/) override
	{
		error = EINVAL;
		return nullptr;
	}

	void sock_shutdown(cloudabi_sdflags_t /*how*/) override
	{
		if(status == sockstatus_t::CONNECTED) {
			error = EINVAL;
		} else {
			error = ENOTCONN;
		}
	}

	void sock_stat_get(cloudabi_sockstat_t *buf, cloudabi_ssflags_t flags) override
	{
		assert(buf);
		buf->ss_peername.sa_family = CLOUDABI_AF_UNSPEC;
		buf->ss_error = error;
		buf->ss_state = status == sockstatus_t::LISTENING ? CLOUDABI_SOCKSTATE_ACCEPTCONN : 0;

		if(flags & CLOUDABI_SOCKSTAT_CLEAR_ERROR) {
			error = 0;
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
