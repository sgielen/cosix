#pragma once
#include "interface.hpp"
#include <memory>
#include <thread>
#include <list>
#include "util.hpp"

#include <mstd/optional.hpp>

namespace networkd {

struct routing_entry {
	// if IP is empty, this is a default gateway entry and the CIDR prefix
	// is ignored
	std::string ip;
	uint8_t cidr_prefix;
	// gateway_ip contains the packed IP of the hop to send packets to for
	// the given range; if gateway is empty, destinations are directly
	// reachable over this link
	std::string gateway_ip;
	std::weak_ptr<interface> iface;
	// TODO: add preference, metric, and active
	// take the rules that are most specific, highest preference, shortest
	// metric; if exactly one is active use that one, otherwise make
	// exactly one active and use it
};

struct routing_table {
	routing_table();

	std::list<routing_entry> copy_table();

	// Find the most specific matching routing entry for the given IP
	// address (packed format), if any.
	mstd::optional<std::pair<std::string, std::weak_ptr<interface>>> routing_rule_for_ip(std::string ip);

	// Add a routing entry to the table.
	void add_entry(std::shared_ptr<interface> iface, std::string ip, int cidr_prefix, std::string gateway_ip);

	// Shorthand for adding a link-local route to the table.
	void add_link_route(std::shared_ptr<interface> iface, std::string ip, int cidr_prefix);

	// Shorthand for adding a default gateway to the table.
	void add_default_gateway(std::shared_ptr<interface>, std::string gateway_ip);

private:
	std::mutex table_mutex;
	std::condition_variable table_cv;
	std::list<routing_entry> table;
};

}
