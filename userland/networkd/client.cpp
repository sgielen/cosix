#include "client.hpp"
#include <argdata.h>
#include <unistd.h>

using namespace networkd;

client::client(int l, int f)
: logfd(l)
, fd(f)
{
}

client::~client()
{
	close(fd);
}

void client::run() {
	while(1) {
		char buf[200];
		// TODO: set non-blocking flag once kernel supports it
		// this way, we can read until EOF instead of only 200 bytes
		ssize_t size = read(fd, buf, sizeof(buf));
		if(size < 0) {
			perror("read");
			return;
		}
		if(size == 0) {
			continue;
		}
		argdata_t *message = argdata_from_buffer(buf, size);
		FILE *out = fdopen(logfd, "w");
		argdata_print_yaml(message, out);
		fflush(out);
	}
}
