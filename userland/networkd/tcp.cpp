#include "tcp.hpp"
#include "ip.hpp"
#include "ip_socket.hpp"
#include "tcp_socket.hpp"
#include "networkd.hpp"
#include "routing_table.hpp"
#include <cassert>

using namespace networkd;

struct tcp_ipv4_pseudo_hdr {
	uint32_t source_ip;
	uint32_t dest_ip;
	uint8_t reserved;
	uint8_t protocol;
	uint16_t length;
};

tcp::tcp() {}

void tcp::handle_packet(std::shared_ptr<interface> iface, const char *frame, size_t framelen, size_t ip_offset, size_t tcp_offset, size_t tcp_length)
{
	if(tcp_length < sizeof(tcp_header)) {
		// TCP header doesn't fit
		return;
	}

	assert(ip_offset < tcp_offset);

	auto *ip_hdr = reinterpret_cast<const ip_header*>(frame + ip_offset);
	auto *tcp_hdr = reinterpret_cast<const tcp_header*>(frame + tcp_offset);

	uint16_t header_size = tcp_hdr->data_off * 4;
	if(header_size < sizeof(tcp_header)) {
		// TCP header including options don't fit
		return;
	}

	// Look for an existing TCP socket with this source_ip/source_port/dest_ip/dest_port combination

	tcp_connection thiscon;
	thiscon.peer_ip = std::string(reinterpret_cast<const char*>(&ip_hdr->source_ip), 4);
	thiscon.local_ip = std::string(reinterpret_cast<const char*>(&ip_hdr->dest_ip), 4);
	thiscon.peer_port = ntohs(tcp_hdr->source_port);
	thiscon.local_port = ntohs(tcp_hdr->dest_port);
	uint32_t seqnum = ntohl(tcp_hdr->seqnum);

	std::unique_lock<std::mutex> lock(sockets_mtx);
	std::shared_ptr<tcp_socket> recv_socket;
	{
		auto it = sockets.find(thiscon);
		if(it != sockets.end()) {
			recv_socket = it->second;
		}
	}

	if(recv_socket) {
		lock.unlock();
		recv_socket->handle_packet(iface, frame, framelen, ip_offset, tcp_offset, tcp_length);
	} else if(!recv_socket && tcp_hdr->flag_syn && !tcp_hdr->flag_ack && !tcp_hdr->flag_rst && !tcp_hdr->flag_fin) {
		// somebody's trying to create a connection with us

		// first, try to do a List() to see if anybody would be interested in this connection
		// do this while holding the sockets mutex, so that additional SYNs won't start creating
		// multiple sockets
		using namespace flower::protocol::switchboard;

		arpc::ClientContext list_context;
		ListRequest list_request;
		auto out_labels = list_request.mutable_out_labels();
		(*out_labels)["scope"] = "network";
		(*out_labels)["protocol"] = "tcp";
		(*out_labels)["peer_ip"] = ipv4_ntop(thiscon.peer_ip);
		(*out_labels)["peer_port"] = std::to_string(thiscon.peer_port);
		(*out_labels)["local_ip"] = ipv4_ntop(thiscon.local_ip);
		(*out_labels)["local_port"] = std::to_string(thiscon.local_port);
		// TODO: allow limiting on destination interface
		// TODO: allow connecting to a wildcard or broadcast IP address?

		ListResponse list_response;
		auto reader = get_switchboard_stub()->List(&list_context, list_request);
		if(!reader->Read(&list_response)) {
			// nobody is listening on this IP/port, drop it
			send_rst(thiscon, seqnum + 1);
			return;
		}

		auto rev_pseu = open_pseudo(CLOUDABI_FILETYPE_SOCKET_STREAM);
		if(rev_pseu.first <= 0 || rev_pseu.second <= 0) {
			return;
		}

		auto pseudo = std::make_shared<arpc::FileDescriptor>(rev_pseu.second);
		// rev_pseu.second is now owned by the FileDescriptor

		recv_socket = std::make_shared<tcp_socket>(thiscon.local_ip, thiscon.local_port, thiscon.peer_ip, thiscon.peer_port, rev_pseu.first);
		auto res = locked_register_socket(recv_socket);
		if(res != 0) {
			// error
			return;
		}
		lock.unlock();

		recv_socket->handle_packet(iface, frame, framelen, ip_offset, tcp_offset, tcp_length);
		recv_socket->start();

		// now, actually ingress the pseudo FD
		arpc::ClientContext context;
		IngressConnectRequest request;
		request.set_client(pseudo);
		*request.mutable_out_labels() = *out_labels;

		IngressConnectResponse response;
		if (arpc::Status status = get_switchboard_stub()->IngressConnect(&context, request, &response); !status.ok()) {
			// apparently, the server went away, so close the connection again
			send_rst(thiscon, seqnum + 1);
			close(rev_pseu.first);
		}
	} else if(tcp_hdr->flag_rst) {
		// somebody's trying to break up a connection with us, but we don't know about it, drop it
	} else {
		send_rst(thiscon, seqnum + 1);
	}
}

cloudabi_errno_t tcp::register_socket(std::shared_ptr<tcp_socket> socket)
{
	std::lock_guard<std::mutex> lock(sockets_mtx);
	return locked_register_socket(socket);
}

cloudabi_errno_t tcp::locked_register_socket(std::shared_ptr<tcp_socket> socket)
{
	tcp_connection thisconn;
	thisconn.peer_ip = socket->get_peer_ip();
	thisconn.peer_port = socket->get_peer_port();
	thisconn.local_ip = socket->get_local_ip();
	thisconn.local_port = socket->get_local_port();

	// TODO: check if any iface even _has_ this local IP?
	// TODO: check if this socket doesn't conflict with any other already-listening
	// sockets, e.g. two exclusive UDP sockets?

	if(sockets.find(thisconn) != sockets.end()) {
		return EADDRINUSE;
	}

	sockets[thisconn] = socket;
	return 0;
}

bool tcp_connection::operator<(tcp_connection const &o) const {
	if(peer_port != o.peer_port) {
		return peer_port < o.peer_port;
	}
	if(local_port != o.local_port) {
		return local_port < o.local_port;
	}
	if(peer_ip != o.peer_ip) {
		return peer_ip < o.peer_ip;
	}
	return local_ip < o.local_ip;
}

void tcp::send_rst(tcp_connection const &conn, uint32_t acknum)
{
	struct tcp_header tcp_hdr;
	memset(&tcp_hdr, 0, sizeof(tcp_header));
	tcp_hdr.source_port = htons(conn.local_port);
	tcp_hdr.dest_port = htons(conn.peer_port);
	tcp_hdr.seqnum = 0;
	tcp_hdr.acknum = htonl(acknum);
	tcp_hdr.flag_syn = 0;
	tcp_hdr.flag_ack = 1;
	tcp_hdr.flag_fin = 0;
	tcp_hdr.flag_rst = 1;
	tcp_hdr.flag_psh = 0;
	tcp_hdr.window = 0;
	tcp_hdr.data_off = sizeof(tcp_hdr) / 4;

	compute_tcp_checksum(tcp_hdr, conn.local_ip, conn.peer_ip, std::string(), 0);

	struct ip_header ip_hdr;
	memset(&ip_hdr, 0, sizeof(ip_hdr));
	ip_hdr.ihl = 5;
	ip_hdr.version = 4;
	ip_hdr.total_len = htons(sizeof(ip_hdr) + sizeof(tcp_hdr));
	arc4random_buf(&ip_hdr.ident, sizeof(ip_hdr.ident));
	ip_hdr.ttl = 0xff;
	ip_hdr.proto = transport_proto::tcp;
	ip_hdr.source_ip = *reinterpret_cast<uint32_t const*>(conn.local_ip.c_str());
	ip_hdr.dest_ip = *reinterpret_cast<uint32_t const*>(conn.peer_ip.c_str());

	compute_ip_checksum(ip_hdr);

	cloudabi_errno_t res;
	try {
		std::vector<iovec> vecs(2);
		vecs[0].iov_base = const_cast<void*>(reinterpret_cast<const void*>(&ip_hdr));
		vecs[0].iov_len = sizeof(ip_hdr);
		vecs[1].iov_base = const_cast<void*>(reinterpret_cast<const void*>(&tcp_hdr));
		vecs[1].iov_len = sizeof(tcp_hdr);

		res = get_ip().send_packet(vecs, conn.peer_ip);
	} catch(std::exception &e) {
		fprintf(stderr, "Failed to send TCP RST: %s\n", e.what());
		res = ECANCELED;
	}
	if(res != 0) {
		fprintf(stderr, "Failed to send TCP RST: %s\n", strerror(errno));
	}
}

void networkd::compute_tcp_checksum(tcp_header &tcp_hdr, std::string local_ip, std::string peer_ip, std::string const &data, size_t segment_size) {
	uint32_t source_ip = *reinterpret_cast<uint32_t const*>(local_ip.c_str());
	uint32_t dest_ip = *reinterpret_cast<uint32_t const*>(peer_ip.c_str());

	struct tcp_ipv4_pseudo_hdr pseudo_hdr;
	memset(&pseudo_hdr, 0, sizeof(pseudo_hdr));
	pseudo_hdr.source_ip = source_ip;
	pseudo_hdr.dest_ip = dest_ip;
	pseudo_hdr.protocol = transport_proto::tcp;
	pseudo_hdr.length = htons(sizeof(tcp_hdr) + segment_size);
	uint16_t *pseudo_hdr_16 = reinterpret_cast<uint16_t*>(&pseudo_hdr);
	uint32_t short_sum = 0;
	for(size_t i = 0; i < sizeof(pseudo_hdr) / 2; ++i) {
		short_sum += pseudo_hdr_16[i];
	}
	uint16_t *tcp_hdr_16 = reinterpret_cast<uint16_t*>(&tcp_hdr);
	for(size_t i = 0; i < sizeof(tcp_hdr) / 2; ++i) {
		short_sum += tcp_hdr_16[i];
	}
	size_t loops = segment_size / 2 + segment_size % 2;
	for(size_t i = 0; i < loops; ++i) {
		if(i * 2 == segment_size - 1) {
			short_sum += uint16_t(data[segment_size-1]);
		} else {
			short_sum += *reinterpret_cast<uint16_t const*>(data.c_str() + i * 2);
		}
	}
	short_sum = (short_sum & 0xffff) + (short_sum >> 16);
	short_sum = (short_sum & 0xffff) + (short_sum >> 16);
	tcp_hdr.checksum = ~short_sum;
}
