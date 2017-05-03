#include "arp.hpp"
#include <cassert>

using namespace networkd;

#define DEFAULT_ARP_VALIDITY 60 * 1000 * 1000 * 1000 /* 60 seconds */
#define ARP_REQUEST 1
#define ARP_RESPONSE 2

struct arp_frame_header {
	uint16_t htype;
	uint16_t ptype;
	uint8_t hlen;
	uint8_t plen;
	uint16_t oper;
};

static bool entry_valid(arp_entry const &entry, cloudabi_timestamp_t now) {
	if(entry.valid_until >= now) {
		return false;
	}
	auto iface_shared = entry.iface.lock();
	return iface_shared != nullptr;
}

static void fill_arp(char *arp_frm, size_t frame_len, char oper, std::string sender_mac, std::string sender_ip, std::string target_mac, std::string target_ip)
{
	assert(!sender_mac.empty());
	assert(!sender_ip.empty());

	assert(sender_mac.length() == target_mac.length());
	assert(sender_ip.length() == target_ip.length());

	auto *arp_hdr = reinterpret_cast<arp_frame_header*>(arp_frm);
	arp_hdr->htype = htons(1); /* Ethernet */
	arp_hdr->ptype = htons(0x0800); /* IPv4 */
	arp_hdr->hlen = sender_mac.length();
	arp_hdr->plen = sender_ip.length();
	arp_hdr->oper = htons(oper);

	char *arp_pld = arp_frm + sizeof(arp_frame_header);
	size_t offset = 0;
	memcpy(arp_pld, sender_mac.c_str(), arp_hdr->hlen);
	offset += arp_hdr->hlen;
	memcpy(arp_pld + offset, sender_ip.c_str(), arp_hdr->plen);
	offset += arp_hdr->plen;
	memcpy(arp_pld + offset, target_mac.c_str(), arp_hdr->hlen);
	offset += arp_hdr->hlen;
	memcpy(arp_pld + offset, target_ip.c_str(), arp_hdr->plen);
	offset += arp_hdr->plen;

	assert(sizeof(arp_frame_header) + offset == frame_len);
}

static void fill_eth(char *eth_frm, size_t frame_len, std::string src_mac, std::string dest_mac = std::string(6, 0xff))
{
	assert(frame_len == 14);
	const size_t hwlen = 6;
	assert(src_mac.length() == hwlen);
	assert(dest_mac.length() == hwlen);

	memcpy(eth_frm,         dest_mac.c_str(), hwlen);
	memcpy(eth_frm + hwlen, src_mac.c_str(),  hwlen);
	eth_frm[12] = 0x08;
	eth_frm[13] = 0x06; /* ARP */
}

arp::arp()
{
}

std::list<arp_entry> arp::copy_table()
{
	std::lock_guard<std::mutex> lock(table_mutex);
	std::list<arp_entry> res;
	auto current_time = monotime();
	for(auto it = arp_table.begin(); it != arp_table.end();) {
		if(!entry_valid(*it, current_time)) {
			// remove invalid entry
			it = arp_table.erase(it);
		}
		res.push_back(*it);
		it++;
	}
	return res;
}

mstd::optional<std::string> arp::mac_for_ip(std::shared_ptr<interface> iface, std::string ip, cloudabi_timestamp_t timeout)
{
	if(ip.length() != 4) {
		throw std::runtime_error("IP length mismatch");
	}

	std::unique_lock<std::mutex> lock(table_mutex);
	cloudabi_timestamp_t stoptime = monotime() + timeout;
	cloudabi_timestamp_t now;
	while((now = monotime()) && now < stoptime) {
		for(auto it = arp_table.begin(); it != arp_table.end();) {
			if(!entry_valid(*it, now)) {
				// remove invalid entry
				it = arp_table.erase(it);
				continue;
			}
			auto iface_shared = it->iface.lock();
			if(iface_shared && iface_shared == iface && memcmp(it->ip, ip.c_str(), 4) == 0) {
				return std::string(it->mac, 6);
			}
			++it;
		}

		send_arp_request(iface, ip);

		if(timeout > 0) {
			// wait until ARP table is updated
			// TODO: include a timeout once kernel supports it
			// table_cv.wait_until(lock, std::chrono::nanoseconds(stoptime));
			table_cv.wait(lock);
		}
	}
	return {};
}

void arp::add_entry(std::shared_ptr<interface> iface, std::string ip, std::string mac, cloudabi_timestamp_t validity)
{
	if(ip.size() != 4) {
		throw std::runtime_error("IP size must be 4");
	}
	if(mac.size() != 6) {
		throw std::runtime_error("MAC size must be 6");
	}

	// find existing entry for this IP
	std::lock_guard<std::mutex> lock(table_mutex);
	auto now = monotime();
	for(auto it = arp_table.begin(); it != arp_table.end(); ++it) {
		if(it->hwtype == iface->get_hwtype()
		&& memcmp(it->ip, ip.c_str(), sizeof(it->ip)) == 0
		&& it->iface.lock() == iface)
		{
			// update MAC and validity
			memcpy(it->mac, mac.c_str(), sizeof(it->mac));
			it->valid_until = monotime() + validity;
			table_cv.notify_all();
			return;
		}

		if(!entry_valid(*it, now)) {
			// remove invalid entry
			it = arp_table.erase(it);
		} else {
			++it;
		}
	}

	// no existing entry, make a new one
	arp_entry e;
	e.hwtype = iface->get_hwtype();
	memcpy(e.mac, mac.c_str(), sizeof(e.mac));
	memcpy(e.ip, ip.c_str(), sizeof(e.ip));
	e.iface = iface;
	e.valid_until = monotime() + validity;

	arp_table.emplace_back(std::move(e));
	table_cv.notify_all();
}

void arp::handle_arp_frame(std::shared_ptr<interface> iface, const char *frame, size_t framelen, size_t arp_offset)
{
	if(arp_offset + sizeof(arp_frame_header) >= framelen) {
		// arp packet does not fit, drop frame
		return;
	}
	auto *arp_hdr = reinterpret_cast<const arp_frame_header*>(frame + arp_offset);

	size_t arp_address_length = 2 * (arp_hdr->hlen + arp_hdr->plen);
	if(arp_offset + sizeof(arp_frame_header) + arp_address_length >= framelen) {
		// arp addresses do not fit, drop frame
		return;
	}

	if(ntohs(arp_hdr->htype) != 1 /* ethernet */) {
		return;
	}

	if(ntohs(arp_hdr->ptype) != 0x0800 /* IPv4 */) {
		return;
	}

	if(arp_hdr->hlen != 6 || arp_hdr->plen != 4) {
		// mismatching hardware and protocol address lengths for these protocols
		return;
	}

	auto oper = ntohs(arp_hdr->oper);
	if(oper != ARP_REQUEST && oper != ARP_RESPONSE) {
		// not request or response
		return;
	}

	const char *arp_pld = frame + arp_offset + sizeof(arp_frame_header);

	std::string sender_mac(arp_pld, arp_hdr->hlen);
	std::string sender_ip(arp_pld + arp_hdr->hlen, arp_hdr->plen);
	std::string target_ip(arp_pld + arp_hdr->hlen * 2 + arp_hdr->plen, arp_hdr->plen);

	add_entry(iface, sender_ip,sender_mac, DEFAULT_ARP_VALIDITY);

	bool send_response = false;
	if(oper == ARP_REQUEST) {
		for(auto &addr : iface->get_ipv4addrs()) {
			if(memcmp(addr.ip, target_ip.c_str(), arp_hdr->plen) == 0) {
				send_response = true;
				break;
			}
		}
	}

	if(send_response) {
		std::string my_mac = iface->get_mac();
		if(my_mac.size() != arp_hdr->hlen) {
			throw std::runtime_error("MAC length mismatch");
		}
		alignas(arp_frame_header) char arp_frm[sizeof(arp_frame_header) + 20];
		std::string zero(4, 0);
		fill_arp(arp_frm, sizeof(arp_frm), ARP_RESPONSE, my_mac, target_ip, sender_mac, sender_ip);

		char eth_frm[14];
		fill_eth(eth_frm, sizeof(eth_frm), sender_mac);

		std::vector<iovec> vecs(2);
		vecs[0].iov_base = eth_frm;
		vecs[0].iov_len = sizeof(eth_frm);
		vecs[1].iov_base = arp_frm;
		vecs[1].iov_len = sizeof(arp_frm);

		iface->send_frame(vecs);
	}
}

void arp::send_arp_request(std::shared_ptr<interface> iface, std::string ip)
{
	std::string zero_ip(4, 0);
	std::string zero_mac(6, 0);
	std::string my_mac = iface->get_mac();
	mstd::optional<std::string> if_ip = iface->get_primary_ipv4addr();
	std::string my_ip = if_ip ? *if_ip : zero_ip;

	alignas(arp_frame_header) char arp_frm[sizeof(arp_frame_header) + 20];
	auto *arp_hdr = reinterpret_cast<arp_frame_header*>(arp_frm);
	fill_arp(arp_frm, sizeof(arp_frm), ARP_REQUEST, my_mac, my_ip, zero_mac, ip);

	if(my_mac.size() != arp_hdr->hlen) {
		throw std::runtime_error("MAC length mismatch");
	}
	if(if_ip->length() != arp_hdr->plen) {
		throw std::runtime_error("IP length mismatch");
	}

	char eth_frm[14];
	fill_eth(eth_frm, sizeof(eth_frm), iface->get_mac());

	std::vector<iovec> vecs(2);
	vecs[0].iov_base = eth_frm;
	vecs[0].iov_len = sizeof(eth_frm);
	vecs[1].iov_base = arp_frm;
	vecs[1].iov_len = sizeof(arp_frm);

	iface->send_frame(vecs);
}

bool arp::probe_ip_is_taken(std::shared_ptr<interface> iface, std::string ip, cloudabi_timestamp_t timeout)
{
	// TODO: officially (per RFC 5227) we SHOULD also check if another machine
	// is probing this IP, and return false if so.
	// TODO: we may have to send a probe even if the entry already exists
	// in the table; maybe the DHCP server gave us this IP because it knows
	// that other lease has already expired and nobody is using it anymore?
	auto mac = mac_for_ip(iface, ip, timeout);
	return mac && *mac != iface->get_mac();
}

void arp::send_gratuitous_arp(std::shared_ptr<interface> iface, std::string ip)
{
	alignas(arp_frame_header) char arp_frm[sizeof(arp_frame_header) + 20];
	std::string my_mac = iface->get_mac();
	std::string zero(4, 0);
	fill_arp(arp_frm, sizeof(arp_frm), ARP_REQUEST, my_mac, ip, zero, ip);

	char eth_frm[14];
	fill_eth(eth_frm, sizeof(eth_frm), iface->get_mac());

	std::vector<iovec> vecs(2);
	vecs[0].iov_base = eth_frm;
	vecs[0].iov_len = sizeof(eth_frm);
	vecs[1].iov_base = arp_frm;
	vecs[1].iov_len = sizeof(arp_frm);

	iface->send_frame(vecs);
}
