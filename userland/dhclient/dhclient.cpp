#include <stdio.h>
#include <stdlib.h>
#include <program.h>
#include <argdata.h>
#include <string.h>
#include <unistd.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/capsicum.h>
#include <cloudabi_syscalls.h>
#include <thread>
#include <vector>
#include <arpa/inet.h>
#include <cassert>

int stdout;
int networkd;
int rawsock;
std::string interface;
bool discovering = false;
bool requesting = false;
uint8_t active_xid[4];

cloudabi_timestamp_t next_discover = 0;

const int DHCPDISCOVER = 1;
const int DHCPOFFER = 2;
const int DHCPREQUEST = 3;
/* const int DHCPDECLINE = 4; */
const int DHCPACK = 5;
/* const int DHCPNAK = 6; */
/* const int DHCPRELEASE = 7; */

const char * const dhcpmagic = "\x63\x82\x53\x63";

static cloudabi_timestamp_t monotime() {
	cloudabi_timestamp_t ts = 0;
	cloudabi_sys_clock_time_get(CLOUDABI_CLOCK_MONOTONIC, 0, &ts);
	return ts;
}

static cloudabi_timestamp_t monotime_after(uint64_t seconds) {
	return monotime() + seconds * 1000000000;
}

size_t send_if_command(std::string command, std::string arg, char *buf, size_t bufsize, int *fd) {
	argdata_t *keys[] = {argdata_create_str_c("command"), argdata_create_str_c("interface"), argdata_create_str_c("arg")};
	argdata_t *values[] = {argdata_create_str_c(command.c_str()), argdata_create_str_c(interface.c_str()), argdata_create_str_c(arg.c_str())};
	argdata_t *req = argdata_create_map(keys, values, sizeof(keys) / sizeof(keys[0]));

	size_t len;
	argdata_serialized_length(req, &len, 0);

	char rbuf[len];
	argdata_serialize(req, rbuf, 0);

	argdata_free(keys[0]);
	argdata_free(keys[1]);
	argdata_free(keys[2]);
	argdata_free(values[0]);
	argdata_free(values[1]);
	argdata_free(values[2]);
	argdata_free(req);

	write(networkd, rbuf, len);
	// TODO: set non-blocking flag once kernel supports it
	// this way, we can read until EOF instead of only 200 bytes
	// TODO: for a generic implementation, MSG_PEEK to find the number
	// of file descriptors
	struct iovec iov = {.iov_base = buf, .iov_len = bufsize};
	alignas(struct cmsghdr) char control[CMSG_SPACE(sizeof(int))];
	struct msghdr msg = {
		.msg_iov = &iov, .msg_iovlen = 1,
		.msg_control = control, .msg_controllen = sizeof(control),
	};
	ssize_t size = recvmsg(networkd, &msg, 0);
	if(size < 0) {
		perror("read");
		exit(1);
	}
	if(fd) {
		struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
		if(cmsg == nullptr || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_len != CMSG_LEN(sizeof(int))) {
			dprintf(stdout, "Ifstore socket requested, but not given\n");
			exit(1);
		}
		int *fdbuf = reinterpret_cast<int*>(CMSG_DATA(cmsg));
		*fd = fdbuf[0];
	}
	return size;
}

const argdata_t *ad_from_map(argdata_t *ad, std::string needle) {
	argdata_map_iterator_t it;
	const argdata_t *key;
	const argdata_t *value;
	argdata_map_iterate(ad, &it);
	while (argdata_map_next(&it, &key, &value)) {
		const char *keystr;
		if(argdata_get_str_c(key, &keystr) != 0) {
			continue;
		}

		if(strcmp(keystr, needle.c_str()) == 0) {
			return value;
		}
	}
	return nullptr;
}

std::string string_from_ad(const argdata_t *ad) {
	const char *str;
	size_t len;
	argdata_get_str(ad, &str, &len);
	return std::string(str, len);
}

std::string string_from_map(argdata_t *ad, std::string needle) {
	const argdata_t *str = ad_from_map(ad, needle);
	if(str) {
		return string_from_ad(str);
	}
	return "";
}

void dump_interfaces() {
	char buf[200];
	size_t size = send_if_command("dump", "", buf, sizeof(buf), nullptr);
	argdata_t *response = argdata_from_buffer(buf, size);
	std::string error = string_from_map(response, "error");
	if(!error.empty()) {
		dprintf(stdout, "Error: %s\n", error.c_str());
	}
}

std::string get_mac() {
	char buf[200];
	size_t size = send_if_command("mac", "", buf, sizeof(buf), nullptr);
	argdata_t *response = argdata_from_buffer(buf, size);
	std::string error = string_from_map(response, "error");
	if(!error.empty()) {
		dprintf(stdout, "Error: %s\n", error.c_str());
		return "";
	}
	return string_from_map(response, "mac");
}

std::string get_hwtype() {
	char buf[200];
	size_t size = send_if_command("hwtype", "", buf, sizeof(buf), nullptr);
	argdata_t *response = argdata_from_buffer(buf, size);
	std::string error = string_from_map(response, "error");
	if(!error.empty()) {
		dprintf(stdout, "Error: %s\n", error.c_str());
		return "";
	}
	return string_from_map(response, "hwtype");
}

std::vector<std::string> get_v4addr() {
	char buf[200];
	size_t size = send_if_command("addrv4", "", buf, sizeof(buf), nullptr);
	argdata_t *response = argdata_from_buffer(buf, size);
	std::string error = string_from_map(response, "error");
	std::vector<std::string> res;
	if(!error.empty()) {
		dprintf(stdout, "Error: %s\n", error.c_str());
		return res;
	}
	const argdata_t *v4addrs = ad_from_map(response, "addrv4");
	argdata_seq_iterator_t it;
	argdata_seq_iterate(v4addrs, &it);
	const argdata_t *v4addr;
	while(argdata_seq_next(&it, &v4addr)) {
		res.push_back(string_from_ad(v4addr));
	}
	return res;
}

void add_v4addr(std::string ip) {
	char buf[200];
	size_t size = send_if_command("add_addrv4", ip, buf, sizeof(buf), nullptr);
	argdata_t *response = argdata_from_buffer(buf, size);
	std::string error = string_from_map(response, "error");
	if(!error.empty()) {
		dprintf(stdout, "Error: %s\n", error.c_str());
	}
}

int get_rawsock() {
	char buf[200];
	int fd;
	size_t size = send_if_command("rawsock", "", buf, sizeof(buf), &fd);
	argdata_t *response = argdata_from_buffer(buf, size);
	std::string error = string_from_map(response, "error");
	if(!error.empty()) {
		dprintf(stdout, "Error: %s\n", error.c_str());
		return -1;
	}
	const argdata_t *fdad = ad_from_map(response, "fd");
	int fdidx;
	argdata_get_fd(fdad, &fdidx);
	if(fdidx != 0) {
		dprintf(stdout, "FD index not as expected\n");
		exit(1);
	}
	return fd;
}

// Parses MACs like AA-BB-CC-DD-EE-FF
void get_raw_mac(uint8_t *buf, size_t bufsize, size_t *macsize) {
	std::string mac = get_mac();
	size_t i = 0;
	while(i < bufsize) {
		char *end;
		buf[i++] = strtol(mac.c_str(), &end, 16);
		if(mac.size() == 2) {
			break;
		}
		mac = mac.substr(3);
	}
	*macsize = i;
	while(i < bufsize) {
		buf[i++] = 0;
	}
}

void send_dhcp_packet(uint8_t *bootp, size_t bootp_size) {
	uint8_t source[4], destination[4];
	source[0] = source[1] = source[2] = source[3] = 0;
	destination[0] = destination[1] = destination[2] = destination[3] = 0xff;

	// TODO: iovecs would be super-useful here
	uint16_t pseudo_ip_length = 12;
	uint16_t udp_length = bootp_size + 8;
	uint8_t *pseudo_packet = reinterpret_cast<uint8_t*>(malloc(pseudo_ip_length + udp_length));
	uint16_t *udp_header = reinterpret_cast<uint16_t*>(pseudo_packet + pseudo_ip_length);
	udp_header[0] = htons(68);
	udp_header[1] = htons(67);
	udp_header[2] = htons(udp_length);
	udp_header[3] = 0;
	memcpy(pseudo_packet + pseudo_ip_length + 8, bootp, bootp_size);

	// TODO: compute checksum, although UDP allows leaving it at zero

	uint8_t *payload = pseudo_packet + pseudo_ip_length;
	// TODO: iovecs would be super-useful here
	uint16_t header_length = 20;
	uint16_t ip_length = udp_length + header_length;
	uint8_t *packet = reinterpret_cast<uint8_t*>(malloc(ip_length));
	packet[0] = 0x40 | (header_length / 4); // version + header words
	packet[1] = 0; // DSCP + ECN
	packet[2] = ip_length >> 8; // total length
	packet[3] = ip_length & 0xff; // total length
	packet[4] = 0xf5; // identification TODO 0
	packet[5] = 0xe9; // identification
	packet[6] = 0; // fragment flags & offset
	packet[7] = 0; // fragment offset
	packet[8] = 0xff; // TTL
	packet[9] = 17; // UDP
	packet[10] = 0; // checksum
	packet[11] = 0; // checksum
	memcpy(packet + 12, source, 4);
	memcpy(packet + 16, destination, 4);
	memcpy(packet + 20, payload, udp_length);

	// compute checksum
	uint16_t *p16 = reinterpret_cast<uint16_t*>(packet);
	uint32_t sum = 0;
	for(size_t i = 0; i < header_length / 2; ++i) {
		sum += p16[i];
	}
	uint16_t checksum = ~((sum >> 16) + sum & 0xffff);
	packet[10] = checksum & 0xff;
	packet[11] = checksum >> 8;

	uint8_t mac[16];
	size_t macsize;
	get_raw_mac(mac, sizeof(mac), &macsize);

	size_t frame_size = ip_length + 18 /* length of ethernet header + crc */;
	uint8_t *frame = reinterpret_cast<uint8_t*>(malloc(frame_size));
	// TODO: look up destination MAC
	memcpy(frame, "\xff\xff\xff\xff\xff\xff", 6);
	memcpy(frame + 6, mac, 6);
	frame[12] = 0x08; // IPv4
	frame[13] = 0x00; // TODO: have upper layer communicate what kind of (IP?) packet this is
	memcpy(frame + 14, packet, ip_length);

	// TODO: ethernet CRC
	frame[ip_length + 14] = 0;
	frame[ip_length + 15] = 0;
	frame[ip_length + 16] = 0;
	frame[ip_length + 17] = 0;

	struct iovec iov = {.iov_base = frame, .iov_len = frame_size};
	struct msghdr msg = {
		.msg_iov = &iov, .msg_iovlen = 1,
	};
	assert(rawsock >= 0);
	ssize_t size = sendmsg(rawsock, &msg, 0);
	if(size < 0) {
		perror("Failed to send to rawsock");
		abort();
	}
}

void perform_discover() {
	uint8_t mac[16];
	size_t macsize;
	get_raw_mac(mac, sizeof(mac), &macsize);

	arc4random_buf(active_xid, sizeof(active_xid));

	// TODO: this should probably be less hardcoded
	uint8_t bootp[] = {
		/* bootp */
		0x01, 0x01, 0x06, 0x00, // bootp request, ethernet, hw address length 6, 0 hops
		active_xid[0], active_xid[1], active_xid[2], active_xid[3], // id
		0x00, 0x00, 0x00, 0x00, // no time elapsed, no flags
		0x00, 0x00, 0x00, 0x00, // client IP
		0x00, 0x00, 0x00, 0x00, // "your" IP
		0x00, 0x00, 0x00, 0x00, // next server IP
		0x00, 0x00, 0x00, 0x00, // relay IP,
		mac[0], mac[1], mac[2], mac[3], // client MAC
		mac[4], mac[5], mac[6], mac[7], // client MAC
		mac[8], mac[9], mac[10], mac[11], // client MAC
		mac[12], mac[13], mac[14], mac[15], // client MAC
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // server host name
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // server host name
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // server host name
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // server host name
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // server host name
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // server host name
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // server host name
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // server host name
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // boot filename
		/* dhcp */
		0x63, 0x82, 0x53, 0x63, // dhcp magic cookie
		0x35, 0x01, DHCPDISCOVER, // dhcp request
		0x37, 0x0a, 0x01, 0x79, 0x03, 0x06, 0x0f, 0x77, 0xf, 0x5f, 0x2c, 0x2e, // request various other parameters as well
		0x39, 0x02, 0x05, 0xdc, // max response size
		0x3d, 0x07, 0x01, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], // client identifier
		/* missing: requested IP address */
		/* missing: IP address lease time */
		0x0c, 0x07, 'c', 'l', 'o', 'u', 'd', 'o', 's', // hostname
		0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 // end + padding
	};

	// Send the initial DHCP discover
	requesting = false;
	discovering = true;
	next_discover = monotime_after(10);

	send_dhcp_packet(bootp, sizeof(bootp));
	dprintf(stdout, "DHCP discover on %s...\n", interface.c_str());
}

void send_dhcp_request(uint8_t *old_payload)
{
	size_t dhcp_length = 250;
	uint8_t *dhcp = reinterpret_cast<uint8_t*>(malloc(dhcp_length));
	dhcp[0] = 1 /* request */;
	dhcp[1] = 1 /* ethernet */;
	dhcp[2] = 6 /* ethernet hardware addresses */;
	dhcp[3] = 0 /* hops */;
	memcpy(dhcp + 4, old_payload + 4, 4); // xid
	dhcp[8] = 0 /* seconds */;
	dhcp[9] = 0 /* seconds */;
	dhcp[10] = 0 /* flags */;
	dhcp[11] = 0 /* flags */;
	dhcp[12] = dhcp[13] = dhcp[14] = dhcp[15] = 0 /* ciaddr */;
	dhcp[16] = dhcp[17] = dhcp[18] = dhcp[19] = 0 /* yiaddr */;
	memcpy(dhcp + 20, old_payload + 20, 8); // siaddr and giaddr

	uint8_t mac[16];
	size_t macsize;
	get_raw_mac(mac, sizeof(mac), &macsize);

	memcpy(dhcp + 28, mac, macsize);
	for(size_t i = 28 + macsize; i < 236; ++i) {
		dhcp[i] = 0; // client hardware address, server hostname, boot filename
	}
	dhcp[236] = 0x63; // DHCP magic cookie
	dhcp[237] = 0x82;
	dhcp[238] = 0x53;
	dhcp[239] = 0x63;

	dhcp[240] = 0x35; // DHCP Message Type
	dhcp[241] = 0x01;
	dhcp[242] = DHCPREQUEST; // DHCP Request

	dhcp[243] = 0x32; // Requested IP Address
	dhcp[244] = 0x04;
	uint8_t *yiaddr = old_payload + 16;
	memcpy(dhcp + 245, yiaddr, 4);

	dhcp[249] = 0xff; // End of options

	discovering = false;
	requesting = true;
	send_dhcp_packet(dhcp, dhcp_length);
	dprintf(stdout, "DHCP request on %s...\n", interface.c_str());
}

void handle_rawsock_frame(uint8_t *frame, size_t size) {
	size_t frame_length = 14;
	if(size < frame_length) {
		return;
	}
	uint16_t ethertype = ntohs(*reinterpret_cast<uint16_t*>(frame + 12));

	// https://en.wikipedia.org/wiki/EtherType
	if(ethertype == 0x8100) {
		frame_length += 4;
		if(size < frame_length) {
			return;
		}
		ethertype = ntohs(*reinterpret_cast<uint16_t*>(frame + 16));
	}

	if(ethertype != 0x0800) {
		return;
	}

	uint8_t *packet = frame + frame_length;
	size_t ip_length = size - frame_length;
	uint8_t ip_version = packet[0] >> 4;
	if(ip_version != 4) {
		return;
	}

	if(ip_length < 20) {
		// shortest IP header does not fit in packet
		return;
	}

	uint16_t header_length = (packet[0] & 0x0f) * 4;
	if(header_length < 20 || ip_length < header_length) {
		// IP header is too short or does not fit in packet
		return;
	}

	uint16_t total_length = ntohs(*reinterpret_cast<uint16_t*>(&packet[2]));
	if(total_length < header_length) {
		// total length does not include header
		return;
	}
	if(ip_length < total_length) {
		// actual IP packet does not fit in packet
		return;
	}

	if(ip_length != total_length) {
		dprintf(stdout, "Packet contains more than just header and payload, since it is %d "
			"bytes, the IP header length is %d and the total IP length is %d\n",
			uint32_t(ip_length), header_length, total_length);
		// continue anyway
	}

	const uint8_t IPV4_FLAG_MORE_FRAGMENTS = 2;

	uint8_t flags = packet[6] >> 5;
	uint16_t fragment_offset = ntohs(*reinterpret_cast<uint16_t*>(&packet[6]) & 0x1fff);
	if(flags & IPV4_FLAG_MORE_FRAGMENTS || fragment_offset != 0) {
		// fragmented packets currently unsupported
		dprintf(stdout, "Fragmented IPv4 packet received, ignoring\n");
		return;
	}

	// TODO: TTL checking
	// TODO: checksum checking

	char ip_source_display[16];
	char ip_dest_display[16];
	uint8_t *ip_source = packet + 12;
	uint8_t *ip_dest = packet + 16;

	dprintf(stdout, "Received IPv4 packet from %s to %s\n",
		inet_ntop(AF_INET, ip_source, ip_source_display, sizeof(ip_source_display)),
		inet_ntop(AF_INET, ip_dest,   ip_dest_display,   sizeof(ip_dest_display)));

	uint8_t protocol = packet[9];
	if(protocol != 17 /* UDP */) {
		return;
	}

	uint8_t *udp_payload = packet + header_length;
	size_t udp_length = ip_length - header_length;
	
	uint16_t *udp_header = reinterpret_cast<uint16_t*>(udp_payload);
	uint16_t source_port = ntohs(udp_header[0]);
	uint16_t destination_port = ntohs(udp_header[1]);
	if(udp_length != ntohs(udp_header[2])) {
		return;
	}
	// TODO: check checksum
	dprintf(stdout, "  It's a UDP message from source port %d to destination port %d\n", source_port, destination_port);
	if(destination_port != 68) {
		return;
	}

	uint8_t *dhcp_payload = udp_payload + 8;
	size_t dhcp_length = udp_length - 8;

	/* minimal BOOTP length is 236, and DHCP also describes a 4-byte magic, so 240 */
	if(dhcp_length < 240) {
		dprintf(stdout, "  It's a too small BOOTP message, dropping\n");
		return;
	}

	uint8_t operation = dhcp_payload[0];
	if(operation != 2 /* response */) {
		dprintf(stdout, "  It's a BOOTP message that's not a response, dropping\n");
		return;
	}

	uint8_t htype = dhcp_payload[1];
	uint8_t hlen = dhcp_payload[2];
	if(htype != 0x01 || hlen != 0x06) {
		dprintf(stdout, "  It's a BOOTP message with an unknown HTYPE, dropping\n");
		return;
	}

	const uint8_t *xid = dhcp_payload + 4;
	if(memcmp(xid, active_xid, 4) != 0) {
		dprintf(stdout, "  It's a BOOTP message with an incorrect XID, dropping\n");
		return;
	}

	if(memcmp(dhcp_payload + 236, dhcpmagic, 4) != 0) {
		dprintf(stdout, "  It's a BOOTP message without the DHCP magic, dropping\n");
		return;
	}

	const uint8_t *options = dhcp_payload + 240;
	size_t options_length = dhcp_length - 240;

	// parse incoming options
	uint8_t dhcp_type = 0;

	while(options_length > 2) {
		uint8_t option = options[0];
		if(option == 0xff) {
			break;
		}

		uint8_t option_length = options[1];
		if(options_length - 2 < option_length) {
			dprintf(stdout, "  It's a DHCP message that ends early, dropping\n");
			return;
		}
		if(option == 0x35 && option_length == 1) {
			dhcp_type = options[2];
		}

		options += option_length + 2;
		options_length -= option_length + 2;
	}

	if(dhcp_type == 0) {
		dprintf(stdout, "  It's a DHCP message without a (valid) message type, dropping\n");
		return;
	}

	if(dhcp_type == DHCPOFFER) {
		dprintf(stdout, "  It's a DHCP offer for the following IPs:\n");
	} else if(dhcp_type == DHCPACK) {
		dprintf(stdout, "  It's a DHCP acknowledgement for the following IPs:\n");
	} else {
		dprintf(stdout, "  It's an unexpected DHCP message type %d, dropping\n", dhcp_type);
		return;
	}

	uint8_t *ciaddr = dhcp_payload + 12;
	uint8_t *yiaddr = dhcp_payload + 16;
	uint8_t *siaddr = dhcp_payload + 20;
	uint8_t *giaddr = dhcp_payload + 24;

	char ip_display[16];
	dprintf(stdout, "  ciaddr: %s\n", inet_ntop(AF_INET, ciaddr, ip_display, sizeof(ip_display)));
	dprintf(stdout, "  yiaddr: %s\n", inet_ntop(AF_INET, yiaddr, ip_display, sizeof(ip_display)));
	dprintf(stdout, "  siaddr: %s\n", inet_ntop(AF_INET, siaddr, ip_display, sizeof(ip_display)));
	dprintf(stdout, "  giaddr: %s\n", inet_ntop(AF_INET, giaddr, ip_display, sizeof(ip_display)));

	if(dhcp_type == DHCPOFFER && discovering) {
		// Currently, we just accept the very first offer we get. This
		// is fine for now, but per standard we should await all offers
		// for a while and pick the best one.
		dprintf(stdout, "  Sending DHCP request for IP address %s\n", inet_ntop(AF_INET, yiaddr, ip_display, sizeof(ip_display)));
		send_dhcp_request(dhcp_payload);
		return;
	} else if(dhcp_type == DHCPOFFER) {
		dprintf(stdout, "  ...But I'm not accepting offers anymore, so dropping.\n");
		return;
	} else if(dhcp_type == DHCPACK && requesting) {
		// TODO: when receiving DHCPACK, we /should/ send ARP request per rfc
		// 2131 4.4.1 and verify that the address is not used yet. If it is
		// used, send DHCPDECLINE and try a DHCPOFFER with another address. If
		// it is unused, broadcast an ARP reply with new IP address.
		// TODO: we should also take the timer information from the
		// offer/request/ack and ensure that we request a new lease as
		// soon as the timer expires.
		std::string ip(inet_ntop(AF_INET, yiaddr, ip_display, sizeof(ip_display)));
		dprintf(stdout, "  DHCP request was accepted! Assigning IP address %s\n", ip.c_str());
		requesting = false;

		add_v4addr(ip);
		dump_interfaces();
		return;
	} else if(dhcp_type == DHCPACK) {
		dprintf(stdout, "  ...But I wasn't waiting for an ACK at the moment, so dropping.\n");
		return;
	}

	// unreachable
	return;
}

void slurp_rawsocket() {
	assert(rawsock >= 0);
	while(1) {
		cloudabi_subscription_t subscriptions[2];
		size_t nevents = 0;
		// TODO: use a non-blocking recvmsg() instead of polling with
		// a timeout
		subscriptions[nevents++] = cloudabi_subscription_t{
			.type = CLOUDABI_EVENTTYPE_CLOCK,
			.clock.clock_id = CLOUDABI_CLOCK_MONOTONIC,
			.clock.timeout = 1000,
			//.clock.timeout = monotime_after(1),
			//.clock.flags = CLOUDABI_SUBSCRIPTION_CLOCK_ABSTIME,
		};
		subscriptions[nevents++] = cloudabi_subscription_t{
			.type = CLOUDABI_EVENTTYPE_FD_READ,
			.fd_readwrite.fd = static_cast<cloudabi_fd_t>(rawsock),
			.fd_readwrite.flags = CLOUDABI_SUBSCRIPTION_FD_READWRITE_POLL,
		};
		cloudabi_event_t events[2];
		cloudabi_errno_t error = cloudabi_sys_poll(subscriptions, events, nevents, &nevents);
		if(error != 0) {
			dprintf(stdout, "poll() failed: %s\n", strerror(error));
			abort();
		}
		if(nevents == 0 || (nevents == 1 && events[0].type == CLOUDABI_EVENTTYPE_CLOCK)) {
			return;
		}

		// TODO: read mtu, don't assume
		uint8_t frame[1500];
		struct iovec iov = {.iov_base = frame, .iov_len = sizeof(frame)};
		struct msghdr msg = {
			.msg_iov = &iov, .msg_iovlen = 1,
		};
		ssize_t size = recvmsg(rawsock, &msg, 0);
		if(size < 0) {
			perror("Failed to read from rawsock");
			abort();
		}
		handle_rawsock_frame(frame, size);
	}
}

void program_main(const argdata_t *ad) {
	argdata_map_iterator_t it;
	const argdata_t *key;
	const argdata_t *value;
	argdata_map_iterate(ad, &it);
	while (argdata_map_next(&it, &key, &value)) {
		const char *keystr;
		if(argdata_get_str_c(key, &keystr) != 0) {
			continue;
		}

		if(strcmp(keystr, "stdout") == 0) {
			argdata_get_fd(value, &stdout);
		} else if(strcmp(keystr, "networkd") == 0) {
			argdata_get_fd(value, &networkd);
		} else if(strcmp(keystr, "interface") == 0) {
			const char *ifstr;
			size_t iflen;
			argdata_get_str(value, &ifstr, &iflen);
			interface.assign(ifstr, iflen);
		}
	}

	rawsock = -1;

	dprintf(stdout, "dhclient started for interface %s\n", interface.c_str());
	FILE *out = fdopen(stdout, "w");
	fswap(stderr, out);

	std::string hwtype = get_hwtype();
	if(hwtype != "ETHERNET") {
		dprintf(stdout, "Unsupported hardware type %s\n", hwtype.c_str());
		exit(0);
	}

	dprintf(stdout, "MAC: %s\n", get_mac().c_str());
	dprintf(stdout, "HW type: %s\n", get_hwtype().c_str());
	dprintf(stdout, "Number of v4 addrs: %d\n", get_v4addr().size());

	while(get_hwtype() != "ERROR") {
		// TODO: Check if lease is expiring
		//   If so, ask for an extension

		// TODO: Check if lease expired
		//   If so, drop IP

		// Check if the interface has no IP; if so, start DHCP discovery
		if(get_v4addr().empty()) {
			if(rawsock < 0) {
				rawsock = get_rawsock();
				if(rawsock < 0) {
					dprintf(stdout, "Failed to obtain raw socket\n");
					exit(1);
				}
			}

			cloudabi_timestamp_t ts = monotime();
			if((!discovering && !requesting) || ts >= next_discover) {
				perform_discover();
			}
		}

		cloudabi_subscription_t subscriptions[2];
		cloudabi_timestamp_t sleep_until;
		if(discovering) {
			sleep_until = next_discover;
		} else if(requesting) {
			sleep_until = monotime_after(10);
		} else {
			sleep_until = monotime_after(60);
		}
		size_t nevents = 0;
		subscriptions[nevents++] = cloudabi_subscription_t{
			.type = CLOUDABI_EVENTTYPE_CLOCK,
			.clock.clock_id = CLOUDABI_CLOCK_MONOTONIC,
			.clock.timeout = sleep_until,
			.clock.flags = CLOUDABI_SUBSCRIPTION_CLOCK_ABSTIME,
		};
		if(rawsock) {
			subscriptions[nevents++] = cloudabi_subscription_t{
				.userdata = static_cast<cloudabi_fd_t>(rawsock),
				.type = CLOUDABI_EVENTTYPE_FD_READ,
				.fd_readwrite.fd = static_cast<cloudabi_fd_t>(rawsock),
				.fd_readwrite.flags = CLOUDABI_SUBSCRIPTION_FD_READWRITE_POLL,
			};
		}
		assert(sizeof(subscriptions) / sizeof(subscriptions[0]) >= nevents);
		cloudabi_event_t events[nevents];
		cloudabi_errno_t error = cloudabi_sys_poll(subscriptions, events, nevents, &nevents);
		if(error != 0) {
			dprintf(stdout, "poll() failed: %s\n", strerror(error));
			abort();
		}

		for(size_t i = 0; i < nevents; ++i) {
			const cloudabi_event_t *event = &events[i];
			if(event->type == CLOUDABI_EVENTTYPE_FD_READ && event->userdata == static_cast<cloudabi_fd_t>(rawsock)) {
				if(event->error != 0) {
					dprintf(stdout, "poll() event failed: %s\n", strerror(errno));
					abort();
				}
				slurp_rawsocket();
			}
		}
	}

	dprintf(stdout, "[dhclient] Interface %s seems gone, exiting", interface.c_str());
	exit(0);
}
