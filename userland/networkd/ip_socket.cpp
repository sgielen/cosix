#include "ip_socket.hpp"
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
	handle_requests(reversefd, this);
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
