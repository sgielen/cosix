#include "routing_table.hpp"
#include <cassert>

using namespace networkd;

static bool entry_valid(routing_entry const &entry) {
	auto iface_shared = entry.iface.lock();
	return iface_shared != nullptr;
}

static bool cidr_ip_matches(uint8_t cidr_prefix, uint32_t left_ip, uint32_t right_ip) {
	uint32_t mask = 0;
	assert(cidr_prefix <= 32);
	for(size_t bit = 0; bit < cidr_prefix; ++bit) {
		mask |= uint32_t(1) << (31 - bit);
	}
	return (left_ip & mask) == (right_ip & mask);
}

routing_table::routing_table()
{
}

std::list<routing_entry> routing_table::copy_table()
{
	std::lock_guard<std::mutex> lock(table_mutex);
	std::list<routing_entry> res;
	for(auto it = table.begin(); it != table.end();) {
		if(!entry_valid(*it)) {
			// remove invalid entry
			it = table.erase(it);
		}
		res.push_back(*it);
		it++;
	}
	return res;
}

mstd::optional<std::pair<std::string, std::shared_ptr<interface>>>
routing_table::routing_rule_for_ip(std::string ip)
{
	if(ip.length() != 4) {
		throw std::runtime_error("IP length mismatch");
	}

	std::unique_lock<std::mutex> lock(table_mutex);

	int best_cidr_prefix = -2;
	std::string gateway_ip;
	std::shared_ptr<interface> iface;

	for(auto it = table.begin(); it != table.end();) {
		if(!entry_valid(*it)) {
			// remove invalid entry
			it = table.erase(it);
			continue;
		}
		auto &entry = *it;
		it++;

		if(entry.ip.empty()) {
			// this is a default gateway rule, only use it if we have nothing yet
			if(best_cidr_prefix != -2) {
				continue;
			}
			std::shared_ptr<interface> locked_iface = entry.iface.lock();
			if(locked_iface) {
				best_cidr_prefix = -1;
				gateway_ip = entry.gateway_ip;
				iface = locked_iface;
			}
			continue;
		}

		assert(entry.cidr_prefix <= 32);
		assert(entry.ip.size() == 4);
		if(entry.cidr_prefix > best_cidr_prefix) {
			// this might be a better rule, if the IP matches
			uint32_t rule_ip = *reinterpret_cast<uint32_t const*>(entry.ip.c_str());
			uint32_t matching_ip = *reinterpret_cast<uint32_t const*>(ip.c_str());
			if(cidr_ip_matches(entry.cidr_prefix, rule_ip, matching_ip)) {
				// IP matches, replace rule
				std::shared_ptr<interface> locked_iface = entry.iface.lock();
				// it's possible that entry.iface expired since
				// the check to entry_valid, so only use this
				// entry if the interface is still valid
				if(locked_iface) {
					iface = locked_iface;
					best_cidr_prefix = entry.cidr_prefix;
					gateway_ip = entry.gateway_ip;
				}
			}
		}
	}

	if(best_cidr_prefix == -2) {
		// no matches
		return {};
	} else {
		return std::make_pair(gateway_ip, iface);
	}
}

void routing_table::add_entry(std::shared_ptr<interface> iface, std::string ip, int cidr_prefix, std::string gateway_ip)
{
	if(ip.empty()) {
		cidr_prefix = -1;
	} else if(ip.size() != 4) {
		throw std::runtime_error("IP size must be empty or 4");
	}
	if(!gateway_ip.empty() && gateway_ip.size() != 4) {
		throw std::runtime_error("Gateway size must be empty or 4");
	}

	// find existing entry for this IP + CIDR
	std::lock_guard<std::mutex> lock(table_mutex);
	for(auto it = table.begin(); it != table.end();) {
		if(it->ip == ip && it->cidr_prefix == cidr_prefix) {
			// update entry
			it->gateway_ip = gateway_ip;
			it->iface = iface;
			table_cv.notify_all();
			return;
		}

		if(!entry_valid(*it)) {
			// remove invalid entry
			it = table.erase(it);
		} else {
			++it;
		}
	}

	// no existing entry, make a new one
	routing_entry e;
	e.ip = ip;
	e.cidr_prefix = cidr_prefix;
	e.gateway_ip = gateway_ip;
	e.iface = iface;

	table.emplace_back(std::move(e));
	table_cv.notify_all();
}

void routing_table::add_link_route(std::shared_ptr<interface> iface, std::string ip, int cidr_prefix)
{
	std::string zero(4, 0);
	add_entry(iface, ip, cidr_prefix, zero);
}

void routing_table::set_default_gateway(std::shared_ptr<interface> iface, std::string gateway_ip)
{
	add_entry(iface, "", 0, gateway_ip);
}

void routing_table::unset_default_gateway(std::shared_ptr<interface> iface)
{
	std::lock_guard<std::mutex> lock(table_mutex);
	for(auto it = table.begin(); it != table.end(); ) {
		if(it->ip.empty()) {
			auto iface_shared = it->iface.lock();
			if(iface_shared == iface) {
				// remove entry
				it = table.erase(it);
				continue;
			}
		}
		if(!entry_valid(*it)) {
			it = table.erase(it);
		} else {
			++it;
		}
	}
}
