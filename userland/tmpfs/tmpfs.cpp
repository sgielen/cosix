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
#include "../../fd/reverse_proto.hpp"

int stdout;
int reversefd;

using reverse_proto::reverse_request_t;
using reverse_proto::reverse_response_t;

reverse_response_t *handle_request(reverse_request_t *request) {
	dprintf(stdout, "Got a request, failing it\n");
	reverse_response_t *response = new reverse_response_t;
	response->result = -EINVAL;
	response->flags = 0;
	response->length = 0;
	return response;
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
		}
	}

	dprintf(stdout, "tmpfs spawned -- awaiting requests on reverse FD %d\n", reversefd);
	while(1) {
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
}
