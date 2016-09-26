#include <stdio.h>
#include <stdlib.h>
#include <program.h>
#include <argdata.h>
#include <sched.h>
#include <pthread.h>
#include <atomic>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <cassert>

#include "../../fd/reverse_proto.hpp"
#include "tmpfs.hpp"

int device = -1;
bool running = false;
int stdout = -1;
int reversefd = -1;
tmpfs *fs = 0;

using reverse_proto::reverse_request_t;
using reverse_proto::reverse_response_t;

reverse_response_t *handle_request(reverse_request_t *request) {
	using op = reverse_request_t::operation;

	reverse_response_t *response = new reverse_response_t;
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
		case op::lookup:
			response->result = fs->lookup(request->pseudofd, reinterpret_cast<const char*>(request->buffer), request->length, request->flags);
			break;
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
		case op::stat_get:
		case op::stat_put:
		case op::readdir:
		case op::rename:
		default:
			dprintf(stdout, "tmpfs: Got an unimplemented request operation, failing it\n");
			response->result = -ENOSYS;
		}
		return response;
	} catch(filesystem_error &e) {
		dprintf(stdout, "<tmpfs> request failed, error %d\n", e.error);
		response->result = -e.error;
		response->flags = 0;
		response->length = 0;
		return response;
	}

	/* unreachable */
	dprintf(stdout, "Unreachable reached\n");
	assert(!"Unreachable");
	abort();
}

void program_main(const argdata_t *ad) {
	argdata_map_iterator_t it;
	const argdata_t *key;
	const argdata_t *value;
	argdata_map_iterate(ad, &it);
	while (argdata_map_next(&it, &key, &value)) {
		const char *keystr;
		if(argdata_get_str_c(key, &keystr) != 0) {
			continue;
		}

		if(strcmp(keystr, "stdout") == 0) {
			argdata_get_fd(value, &stdout);
		} else if(strcmp(keystr, "reversefd") == 0) {
			argdata_get_fd(value, &reversefd);
		} else if(strcmp(keystr, "deviceid") == 0) {
			argdata_get_int(value, &device);
		}
	}

	running = true;
	fs = new tmpfs(device);

	dprintf(stdout, "tmpfs spawned -- awaiting requests on reverse FD %d\n", reversefd);
	while(running) {
		size_t received = 0;
		char buf[sizeof(reverse_request_t)];
		while(received < sizeof(reverse_request_t)) {
			size_t remaining = sizeof(reverse_request_t) - received;
			ssize_t count = read(reversefd, buf + received, remaining);
			if(count <= 0) {
				dprintf(stdout, "tmpfs read() failed: %s\n", strerror(errno));
				abort();
			}
			received += count;
		}
		reverse_request_t *request = reinterpret_cast<reverse_request_t*>(&buf[0]);
		reverse_response_t *response = handle_request(request);
		uint8_t *msg = reinterpret_cast<uint8_t*>(response);
		write(reversefd, msg, sizeof(reverse_response_t));
		delete response;
	}

	delete fs;
	close(reversefd);

	dprintf(stdout, "tmpfs closing.\n");
	close(stdout);
	exit(0);
}
