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
		case op::stat_fput: {
			if(request->send_length != sizeof(cloudabi_filestat_t)) {
				throw cloudabi_system_error(EIO);
			}
			h->stat_fput(request->pseudofd, reinterpret_cast<const cloudabi_filestat_t*>(buf), request->inode);
			response->result = 0;
			break;
		}
		case op::stat_put: {
			if(request->send_length < sizeof(cloudabi_filestat_t)) {
				throw cloudabi_system_error(EIO);
			}
			auto *path = reinterpret_cast<const char*>(buf + sizeof(cloudabi_filestat_t));
			size_t pathlen = request->send_length - sizeof(cloudabi_filestat_t);
			h->stat_put(request->pseudofd, request->flags, path, pathlen, reinterpret_cast<const cloudabi_filestat_t*>(buf), request->inode);
			response->result = 0;
			break;
		}
		case op::sock_recv: {
			res = reinterpret_cast<char*>(malloc(request->recv_length));
			response->send_length = h->sock_recv(request->pseudofd, res, request->recv_length);
			response->result = 0;
			break;
		}
		case op::sock_send: {
			h->sock_send(request->pseudofd, buf, request->send_length);
			// TODO: what if we wrote less?
			response->recv_length = request->send_length;
			response->result = 0;
			break;
		}
		case op::rename: {
			pseudofd_t pseudo2 = request->flags;
			char *path1 = buf;
			size_t path1len = 0;
			while(path1len < request->send_length && path1[path1len] != 0) {
				path1len++;
			}
			if(path1len == request->send_length) {
				// no delimiter found, fail
				throw cloudabi_system_error(EIO);
			}
			char *path2 = path1 + path1len + 1;
			size_t path2len = request->send_length - path1len - 1;
			h->rename(request->pseudofd, path1, path1len, pseudo2, path2, path2len);
			response->result = 0;
			break;
		}
		case op::readlink:
			res = reinterpret_cast<char*>(malloc(request->recv_length));
			response->send_length = h->readlink(request->pseudofd, buf, request->send_length, res, request->recv_length);
			response->result = 0;
			break;
		case op::symlink: {
			char *path1 = buf;
			size_t path1len = 0;
			while(path1len < request->send_length && path1[path1len] != 0) {
				path1len++;
			}
			if(path1len == request->send_length) {
				// no delimiter found, fail
				throw cloudabi_system_error(EIO);
			}
			char *path2 = path1 + path1len + 1;
			size_t path2len = request->send_length - path1len - 1;
			h->symlink(request->pseudofd, path1, path1len, path2, path2len);
			response->result = 0;
			break;
		}
		case op::allocate:
			h->allocate(request->pseudofd, request->offset, request->flags);
			break;
		case op::link: {
			pseudofd_t pseudo2 = request->flags;
			char *path1 = buf;
			size_t path1len = 0;
			while(path1len < request->send_length && path1[path1len] != 0) {
				path1len++;
			}
			if(path1len == request->send_length) {
				// no delimiter found, fail
				throw cloudabi_system_error(EIO);
			}
			char *path2 = path1 + path1len + 1;
			size_t path2len = request->send_length - path1len - 1;
			h->link(request->pseudofd, path1, path1len, request->offset, pseudo2, path2, path2len);
			response->result = 0;
			break;
		}
		case op::datasync:
			h->datasync(request->pseudofd);
			response->result = 0;
			break;
		case op::sync:
			h->sync(request->pseudofd);
			response->result = 0;
			break;
		case op::sock_shutdown:
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

char *cosix::read_request(int reversefd, reverse_request_t *request) {
	size_t received = 0;
	char *buf = reinterpret_cast<char*>(request);
	while(received < sizeof(reverse_request_t)) {
		size_t remaining = sizeof(reverse_request_t) - received;
		ssize_t count = read(reversefd, buf + received, remaining);
		if(count <= 0) {
			throw cloudabi_system_error(errno);
		}
		received += count;
	}
	if(request->send_length == 0) {
		return nullptr;
	}
	received = 0;
	buf = reinterpret_cast<char*>(malloc(request->send_length));
	while(received < request->send_length) {
		size_t remaining = request->send_length - received;
		ssize_t count = read(reversefd, buf + received, remaining);
		if(count <= 0) {
			throw cloudabi_system_error(errno);
		}
		received += count;
	}
	return buf;
}

void cosix::write_response(int reversefd, reverse_response_t *response, char *buf) {
	char *msg = reinterpret_cast<char*>(response);
	if(write(reversefd, msg, sizeof(reverse_response_t)) <= 0) {
		throw cloudabi_system_error(errno);
	}
	if(response->send_length > 0) {
		if(write(reversefd, buf, response->send_length) <= 0) {
			throw cloudabi_system_error(errno);
		}
	}
}

cloudabi_errno_t cosix::handle_request(int reversefd, reverse_handler *h, std::mutex &mtx, cloudabi_timestamp_t poll_timeout) {
	// if a timeout is given, wait until there is at least one byte to read
	if(poll_timeout != 0) {
		auto res = wait_for_request(reversefd, poll_timeout);
		if(res != 0) {
			return res;
		}
	}

	reverse_request_t request;
	reverse_response_t response;

	char *buf = nullptr, *resbuf = nullptr;
	cloudabi_errno_t res = 0;
	try {
		{
			std::lock_guard<std::mutex> lock(mtx);
			buf = read_request(reversefd, &request);
		}
		resbuf = handle_request(&request, buf, &response, h);
		{
			std::lock_guard<std::mutex> lock(mtx);
			write_response(reversefd, &response, resbuf);
		}
	} catch(cloudabi_system_error &e) {
		res = e.error;
	}

	free(buf);
	free(resbuf);
	return res;
}

cloudabi_errno_t cosix::handle_request(int reversefd, reverse_handler *h, cloudabi_timestamp_t poll_timeout) {
	// always-unlocked mtx
	std::mutex mtx;
	return handle_request(reversefd, h, mtx, poll_timeout);
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
	std::string message = "PSEUDOPAIR " + std::to_string(int(type));
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
