#include "interface.hpp"
#include <memory>
#include <thread>
#include <list>
#include "util.hpp"

#include <mstd/optional.hpp>

namespace networkd {

struct arp_entry {
	std::string hwtype;
	char mac[6];
	char ip[4];
	std::weak_ptr<interface> iface;
	cloudabi_timestamp_t valid_until;
};

struct arp {
	arp();

	std::list<arp_entry> copy_table();

	// Returns the MAC address, if possible. If the IP is not known in the
	// ARP table, a request for it will be sent. If timeout is greater than
	// zero, this method will block if necessary, pending arp responses. IP
	// and MAC are in packed format, so their sizes are 4 and 6 respectively.
	mstd::optional<std::string> mac_for_ip(std::shared_ptr<interface> iface, std::string ip, cloudabi_timestamp_t timeout);

	// Add an ARP entry to the table. IP and MAC are in packed format, so
	// their sizes are 4 and 6 respectively.
	void add_entry(std::shared_ptr<interface> iface, std::string ip, std::string mac, cloudabi_timestamp_t validity);

	// Handle an incoming ARP frame
	void handle_arp_frame(std::shared_ptr<interface> iface, const char *frame, size_t framelen, size_t arp_offset);

	// Send an ARP request
	void send_arp_request(std::shared_ptr<interface> iface, std::string ip);

	// Probe to see if an IP address is taken, returns true if so, false if not.
	// Will wait $timeout ns to return false, but returns true as soon as an
	// ARP response is received.
	bool probe_ip_is_taken(std::shared_ptr<interface> iface, std::string ip, cloudabi_timestamp_t timeout);

	// Send a gratuitous ARP broadcast saying that the interface has the given IP
	void send_gratuitous_arp(std::shared_ptr<interface> iface, std::string ip);

private:
	std::mutex table_mutex;
	std::condition_variable table_cv;
	std::list<arp_entry> arp_table;
};

}
