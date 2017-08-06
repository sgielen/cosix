#include "ip_socket.hpp"
#include "util.hpp"
#include <thread>
#include <cassert>

using namespace networkd;

ip_socket::ip_socket(transport_proto p, std::string l_ip, uint16_t l_port, std::string p_ip, uint16_t p_port, cosix::pseudofd_t ps, int f)
: proto(p)
, local_ip(l_ip)
, local_port(l_port)
, peer_ip(p_ip)
, peer_port(p_port)
, pseudofd(ps)
, reversefd(f)
{
}

ip_socket::~ip_socket()
{
}

void ip_socket::start()
{
	// only root pseudo FD's may start a listening thread, otherwise multiple
	// threads are listening on a single reverse FD
	assert(pseudofd == 0);

	auto that = shared_from_this();
	std::thread thr([that](){
		that->run();
	});
	thr.detach();
}

void ip_socket::run()
{
	reverse_thread = std::this_thread::get_id();
	const cloudabi_timestamp_t max_offset_from_now = 60ull * 1000 * 1000 * 1000; /* 60 seconds */
	(void)max_offset_from_now;
	try {
		while(true) {
			assert(reverse_thread == std::this_thread::get_id());
			auto next_ts = next_timeout();
			assert(next_ts > 0);
#ifndef NDEBUG
			auto now = monotime();
			assert(now < max_offset_from_now || (now - max_offset_from_now) < next_ts);
#endif
			auto res = cosix::wait_for_request(reversefd, next_ts);
			if(res != 0 && res != EAGAIN) {
				throw std::runtime_error("wait_for_request failed: " + std::string(strerror(res)));
			}
			if(res != EAGAIN) {
				std::lock_guard<std::mutex> lock(reverse_mtx);
				res = handle_request(reversefd, this, next_ts);
				if(res != 0) {
					throw std::runtime_error("handle_request failed: " + std::string(strerror(res)));
				}
			}
			if(monotime() > next_timeout()) {
				timed_out();
			}
		}
	} catch(std::exception &e) {
		dprintf(0, "*** handle_requests threw an exception in ip_socket\n");
		dprintf(0, "*** error: \"%s\"\n", e.what());
		dprintf(0, "*** proto: %d\n", proto);
		std::string ipd = ipv4_ntop(local_ip);
		dprintf(0, "*** local bind: %s:%d\n", ipd.c_str(), local_port);
		ipd = ipv4_ntop(peer_ip);
		dprintf(0, "*** remote bind: %s:%d\n", ipd.c_str(), peer_port);
		dprintf(0, "*** pseudo: %llu / reverse: %d\n", pseudofd, reversefd);
	}
}

void ip_socket::stat_fget(cosix::pseudofd_t, cloudabi_filestat_t *buf)
{
	buf->st_dev = 0;
	buf->st_ino = 0;
	buf->st_filetype = proto == transport_proto::udp ? CLOUDABI_FILETYPE_SOCKET_DGRAM : CLOUDABI_FILETYPE_SOCKET_STREAM;
	buf->st_nlink = 0;
	buf->st_size = 0;
	buf->st_atim = 0;
	buf->st_mtim = 0;
	buf->st_ctim = 0;
}

void ip_socket::becomes_readable()
{
	if(reverse_thread != std::this_thread::get_id()) {
		// since we're not the thread handling requests on the reverse fd,
		// that thread may be sending responses currently, so wait until
		// we get a lock before we send this gratituous message
		std::lock_guard<std::mutex> lock(reverse_mtx);
		cosix::pseudo_fd_becomes_readable(reversefd, pseudofd);
	} else {
		// we are the thread handling requests, so we're not currently
		// sending a response, we already have the lock and can send
		// immediately
		cosix::pseudo_fd_becomes_readable(reversefd, pseudofd);
	}
}
