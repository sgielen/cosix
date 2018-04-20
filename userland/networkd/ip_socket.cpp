#include "ip_socket.hpp"
#include "util.hpp"
#include <thread>
#include <cassert>

using namespace networkd;

ip_socket::ip_socket(transport_proto p, std::string l_ip, uint16_t l_port, std::string p_ip, uint16_t p_port, int f)
: proto(p)
, local_ip(l_ip)
, local_port(l_port)
, peer_ip(p_ip)
, peer_port(p_port)
, running(false)
, reversefd(f)
{
}

ip_socket::~ip_socket()
{
}

void ip_socket::start()
{
	running = true;
	auto that = shared_from_this();
	std::thread thr([that](){
		that->run();
	});
	thr.detach();
}

void ip_socket::stop()
{
	running = false;
}

void ip_socket::run()
{
	reverse_thread = std::this_thread::get_id();
	const cloudabi_timestamp_t max_offset_from_now = 60ull * 1000 * 1000 * 1000; /* 60 seconds */
	(void)max_offset_from_now;
	try {
		while(running.load()) {
			assert(reverse_thread == std::this_thread::get_id());
			auto next_ts = next_timeout();
			assert(next_ts > 0);
#ifndef NDEBUG
			auto now = monotime();
			assert(now < max_offset_from_now || (now - max_offset_from_now) < next_ts);
#endif
			auto res = cosix::handle_request(reversefd, this, reverse_mtx, next_ts);
			if(!running.load()) {
				break;
			}
			if(res != 0 && res != EAGAIN /* timeout passed */) {
				throw cosix::cloudabi_system_error(res);
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
		dprintf(0, "*** reverse: %d\n", reversefd);
	}

	::close(reversefd);
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
		cosix::pseudo_fd_becomes_readable(reversefd, 0);
	} else {
		// we are the thread handling requests, so we're not currently
		// sending a response, we already have the lock and can send
		// immediately
		cosix::pseudo_fd_becomes_readable(reversefd, 0);
	}
}
