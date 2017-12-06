#pragma once
#include <fd/sock.hpp>
#include <fd/process_fd.hpp>
#include <oslibc/list.hpp>
#include <net/interface.hpp>
#include <concur/condition.hpp>

namespace cloudos {

struct rawsock : public sock_t, public enable_shared_from_this<rawsock> {
	rawsock(interface *iface, cloudabi_fdflags_t f, const char *n);
	~rawsock() override;

	void init();

	bool has_messages() const;
	cloudabi_errno_t get_read_signaler(thread_condition_signaler **s) override;

	void sock_shutdown(cloudabi_sdflags_t how) override;
	void sock_recv(const cloudabi_recv_in_t* in, cloudabi_recv_out_t *out) override;
	void sock_send(const cloudabi_send_in_t* in, cloudabi_send_out_t *out) override;

	void frame_received(uint8_t *frame, size_t frame_length);

private:
	// TODO: make this a weak ptr
	interface *iface;

	thread_condition_signaler read_signaler;

	linked_list<Blk> *messages = nullptr;
	cv_t read_cv;
};

}
