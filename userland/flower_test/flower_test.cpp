#include <argdata.h>
#include <atomic>
#include <errno.h>
#include <program.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <unistd.h>

#include <arpc++/arpc++.h>
#include <flower/protocol/server.ad.h>
#include <flower/protocol/switchboard.ad.h>

#include "configuration.ad.h"

using namespace arpc;
using namespace flower::protocol::server;
using namespace flower::protocol::switchboard;

int stdout;
uint32_t instance;
std::shared_ptr<FileDescriptor> switchboard;
std::mutex mtx;
std::condition_variable cv;

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

void *thread_handler(void *sbstub) {
	dprintf(stdout, "Serving thread started\n");
	std::unique_lock<std::mutex> lock(mtx);

	auto *stub = reinterpret_cast<Switchboard::Stub*>(sbstub);

	ClientContext context;
	ServerStartRequest request;
	auto in_labels = request.mutable_in_labels();
	(*in_labels)["scope"] = "test";
	(*in_labels)["service"] = "flower_test";
	(*in_labels)["instance"] = std::to_string(instance);
	ServerStartResponse response;

	if(Status status = stub->ServerStart(&context, request, &response); !status.ok()) {
		dprintf(stdout, "Failed to start server in switchboard: %s\n", status.error_message().c_str());
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

	cv.notify_one();
	mtx.unlock();

	dprintf(stdout, "Waiting for an incoming connection\n");
	if(server->HandleRequest() != 0) {
		dprintf(stdout, "HandleRequest failed\n");
		exit(1);
	}
	auto connect_request = connection_extractor.GetIncomingConnection();
	if(!connect_request) {
		dprintf(stdout, "No incoming connection\n");
		exit(1);
	}

	auto connection = connect_request->client();
	if(!connection) {
		dprintf(stdout, "flower_test: switchboard did not return a connection\n");
		exit(1);
	}

	for(size_t j = 0; j < 5; ++j) {
		// wait a bit
		struct timespec ts = {.tv_sec = 0, .tv_nsec = 200 * 1000 * 1000 /* 200 ms */};
		clock_nanosleep(CLOCK_MONOTONIC, 0, &ts);

		// send something
		char buf[20];
		snprintf(buf, sizeof(buf), "This is message #%d", j + 1);
		if(write(connection->get(), buf, strlen(buf)) < 0) {
			dprintf(stdout, "tx failed: %s\n", strerror(errno));
		} else {
			dprintf(stdout, "tx: %s\n", buf);
		}
	}
	return nullptr;
}

void program_main(const argdata_t *ad) {
	cosix::flower_test::Configuration configuration;
	{
		ArgdataParser parser;
		configuration.Parse(*ad, &parser);
	}

	if(!configuration.logger_output() || !configuration.switchboard_socket()) {
		exit(1);
	}

	stdout = configuration.logger_output()->get();
	switchboard = configuration.switchboard_socket();

	dprintf(stdout, "This is flower_test\n");

	std::unique_lock<std::mutex> lock(mtx);

	instance = arc4random();
	auto channel = CreateChannel(switchboard);
	auto stub = Switchboard::NewStub(channel);

	pthread_t thread;
	pthread_create(&thread, nullptr, thread_handler, stub.get());

	// Wait until server is created
	cv.wait(lock);

	// Connect to the thread via the switchboard
	ClientContext context;
	ClientConnectRequest request;
	auto out_labels = request.mutable_out_labels();
	(*out_labels)["scope"] = "test";
	(*out_labels)["service"] = "flower_test";
	(*out_labels)["instance"] = std::to_string(instance);
	ClientConnectResponse response;
	if (Status status = stub->ClientConnect(&context, request, &response); !status.ok()) {
		dprintf(stdout, "flower_test failed to connect\n");
		exit(1);
	}

	auto connection = response.server();
	if(!connection) {
		dprintf(stdout, "flower_test: switchboard did not return a connection\n");
		exit(1);
	}

	char buf[128];
	for(size_t i = 0; i < 5; ++i) {
		ssize_t count = read(connection->get(), buf, sizeof(buf));
		if(count <= 0) {
			dprintf(stdout, "rx failed: %s\n", strerror(errno));
		} else {
			buf[count] = 0;
			dprintf(stdout, "rx: %s\n", buf);
		}
	}

	pthread_join(thread, nullptr);
	pthread_exit(NULL);
}
