#pragma once
#include <fd/sock.hpp>
#include <oslibc/list.hpp>
#include <fd/process_fd.hpp>

namespace cloudos {

/**
 * A utility base class for a socket that can be used for communication with userland
 * processes.
 */
struct userlandsock : public sock_t {
	userlandsock(const char *n);
	~userlandsock() override;

	void sock_shutdown(cloudabi_sdflags_t how) override;
	void sock_recv(const cloudabi_recv_in_t* in, cloudabi_recv_out_t *out) override;
	void sock_send(const cloudabi_send_in_t* in, cloudabi_send_out_t *out) override;

protected:
	virtual void handle_command(const char *cmd, const char *parameter) = 0;

	void set_response(const char *message);
	void add_fd_to_response(int fd);

private:
	void clear_response();

	bool has_message = false;
	Blk message_buf;
	linked_list<int> *message_fds = nullptr;
	cv_t read_cv;
	cv_t write_cv;
};

}
