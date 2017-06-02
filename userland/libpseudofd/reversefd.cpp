#include <stdio.h>
#include <stdlib.h>
#include <program.h>
#include <argdata.h>
#include <sched.h>
#include <pthread.h>
#include <atomic>
#include <unistd.h>
#include <errno.h>
#include "cosix/reverse.hpp"

using namespace cosix;

char *cosix::handle_request(reverse_request_t *request, char *buf, reverse_response_t *response, reverse_handler *h) {
	using op = reverse_request_t::operation;

	response->flags = 0;
	response->send_length = 0;
	response->recv_length = 0;
	char *res = nullptr;

	try {
		switch(request->op) {
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

void cosix::handle_requests(int reversefd, reverse_handler *h) {
	reverse_request_t request;
	reverse_response_t response;
	while(true) {
		size_t received = 0;
		char *buf = reinterpret_cast<char*>(&request);
		while(received < sizeof(reverse_request_t)) {
			size_t remaining = sizeof(reverse_request_t) - received;
			ssize_t count = read(reversefd, buf + received, remaining);
			if(count <= 0) {
				throw std::runtime_error("reversefd read() failed");
			}
			received += count;
		}
		received = 0;
		buf = reinterpret_cast<char*>(malloc(request.send_length));
		while(received < request.send_length) {
			size_t remaining = request.send_length - received;
			ssize_t count = read(reversefd, buf + received, remaining);
			if(count <= 0) {
				throw std::runtime_error("reversefd read() failed");
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
	}
}
