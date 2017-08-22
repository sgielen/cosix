#include <cosix/reverse.hpp>
#include <string>
#include <thread>

namespace telnetd {

struct telnet_terminal : public cosix::reverse_handler {
	static void run(int socket, int reversefd);

private:
	size_t pread(cosix::pseudofd_t pseudo, off_t offset, char *dest, size_t requested) override;
	void pwrite(cosix::pseudofd_t pseudo, off_t offset, const char *buf, size_t length) override;
	void handle_socketdata(const char *buf, size_t datalen);

	std::mutex mtx;
	int socket;
	std::condition_variable pseudo_cv;
	std::string sock_sendbuf;
	std::string pseudo_sendbuf;
};

}
