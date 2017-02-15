#include <stdio.h>
#include <stdlib.h>
#include <program.h>
#include <argdata.h>
#include <sched.h>
#include <pthread.h>
#include <atomic>
#include <unistd.h>
#include <errno.h>
#include "cosix/filesystem.hpp"

using namespace cosix;

cosix::file_entry::~file_entry() {}

void cosix::handle_request(reverse_request_t *request, reverse_response_t *response, filesystem *fs) {
	using op = reverse_request_t::operation;

	response->flags = 0;
	response->length = 0;

/* if request->length becomes uint16_t
	if(request->length > sizeof(request->buffer) && request->op != op::lookup) {
		// Protocol error
		response->result = -EINVAL;
		return response;
	}
*/

	try {
		switch(request->op) {
		case op::lookup: {
			auto file_entry = fs->lookup(request->pseudofd, reinterpret_cast<const char*>(request->buffer), request->length, request->flags);
			response->result = file_entry.inode;
			response->flags = file_entry.type;
			break;
		}
		case op::open:
			response->result = fs->open(request->inode, request->flags);
			break;
		case op::create:
			response->result = fs->create(request->pseudofd, reinterpret_cast<const char*>(request->buffer), request->length, request->flags);
			break;
		case op::close:
			fs->close(request->pseudofd);
			response->result = 0;
			break;
		case op::pread:
			/* if request->length becomes uint16_t
			if(request->length > sizeof(response->buffer)) {
				// Protocol error
				response->result = -EINVAL;
				return response;
			}
			*/
			response->length = fs->pread(request->pseudofd, request->offset, reinterpret_cast<char*>(response->buffer), request->length);
			response->result = 0;
			break;
		case op::pwrite:
			fs->pwrite(request->pseudofd, request->offset, reinterpret_cast<const char*>(request->buffer), request->length);
			break;
		case op::unlink:
			fs->unlink(request->pseudofd, reinterpret_cast<const char*>(request->buffer), request->length, request->flags);
			response->result = 0;
			break;
		case op::readdir: {
			cloudabi_dircookie_t cookie = request->flags;
			response->length = fs->readdir(request->pseudofd, reinterpret_cast<char*>(response->buffer), sizeof(response->buffer), cookie);
			response->result = cookie;
			break;
		}
		case op::stat_get: {
			response->length = sizeof(cloudabi_filestat_t);
			auto statbuf = reinterpret_cast<cloudabi_filestat_t*>(&response->buffer[0]);
			fs->stat_get(request->pseudofd, request->flags, reinterpret_cast<char*>(request->buffer), request->length, statbuf);
			break;
		}
		case op::stat_put:
		case op::rename:
		default:
			response->result = -ENOSYS;
		}
	} catch(filesystem_error &e) {
		response->result = -e.error;
		response->flags = 0;
		response->length = 0;
	}
}

void cosix::handle_requests(int reversefd, filesystem *fs) {
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
		handle_request(&request, &response, fs);
		char *msg = reinterpret_cast<char*>(&response);
		write(reversefd, msg, sizeof(reverse_response_t));
	}
}
