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
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <argdata.hpp>
#include <cloudabi_syscalls.h>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <cosix/networkd.hpp>
#include <flower/protocol/server.ad.h>
#include <flower/protocol/switchboard.ad.h>

using namespace arpc;
using namespace flower::protocol::server;
using namespace flower::protocol::switchboard;

int stdout = -1;
int tmpdir = -1;
int networkd = -1;
int switchboard = -1;

class ConnectionExtractor : public flower::protocol::server::Server::Service {
public:
	virtual ~ConnectionExtractor() {}

	Status Connect(ServerContext*, const ConnectRequest *request, ConnectResponse*) override {
		incoming_connection_ = *request;
		return Status::OK;
	}

	std::optional<ConnectRequest> GetIncomingConnection() {
		std::optional<ConnectRequest> result;
		result.swap(incoming_connection_);
		return result;
	}

private:
	std::optional<ConnectRequest> incoming_connection_;
};

void program_main(const argdata_t *ad) {
	argdata_map_iterator_t it;
	const argdata_t *key;
	const argdata_t *value;
	argdata_map_iterate(ad, &it);
	while (argdata_map_get(&it, &key, &value)) {
		const char *keystr;
		if(argdata_get_str_c(key, &keystr) != 0) {
			argdata_map_next(&it);
			continue;
		}

		if(strcmp(keystr, "stdout") == 0) {
			argdata_get_fd(value, &stdout);
		} else if(strcmp(keystr, "networkd") == 0) {
			argdata_get_fd(value, &networkd);
		} else if(strcmp(keystr, "tmpdir") == 0) {
			argdata_get_fd(value, &tmpdir);
		} else if(strcmp(keystr, "switchboard_socket") == 0) {
			argdata_get_fd(value, &switchboard);
		}
		argdata_map_next(&it);
	}

	FILE *out = fdopen(stdout, "w");
	setvbuf(out, nullptr, _IONBF, BUFSIZ);
	fswap(stderr, out);

	auto channel = CreateChannel(std::make_shared<FileDescriptor>(dup(switchboard)));
	auto switchboard_stub = Switchboard::NewStub(channel);

	// First test, to see IP stack sending behaviour in PCAP dumps:
	try {
		int sock0 = cosix::networkd::get_socket(networkd, SOCK_STREAM, "138.201.24.102:8765", "");
		write(sock0, "Hello World!\n", 13);
	} catch(std::runtime_error &e) {
		dprintf(stdout, "Failed to run TCP test: %s\n", e.what());
	}

	bool running = true;
	std::thread serverthread([&]() {
		ClientContext context;
		ServerStartRequest request;
		auto in_labels = request.mutable_in_labels();
		(*in_labels)["scope"] = "network";
		(*in_labels)["protocol"] = "tcp";
		(*in_labels)["local_ip"] = "127.0.0.1";
		(*in_labels)["local_port"] = "1234";

		ServerStartResponse response;
		if(Status status = switchboard_stub->ServerStart(&context, request, &response); !status.ok()) {
			dprintf(stdout, "Failed to register tcptest in switchboard: %s\n", status.error_message().c_str());
			exit(1);
		}

		if(!response.server()) {
			dprintf(stdout, "Switchboard did not return a server\n");
			exit(1);
		}

		ServerBuilder builder(response.server());
		ConnectionExtractor connection_extractor;
		builder.RegisterService(&connection_extractor);
		auto server = builder.Build();

		if(server->HandleRequest() != 0) {
			dprintf(stdout, "tcptest: HandleRequest failed\n");
			exit(1);
		}
		auto connect_request = connection_extractor.GetIncomingConnection();
		if(!connect_request) {
			dprintf(stdout, "tcptest: No incoming connection\n");
			exit(1);
		}

		auto connection = connect_request->client();
		if(!connection) {
			dprintf(stdout, "tcptest: switchboard did not return a connection\n");
			exit(1);
		}

		char buf[16];
		while(running) {
			ssize_t res = read(connection->get(), buf, sizeof(buf));
			if(res < 0) {
				dprintf(stdout, "Failed to receive data over TCP (%s)\n", strerror(errno));
				exit(1);
			}
			for(ssize_t i = 0; i < res; ++i) {
				if(buf[i] >= 'A' && buf[i] <= 'Z') {
					buf[i] = 'A' + (((buf[i] - 'A') + 13) % 26);
				} else if(buf[i] >= 'a' && buf[i] <= 'z') {
					buf[i] = 'a' + (((buf[i] - 'a') + 13) % 26);
				}
			}
			if(write(connection->get(), buf, res) != res) {
				dprintf(stdout, "Failed to write data over TCP (%s)\n", strerror(errno));
				exit(1);
			}
		}
	});

	// Wait a second for listening thread to come up
	struct timespec ts = {.tv_sec = 1, .tv_nsec = 0};
	clock_nanosleep(CLOCK_MONOTONIC, 0, &ts);

	// Socket: connected to 127.0.0.1:1234, not bound
	int connected = cosix::networkd::get_socket(networkd, SOCK_STREAM, "127.0.0.1:1234", "");
	if(write(connected, "Foo bar!", 8) != 8) {
		dprintf(stdout, "Failed to write data over TCP (%s)\n", strerror(errno));
		exit(1);
	}
	char buf[32];
	if(read(connected, buf, sizeof(buf)) != 8) {
		dprintf(stdout, "Failed to receive data over TCP (%s)\n", strerror(errno));
		exit(1);
	}
	if(memcmp(buf, "Sbb one!", 8) != 0) {
		buf[8] = 0;
		dprintf(stdout, "ROT13 data received is incorrect: \"%s\"\n", buf);
		exit(1);
	}

	if(write(connected, "Mumblebumble", 12) != 12) {
		dprintf(stdout, "Failed to write data over TCP (%s)\n", strerror(errno));
		exit(1);
	}
	if(write(connected, "Blamblam", 8) != 8) {
		dprintf(stdout, "Failed to write additional data over TCP (%s)\n", strerror(errno));
		exit(1);
	}
	// wait for a second for the response to arrive
	clock_nanosleep(CLOCK_MONOTONIC, 0, &ts);
	if(read(connected, buf, sizeof(buf)) != 20) {
		dprintf(stdout, "Failed to receive all data over TCP (%s)\n", strerror(errno));
		exit(1);
	}
	if(memcmp(buf, "ZhzoyrohzoyrOynzoynz", 20) != 0) {
		buf[20] = 0;
		dprintf(stdout, "ROT13 data received is incorrect: \"%s\"\n", buf);
		exit(1);
	}
	
	// TODO: send packets from invalid TCP stacks, see if the kernel copes
	// TODO: send packets over very bad connections (VDE switch?), see if communication
	// still works well

	running = false;
	// bump the server thread if it's blocked on read()
	write(connected, "bump", 4);
	serverthread.join();

	dprintf(stdout, "All TCP traffic seems correct!\n");
	exit(0);
}
