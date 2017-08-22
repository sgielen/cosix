#include "tterm.hpp"
#include <unistd.h>
#include <sys/select.h>

using namespace telnetd;

size_t telnet_terminal::pread(cosix::pseudofd_t, off_t, char *dest, size_t requested)
{
	// application wants to read keystrokes from telnet user
	std::unique_lock<std::mutex> lock(mtx);
	while(pseudo_sendbuf.empty()) {
		pseudo_cv.wait(lock);
	}
	size_t len = std::min(pseudo_sendbuf.size(), requested);
	memcpy(dest, pseudo_sendbuf.c_str(), len);
	pseudo_sendbuf = pseudo_sendbuf.substr(len);
	return len;
}

void telnet_terminal::pwrite(cosix::pseudofd_t, off_t, const char *buf, size_t length)
{
	// application wants to send data to telnet user
	std::unique_lock<std::mutex> lock(mtx);
	write(socket, buf, length);
}

void telnet_terminal::run(int socket, int reversefd)
{
	telnet_terminal tt;
	tt.socket = socket;

	bool running = true;
	std::thread revthread([&]() {
		while(running) {
			auto err = cosix::handle_request(reversefd, &tt);
			if(err < 0) {
				fprintf(stderr, "[TELNETD] Failed handle_request: %s\n", strerror(err));
				running = false;
				break;
			}
		}
	});

	// wait for something to read off the socket
	char buf[64];
	while(running) {
		// wait for incoming data without having a reverse FD request outstanding
		fd_set rdset;
		FD_ZERO(&rdset);
		FD_SET(socket, &rdset);
		select(socket + 1, &rdset, nullptr, nullptr, nullptr);

		ssize_t datalen = read(socket, buf, sizeof(buf));
		if(datalen < 0) {
			fprintf(stderr, "[TELNETD] Failed socket read(): %s\n", strerror(errno));
			running = false;
			break;
		}

		// the user wants to send data to the application
		std::unique_lock<std::mutex> lock(tt.mtx);
		// TODO: interpret telnet options from data
		tt.pseudo_sendbuf.append(buf, datalen);
		tt.pseudo_cv.notify_one();
	}

	revthread.join();
}
