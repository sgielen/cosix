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
#include <sys/procdesc.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <cosix/networkd.hpp>
#include <flower/protocol/server.ad.h>
#include <flower/protocol/switchboard.ad.h>

int stdout;
int tmpdir;
int networkd;
int port;
int switchboard;

using namespace arpc;
using namespace flower::protocol::server;
using namespace flower::protocol::switchboard;

class WebServer : public flower::protocol::server::Server::Service {
public:
	virtual ~WebServer() {}

	Status Connect(ServerContext*, const ConnectRequest *request, ConnectResponse*) override {
		auto client = request->client();
		std::thread thr([client]() {
			char buf[512];
			buf[0] = 0;
			strlcat(buf, "HTTP/1.1 200 OK\r\n", sizeof(buf));
			strlcat(buf, "Server: cosix/0.0\r\n", sizeof(buf));
			strlcat(buf, "Content-Type: text/html; charset=UTF-8\r\n", sizeof(buf));
			strlcat(buf, "Transfer-Encoding: chunked\r\n", sizeof(buf));
			strlcat(buf, "Connection: close\r\n\r\n", sizeof(buf));
			strlcat(buf, "40\r\n", sizeof(buf));
			strlcat(buf, "<!DOCTYPE html><html><body><h1>Hello world!</h1></body></html>\r\n\r\n0\r\n\r\n", sizeof(buf));

			write(client->get(), buf, strlen(buf));

			// Wait for the client to close the connection.
			shutdown(client->get(), SHUT_WR);
			char discard[4096];
			while (read(client->get(), discard, sizeof(discard)) > 0) {}
		});
		thr.detach();
		return Status::OK;
	}
};

void program_main(const argdata_t *ad) {
	stdout = -1;
	tmpdir = -1;
	networkd = -1;
	port = 80;
	switchboard = -1;

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
		} else if(strcmp(keystr, "port") == 0) {
			argdata_get_int(value, &port);
		} else if(strcmp(keystr, "switchboard_socket") == 0) {
			argdata_get_fd(value, &switchboard);
		}
		argdata_map_next(&it);
	}

	FILE *out = fdopen(stdout, "w");
	setvbuf(out, nullptr, _IONBF, BUFSIZ);
	fswap(stderr, out);

	auto switchboard_stub = Switchboard::NewStub(CreateChannel(std::make_shared<FileDescriptor>(dup(switchboard))));

	ClientContext context;
	ServerStartRequest request;
	auto in_labels = request.mutable_in_labels();
	(*in_labels)["scope"] = "network";
	(*in_labels)["protocol"] = "tcp";
	(*in_labels)["local_port"] = std::to_string(port);

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
	WebServer webserver;
	builder.RegisterService(&webserver);
	auto server = builder.Build();

	while(1) {
		int res = server->HandleRequest();
		if(res != 0) {
			dprintf(stdout, "httpd: HandleRequest failed: %s\n", strerror(res));
			exit(1);
		}
	}
}
