#include "ip_socket.hpp"

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
