#include "cosix/reverse.hpp"

#include <argdata.h>
#include <cloudabi_syscalls.h>
#include <errno.h>
#include <program.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <string>
#include <atomic>

using namespace cosix;

char *cosix::handle_request(reverse_request_t *request, char *buf, reverse_response_t *response, reverse_handler *h) {
	using op = reverse_request_t::operation;

	response->flags = 0;
	response->send_length = 0;
	response->recv_length = 0;
	char *res = nullptr;

	try {
		switch(request->op) {
		case op::stat_fget: {
			response->send_length = sizeof(cloudabi_filestat_t);
			res = reinterpret_cast<char*>(malloc(response->send_length));
			auto statbuf = reinterpret_cast<cloudabi_filestat_t*>(res);
			h->stat_fget(request->pseudofd, statbuf);
			response->result = 0;
			break;
		}
		case op::lookup: {
			auto file_entry = h->lookup(request->pseudofd, buf, request->send_length, request->flags);
			response->result = file_entry.inode;
			response->flags = file_entry.type;
			break;
		}
		case op::open:
			response->result = h->open(request->inode, request->flags);
			break;
		case op::create:
			response->result = h->create(request->pseudofd, buf, request->send_length, request->flags);
			break;
		case op::close:
			h->close(request->pseudofd);
			response->result = 0;
			break;
		case op::is_readable:
			response->result = h->is_readable(request->pseudofd);
			break;
		case op::pread:
			res = reinterpret_cast<char*>(malloc(request->recv_length));
			response->send_length = h->pread(request->pseudofd, request->offset, res, request->recv_length);
			response->result = 0;
			break;
		case op::pwrite:
			h->pwrite(request->pseudofd, request->offset, buf, request->send_length);
			response->result = 0;
			break;
		case op::unlink:
			h->unlink(request->pseudofd, buf, request->send_length, request->flags);
			response->result = 0;
			break;
		case op::readdir: {
			cloudabi_dircookie_t cookie = request->flags;
			size_t const res_size = 512;
			res = reinterpret_cast<char*>(malloc(res_size));
			response->send_length = h->readdir(request->pseudofd, res, res_size, cookie);
			response->result = cookie;
			break;
		}
		case op::stat_get: {
			response->send_length = sizeof(cloudabi_filestat_t);
			res = reinterpret_cast<char*>(malloc(response->send_length));
			auto statbuf = reinterpret_cast<cloudabi_filestat_t*>(res);
			h->stat_get(request->pseudofd, request->flags, buf, request->send_length, statbuf);
			response->result = 0;
			break;
		}
		case op::sock_accept: {
			response->send_length = sizeof(cloudabi_sockstat_t);
			res = reinterpret_cast<char*>(malloc(response->send_length));
			auto ss = reinterpret_cast<cloudabi_sockstat_t*>(res);
			response->result = h->sock_accept(request->pseudofd, ss);
			break;
		}
		case op::sock_stat_get: {
			response->send_length = sizeof(cloudabi_sockstat_t);
			res = reinterpret_cast<char*>(malloc(response->send_length));
			auto ss = reinterpret_cast<cloudabi_sockstat_t*>(res);
			h->sock_stat_get(request->pseudofd, ss);
			response->result = 0;
			break;
		}
		case op::sock_recv: {
			// Implement in terms of read. This makes it impossible to do
			// FD passing, but otherwise it's the same.
			res = reinterpret_cast<char*>(malloc(request->recv_length));
			response->send_length = h->pread(request->pseudofd, 0, res, request->recv_length);
			response->result = 0;
			break;
		}
		case op::sock_send: {
			// Implement in terms of write.
			h->pwrite(request->pseudofd, 0, buf, request->send_length);
			// TODO: what if we wrote less?
			response->recv_length = request->send_length;
			response->result = 0;
			break;
		}
		case op::rename:
		case op::sock_listen:
		case op::sock_shutdown:
		case op::stat_put:
		default:
			response->result = -ENOSYS;
		}
	} catch(cloudabi_system_error &e) {
		response->result = -e.error;
		response->flags = 0;
		response->send_length = 0;
		response->recv_length = 0;
		if(res) {
			free(res);
			res = nullptr;
		}
	}
	return res;
}

cloudabi_errno_t cosix::wait_for_request(int reversefd, cloudabi_timestamp_t poll_timeout) {
	cloudabi_subscription_t subscriptions[2] = {
		{
			.type = CLOUDABI_EVENTTYPE_CLOCK,
			.clock.clock_id = CLOUDABI_CLOCK_MONOTONIC,
			.clock.flags = CLOUDABI_SUBSCRIPTION_CLOCK_ABSTIME,
			.clock.timeout = poll_timeout,
		},
		{
			.type = CLOUDABI_EVENTTYPE_FD_READ,
			.fd_readwrite.fd = static_cast<cloudabi_fd_t>(reversefd),
			.fd_readwrite.flags = CLOUDABI_SUBSCRIPTION_FD_READWRITE_POLL
		},
	};
	cloudabi_event_t events[2];
	size_t nevents = 2;
	cloudabi_errno_t error = cloudabi_sys_poll(subscriptions, events, nevents, &nevents);
	if(error != 0) {
		return error;
	}
	for(size_t i = 0; i < nevents; ++i) {
		if(events[i].error != 0) {
			return events[i].error;
		}
	}
	if(nevents == 0) {
		// ?
		return EINVAL;
	}
	if(nevents == 1 && events[0].type == CLOUDABI_EVENTTYPE_CLOCK) {
		// clock passed
		return EAGAIN;
	}

	return 0;
}

cloudabi_errno_t cosix::handle_request(int reversefd, reverse_handler *h, cloudabi_timestamp_t poll_timeout) {
	// if a timeout is given, wait until there is at least one byte to read
	if(poll_timeout != 0) {
		auto res = wait_for_request(reversefd, poll_timeout);
		if(res != 0) {
			return res;
		}
	}

	reverse_request_t request;
	reverse_response_t response;

	size_t received = 0;
	char *buf = reinterpret_cast<char*>(&request);
	while(received < sizeof(reverse_request_t)) {
		size_t remaining = sizeof(reverse_request_t) - received;
		ssize_t count = read(reversefd, buf + received, remaining);
		if(count <= 0) {
			if(errno == 0) {
				throw std::runtime_error("reversefd read() failed, but no errno present");
			}
			return errno;
		}
		received += count;
	}
	received = 0;
	buf = reinterpret_cast<char*>(malloc(request.send_length));
	while(received < request.send_length) {
		size_t remaining = request.send_length - received;
		ssize_t count = read(reversefd, buf + received, remaining);
		if(count <= 0) {
			if(errno == 0) {
				throw std::runtime_error("reversefd read() failed, but no errno present");
			}
			return errno;
		}
		received += count;
	}
	char *resbuf = handle_request(&request, buf, &response, h);
	free(buf);
	char *msg = reinterpret_cast<char*>(&response);
	write(reversefd, msg, sizeof(reverse_response_t));
	if(response.send_length > 0) {
		write(reversefd, resbuf, response.send_length);
	}
	if(resbuf) {
		free(resbuf);
	}
	return 0;
}

void cosix::handle_requests(int reversefd, reverse_handler *h) {
	while(true) {
		auto res = handle_request(reversefd, h);
		if(res != 0) {
			throw std::runtime_error("handle_request failed: " + std::string(strerror(res)));
		}
	}
}

void cosix::pseudo_fd_becomes_readable(int reversefd, pseudofd_t pseudo) {
	reverse_response_t response;
	response.gratituous = true;
	response.result = pseudo;
	response.flags = 1; /* pseudo became readable */
	char *msg = reinterpret_cast<char*>(&response);
	write(reversefd, msg, sizeof(response));
	// TODO: response.recv_length = bytes that are readable now
}

std::pair<int, int> cosix::open_pseudo(int ifstore, cloudabi_filetype_t type) {
	std::string message = "PSEUDOPAIR ";
	message += (type == CLOUDABI_FILETYPE_SOCKET_DGRAM ? "SOCKET_DGRAM" : "SOCKET_STREAM");
	write(ifstore, message.c_str(), message.size());
	char buf[20];
	buf[0] = 0;
	struct iovec iov = {.iov_base = buf, .iov_len = sizeof(buf)};
	alignas(struct cmsghdr) char control[CMSG_SPACE(2 * sizeof(int))];
	struct msghdr msg = {
		.msg_iov = &iov, .msg_iovlen = 1,
		.msg_control = control, .msg_controllen = sizeof(control),
	};
	if(recvmsg(ifstore, &msg, 0) < 0 || strncmp(buf, "OK", 2) != 0) {
		perror("Failed to retrieve pseudopair from ifstore");
		exit(1);
	}
	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
	if(cmsg == nullptr || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_len != CMSG_LEN(2 * sizeof(int))) {
		fprintf(stderr, "Pseudopair requested, but not given\n");
		exit(1);
	}
	int *fdbuf = reinterpret_cast<int*>(CMSG_DATA(cmsg));
	// (reverse, pseudo)
	return std::make_pair(fdbuf[0], fdbuf[1]);
}
