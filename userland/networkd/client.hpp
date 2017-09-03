#pragma once
#include <string>
#include <argdata.h>

namespace arpc {
struct FileDescriptor;
}

namespace networkd {

struct client {
	client(int logfd, std::shared_ptr<arpc::FileDescriptor> clientfd);

	void run();

private:
	bool send_response(argdata_t *t);
	bool send_error(std::string error);

	int logfd;
	std::shared_ptr<arpc::FileDescriptor> fd;
};

}
