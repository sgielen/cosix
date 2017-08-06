#pragma once

#include <fd/unixsock.hpp>
#include <fd/reverse_proto.hpp>

namespace cloudos {

using reverse_proto::reverse_request_t;
using reverse_proto::reverse_response_t;
using reverse_proto::pseudofd_t;

struct pseudo_fd;

typedef linked_list<weak_ptr<pseudo_fd>> pseudo_list;

struct reversefd_t : public unixsock {
	reversefd_t(cloudabi_filetype_t sockettype, const char *n);
	~reversefd_t() override;

	void subscribe_fd_read_events(shared_ptr<pseudo_fd> fd);
	virtual void have_bytes_received() override;

	// send a request and block until we get a response
	Blk send_request(reverse_request_t *request, const char *buffer, reverse_response_t *response);

private:
	shared_ptr<pseudo_fd> get_pseudo(reverse_proto::pseudofd_t pseudo_id);
	void handle_gratituous_message();
	cloudabi_errno_t read_response(reverse_response_t *response, Blk *recv_buf);

	size_t bytes_read = 0;
	reverse_proto::reverse_response_t message;
	Blk recv_data;

	pseudo_list *pseudos = nullptr;

	bool sending_request = false;
	cv_t response_arrived_cv;
	cv_t request_done_cv;
};

}
