#pragma once
#include <fd/sock.hpp>
#include <oslibc/list.hpp>
#include <fd/process_fd.hpp>

namespace cloudos {

struct ifstoresock : public sock_t {
	ifstoresock(const char *n);
	~ifstoresock() override;

	void sock_shutdown(cloudabi_sdflags_t how) override;
	void sock_stat_get(cloudabi_sockstat_t* buf, cloudabi_ssflags_t flags) override;
	void sock_recv(const cloudabi_recv_in_t* in, cloudabi_recv_out_t *out) override;
	void sock_send(const cloudabi_send_in_t* in, cloudabi_send_out_t *out) override;

private:
	bool has_message = false;
	Blk message_buf;
	linked_list<fd_mapping_t> *message_fds = nullptr;
	cv_t read_cv;
	cv_t write_cv;
};

}
