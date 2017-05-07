#include "ip_socket.hpp"
#include <thread>

using namespace networkd;

ip_socket::ip_socket(transport_proto p, std::string l_ip, uint16_t l_port, std::string p_ip, uint16_t p_port, int f)
: proto(p)
, local_ip(l_ip)
, local_port(l_port)
, peer_ip(p_ip)
, peer_port(p_port)
, fd(f)
{
}

ip_socket::~ip_socket()
{
}

void ip_socket::start()
{
	auto that = shared_from_this();
	std::thread thr([that](){
		that->run();
	});
	thr.detach();
}

void ip_socket::run()
{
	handle_requests(fd, this);
}
