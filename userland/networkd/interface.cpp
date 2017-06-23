#include "interface.hpp"
#include "networkd.hpp"
#include "arp.hpp"
#include "ip.hpp"
#include <unistd.h>
#include <cassert>
#include <cloudabi_types.h>
#include <cloudabi_syscalls.h>
#include <arpa/inet.h>

using namespace networkd;

#define IP_ARP_TIMEOUT 5ull * 1000 * 1000 * 1000 /* 5 seconds */

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
	auto that = shared_from_this();
	thr = std::thread([that](){
		try {
			that->run();
		} catch(std::exception &e) {
			dprintf(0, "*** Uncaught exception in handling incoming data on %s interface %s\n",
				that->hwtype.c_str(), that->name.c_str());
			dprintf(0, "*** Error: \"%s\"\n", e.what());
		}
	});
}

void interface::add_ipv4addr(const char *ip, uint8_t cidr_prefix)
{
	interface_ipv4addr a;
	memcpy(a.ip, ip, sizeof(a.ip));
	a.cidr_prefix = cidr_prefix;
	ipv4addrs.emplace_back(std::move(a));
}

cloudabi_errno_t interface::send_ip_packet(std::vector<iovec> const &in_vecs, std::string ip_hop)
{
	if(get_hwtype() == "LOOPBACK") {
		send_frame(in_vecs);
		return 0;
	}

	auto opt_mac = get_arp().mac_for_ip(shared_from_this(), ip_hop);
	if(!opt_mac) {
		// don't know how to address this IP, send an ARP request and put it in the waiting
		// request queue
		get_arp().send_arp_request(shared_from_this(), ip_hop);
		arp_pending_frame f;
		f.ip_hop = ip_hop;
		f.timeout = monotime() + IP_ARP_TIMEOUT;
		for(auto const &vec : in_vecs) {
			f.frame.append(reinterpret_cast<const char*>(vec.iov_base), vec.iov_len);
		}
		frames_pending_arp.emplace_back(std::move(f));
		return 0;
	}

	return send_frame(in_vecs, *opt_mac);
}

void interface::check_pending_arp_list() {
	for(auto it = frames_pending_arp.begin(); it != frames_pending_arp.end();) {
		if(it->timeout < monotime()) {
			// remove it
			it = frames_pending_arp.erase(it);
			continue;
		}

		auto opt_mac = get_arp().mac_for_ip(shared_from_this(), it->ip_hop);
		if(opt_mac) {
			// got a MAC, we can send it now
			{
				std::vector<iovec> vecs(1);
				vecs[0].iov_base = const_cast<char*>(it->frame.c_str());
				vecs[0].iov_len = it->frame.length();
				send_frame(vecs, *opt_mac);
			}
			it = frames_pending_arp.erase(it);
			continue;
		}

		// can't send it yet
		it++;
	}
}

cloudabi_errno_t interface::send_frame(std::vector<iovec> in_vecs, std::string mac_hop) {
	char eth_frm[14];
	const size_t hwlen = 6;
	assert(mac_hop.length() == hwlen);
	assert(get_mac().length() == hwlen);

	memcpy(eth_frm,         mac_hop.c_str(),    hwlen);
	memcpy(eth_frm + hwlen, get_mac().c_str(),  hwlen);
	eth_frm[12] = 0x08;
	eth_frm[13] = 0x00; /* IPv4 */

	std::vector<iovec> vecs(in_vecs.size()+1);
	vecs[0].iov_base = eth_frm;
	vecs[0].iov_len = sizeof(eth_frm);
	for(size_t i = 0; i < in_vecs.size(); ++i) {
		vecs[i+1] = in_vecs[i];
	}

	return send_frame(vecs);
}

cloudabi_errno_t interface::send_frame(std::vector<iovec> iov) {
	// Ethernet trailer & check sum
	// Ethernet packets must be a minimum of 60 bytes, plus a check-sum at the end.
	// The checksum is computed in the kernel (because it can be offloaded to hardware).
	char trailer[64];
	memset(trailer, 0, sizeof(trailer));

	size_t packet_size = 0;
	for(size_t i = 0; i < iov.size(); ++i) {
		packet_size += iov[i].iov_len;
	}
	size_t traileridx = iov.size();
	iov.resize(traileridx + 1);
	iov[traileridx].iov_base = trailer;
	iov[traileridx].iov_len = (packet_size < 60 ? 60 - packet_size : 0) + 4;
	assert(iov[traileridx].iov_len <= sizeof(trailer));

	cloudabi_send_in_t in;
	in.si_data = const_cast<cloudabi_ciovec_t*>(
		reinterpret_cast<const cloudabi_ciovec_t*>(iov.data()));
	in.si_data_len = iov.size();
	in.si_fds = 0;
	in.si_fds_len = 0;
	in.si_flags = 0;
	cloudabi_send_out_t out;
	return cloudabi_sys_sock_send(rawsock, &in, &out);
}

void interface::run() {
	std::shared_ptr<interface> that = shared_from_this();
	assert(that != nullptr);
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
			get_ip().handle_packet(that, frame, datalen, 0);
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
				get_arp().handle_arp_frame(that, frame, datalen, sizeof(ethernet_frame));
				check_pending_arp_list();
			} else if(eth_type == 0x0800 /* IPv4 */) {
				get_ip().handle_packet(that, frame, datalen, sizeof(ethernet_frame));
			}
			// didn't understand eth_type, drop
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
