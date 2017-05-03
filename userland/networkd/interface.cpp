#include "interface.hpp"
#include "networkd.hpp"
#include "arp.hpp"
#include <unistd.h>
#include <cassert>
#include <cloudabi_types.h>
#include <cloudabi_syscalls.h>
#include <arpa/inet.h>

using namespace networkd;

interface::interface(std::string n, std::string m, std::string h, int r)
: name(n)
, mac(m)
, hwtype(h)
, rawsock(r)
{}

interface::~interface()
{
	if(rawsock >= 0) {
		int r = rawsock;
		rawsock = -1;
		close(r);
	}
	if(thr.joinable()) {
		thr.join();
	}
}

void interface::start()
{
	assert(rawsock >= 0);
	thr = std::thread([this](){
		run();
	});
}

void interface::add_ipv4addr(const char *ip, uint8_t cidr_prefix)
{
	interface_ipv4addr a;
	memcpy(a.ip, ip, sizeof(a.ip));
	a.cidr_prefix = cidr_prefix;
	ipv4addrs.emplace_back(std::move(a));
}

void interface::send_frame(std::vector<iovec> const &iov) {
	cloudabi_send_in_t in;
	in.si_data = const_cast<cloudabi_ciovec_t*>(
		reinterpret_cast<const cloudabi_ciovec_t*>(iov.data()));
	in.si_data_len = iov.size();
	in.si_fds = 0;
	in.si_fds_len = 0;
	in.si_flags = 0;
	cloudabi_send_out_t out;
	cloudabi_errno_t error =
		cloudabi_sys_sock_send(rawsock, &in, &out);
	if(error != 0) {
		// TODO: close the interface?
		throw std::runtime_error("Failed to write frame");
	}
}

void interface::run() {
	while(rawsock >= 0) {
		// TODO: get MTU for interface instead of hardcoding
		const int mtu = 1500;
		char frame[mtu];

		cloudabi_iovec_t ri_vec;
		ri_vec.buf = frame;
		ri_vec.buf_len = sizeof(frame);

		cloudabi_recv_in_t in;
		in.ri_data = &ri_vec;
		in.ri_data_len = 1;
		in.ri_fds = 0;
		in.ri_fds_len = 0;
		in.ri_flags = 0;

		cloudabi_recv_out_t out;
	
		cloudabi_errno_t error =
			cloudabi_sys_sock_recv(rawsock, &in, &out);
		if(error != 0) {
			int r = rawsock;
			rawsock = -1;
			close(r);
			return;
		}

		size_t datalen = out.ro_datalen;

		// TODO: instead of hardcoding 'loopback'/'ethernet' here, make
		// hwtype-specific interface implementations for receive_frame()
		if(hwtype == "LOOPBACK") {
			// immediately forward to IP stack
			continue;
		} else if(hwtype == "ETHERNET") {
			struct ethernet_frame {
				char dest_mac[6];
				char src_mac[6];
				uint16_t type;
			};
			if(datalen < sizeof(ethernet_frame)) {
				// drop packet, it's too short
				continue;
			}
			auto *eth_frame = reinterpret_cast<ethernet_frame*>(frame);
			uint16_t eth_type = ntohs(eth_frame->type);
			if(eth_type == 0x0806 /* ARP */) {
				get_arp().handle_arp_frame(shared_from_this(), frame, datalen, sizeof(ethernet_frame));
			}
			// didn't understand eth_type
			// TODO: IP
			continue;
		} else {
			assert(!"Unknown hardware type");
		}
	}
}

mstd::optional<std::string> interface::get_primary_ipv4addr() const
{
	for(auto &addr : ipv4addrs) {
		// TODO: we could filter by flags here
		std::string r(addr.ip, sizeof(addr.ip));
		return r;
	}
	return {};
}
