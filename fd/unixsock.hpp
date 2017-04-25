#pragma once
#include <fd/sock.hpp>
#include <oslibc/list.hpp>
#include <fd/process_fd.hpp>

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

struct unixsock;

struct unixsock_listen_store {
	~unixsock_listen_store();

	void register_unixsock(cloudabi_device_t dev, cloudabi_inode_t inode, shared_ptr<unixsock> listensock);
	void unregister_unixsock(cloudabi_device_t dev, cloudabi_inode_t inode);
	shared_ptr<unixsock> get_unixsock(cloudabi_device_t dev, cloudabi_inode_t inode);

private:
	struct unixsock_registration {
		cloudabi_device_t dev;
		cloudabi_inode_t inode;
		weak_ptr<unixsock> listensock;
	};
	typedef linked_list<unixsock_registration> unixsock_list;
	unixsock_list *socks = nullptr;
};

struct unixsock : public sock_t, public enable_shared_from_this<unixsock> {
	unixsock(cloudabi_filetype_t sockettype, const char *n);
	~unixsock() override;

	void socketpair(shared_ptr<unixsock> other);

	size_t read(void *dest, size_t count) override;
	void putstring(const char *str, size_t count) override;

	void sock_bind(cloudabi_sa_family_t family, shared_ptr<fd_t> fd, void *address, size_t address_len) override;
	void sock_connect(cloudabi_sa_family_t family, shared_ptr<fd_t> fd, void *address, size_t address_len) override;
	void sock_listen(cloudabi_backlog_t backlog) override;
	shared_ptr<fd_t> sock_accept(cloudabi_sa_family_t family, void *address, size_t *address_len) override;
	void sock_shutdown(cloudabi_sdflags_t how) override;
	void sock_stat_get(cloudabi_sockstat_t* buf, cloudabi_ssflags_t flags) override;
	void sock_recv(const cloudabi_recv_in_t* in, cloudabi_recv_out_t *out) override;
	void sock_send(const cloudabi_send_in_t* in, cloudabi_send_out_t *out) override;

private:
	weak_ptr<unixsock> othersock;

	/* if BOUND / LISTENING */
	cloudabi_device_t listen_device = 0;
	cloudabi_inode_t listen_inode = 0;
	cloudabi_backlog_t backlog = 0;

	void queue_connect(shared_ptr<unixsock> connectingsock);
	typedef linked_list<shared_ptr<unixsock>> unixsock_list;
	/** For unix sockets, connectat() returns a connected socket immediately,
	 * with accept() blocking. So, connectat() creates the connected socketpair
	 * and puts the 'accepting end' into the listenqueue. accept() wait until
	 * there is an accepting end in the list, and then immediately returns it
	 * once there is.
	 */
	unixsock_list *listenqueue = nullptr;
	cv_t listenqueue_cv;

	/* if CONNECTED or SHUTDOWN */
	static constexpr size_t MAX_SIZE_BUFFER = 1500;
	static constexpr size_t MAX_NUM_BUFFERS = 20;
	static constexpr size_t MAX_SIZE_BUFFERS = MAX_NUM_BUFFERS * MAX_SIZE_BUFFER;
	static constexpr size_t MAX_FD_PER_MESSAGE = 20;

	size_t num_recv_messages = 0;
	size_t num_recv_bytes = 0;
	unixsock_message_list *recv_messages = nullptr;
	cv_t recv_messages_cv;
};

}
