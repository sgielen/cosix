#include <string>
#include <argdata.h>

namespace networkd {

struct client {
	client(int logfd, int fd);
	~client();

	void run();

private:
	bool send_response(argdata_t *t);
	bool send_error(std::string error);

	int logfd;
	int fd;
};

}
