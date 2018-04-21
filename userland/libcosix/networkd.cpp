#include <cosix/networkd.hpp>

#include <mstd/range.hpp>
#include <argdata.hpp>
#include <memory>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>

#include <flower/protocol/switchboard.ad.h>

using namespace arpc;
using namespace flower::protocol::switchboard;

static std::string strerrno() {
	return strerror(errno);
}

int cosix::networkd::open(int switchboard) {
	return open(std::make_shared<FileDescriptor>(dup(switchboard)));
}

int cosix::networkd::open(std::shared_ptr<FileDescriptor> switchboard) {
	auto stub = Switchboard::NewStub(CreateChannel(switchboard));
	return open(stub.get());
}

int cosix::networkd::open(Switchboard::Stub *switchboard) {
	ClientContext context;
	ClientConnectRequest request;
	auto out_labels = request.mutable_out_labels();
	(*out_labels)["scope"] = "root";
	(*out_labels)["service"] = "networkd";
	ClientConnectResponse response;
	if (Status status = switchboard->ClientConnect(&context, request, &response); !status.ok()) {
		throw std::runtime_error("Failed to connect to networkd");
	}

	auto connection = response.server();
	if(!connection) {
		throw std::runtime_error("Switchboard did not return a connection");
	}

	return dup(connection->get());
}

int cosix::networkd::get_socket(int networkd, int type, std::string connect, std::string bind) {
	std::string command;
	if(type == SOCK_DGRAM) {
		command = "udpsock";
	} else if(type == SOCK_STREAM) {
		command = "tcpsock";
	} else {
		throw std::runtime_error("Unknown type of socket to get");
	}

	std::unique_ptr<argdata_t> keys[] =
		{argdata_t::create_str("command"), argdata_t::create_str("connect"), argdata_t::create_str("bind")};
	std::unique_ptr<argdata_t> values[] =
		{argdata_t::create_str(command.c_str()), argdata_t::create_str(connect.c_str()), argdata_t::create_str(bind.c_str())};
	std::vector<argdata_t*> key_ptrs;
	std::vector<argdata_t*> value_ptrs;
	
	for(auto &key : mstd::range<std::unique_ptr<argdata_t>>(keys)) {
		key_ptrs.push_back(key.get());
	}
	for(auto &value : mstd::range<std::unique_ptr<argdata_t>>(values)) {
		value_ptrs.push_back(value.get());
	}
	auto map = argdata_t::create_map(key_ptrs, value_ptrs);

	std::vector<unsigned char> rbuf;
	map->serialize(rbuf);

	write(networkd, rbuf.data(), rbuf.size());
	// TODO: set non-blocking flag once kernel supports it
	// this way, we can read until EOF instead of only 200 bytes
	// TODO: for a generic implementation, MSG_PEEK to find the number
	// of file descriptors
	uint8_t buf[1500];
	memset(buf, 0, sizeof(buf));
	struct iovec iov = {.iov_base = buf, .iov_len = sizeof(buf)};
	alignas(struct cmsghdr) char control[CMSG_SPACE(sizeof(int))];
	memset(control, 0, sizeof(control));
	struct msghdr msg = {
		.msg_iov = &iov, .msg_iovlen = 1,
		.msg_control = control, .msg_controllen = sizeof(control),
	};
	ssize_t size = recvmsg(networkd, &msg, 0);
	if(size < 0) {
		throw std::runtime_error("Failed to read networkd socket: " + strerrno());
	}
	auto response = argdata_t::create_from_buffer(mstd::range<unsigned char const>(&buf[0], size));
	int fdnum = -1;
	for(auto i : response->as_map()) {
		auto key = i.first->as_str();
		if(key == "error") {
			throw std::runtime_error("Failed to retrieve TCP socket from networkd: " + std::string(i.second->as_str()));
		} else if(key == "fd") {
			fdnum = *i.second->get_fd();
		}
	}
	// TODO: a bug somewhere (in the argdata library?) sometimes causes
	// this fd not to be set, but we can assume it is 0 and retrieve it from the CMSG_DATA
	//if(fdnum != 0) {
	//	throw std::runtime_error("Ifstore TCP socket not received");
	//}
	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
	if(cmsg == nullptr || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_len != CMSG_LEN(sizeof(int))) {
		throw std::runtime_error("Ifstore socket requested, but not given\n");
	}
	int *fdbuf = reinterpret_cast<int*>(CMSG_DATA(cmsg));
	return fdbuf[0];
}
