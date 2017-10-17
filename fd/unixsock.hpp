#pragma once
#include <concur/condition.hpp>
#include <fd/process_fd.hpp>
#include <fd/sock.hpp>
#include <oslibc/list.hpp>

namespace cloudos {

struct unixsock_message;
typedef linked_list<unixsock_message*> unixsock_message_list;

struct unixsock_message {
	Blk buf;
	size_t stream_data_recv = 0;
	// fd_mapping_t contains a shared_ptr<fd_t>. With shared_ptr, fd's
	// survive in-flight (i.e. they are not destructed when a close()
	// happens between send() and recv()).
	linked_list<fd_mapping_t> *fd_list = nullptr;
};

struct unixsock : public sock_t, public enable_shared_from_this<unixsock> {
	unixsock(cloudabi_filetype_t sockettype, const char *n);
	~unixsock() override;

	size_t bytes_readable() const;
	bool has_messages() const;
	bool is_writeable();
	cloudabi_errno_t get_read_signaler(thread_condition_signaler **s) override;
	cloudabi_errno_t get_write_signaler(thread_condition_signaler **s) override;

	void socketpair(shared_ptr<unixsock> other);

	size_t read(void *dest, size_t count) override;
	size_t write(const char *str, size_t count) override;

	void sock_shutdown(cloudabi_sdflags_t how) override;
	void sock_recv(const cloudabi_recv_in_t* in, cloudabi_recv_out_t *out) override;
	void sock_send(const cloudabi_send_in_t* in, cloudabi_send_out_t *out) override;

protected:
	// This function is called by another unixsock when bytes were just added to
	// this sock's recv_messages / num_recv_bytes. Because it's virtual, this allows
	// creating unixsocks with additional behaviour when bytes are received, such as
	// the reverse_fd.
	virtual void have_bytes_received();

private:
	weak_ptr<unixsock> othersock;

	static constexpr size_t MAX_SIZE_BUFFERS = 1024 * 1024;
	static constexpr size_t MAX_FD_PER_MESSAGE = 20;

	size_t num_recv_bytes = 0;
	unixsock_message_list *recv_messages = nullptr;
	cv_t recv_messages_cv;
	thread_condition_signaler recv_signaler;
	thread_condition_signaler send_signaler;
};

}
