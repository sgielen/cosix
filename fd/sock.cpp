#include <fd/sock.hpp>

using namespace cloudos;

sock_t::sock_t(cloudabi_filetype_t sockettype, const char *n)
: fd_t(sockettype, n)
{
	assert(sockettype == CLOUDABI_FILETYPE_SOCKET_DGRAM
	    || sockettype == CLOUDABI_FILETYPE_SOCKET_STREAM);
}

size_t sock_t::read(void *dest, size_t count)
{
	cloudabi_iovec_t iovec[1];
	iovec[0].buf = dest;
	iovec[0].buf_len = count;

	cloudabi_recv_in_t recv_in[1];
	recv_in[0].ri_data = &iovec[0];
	recv_in[0].ri_data_len = 1;
	recv_in[0].ri_fds = 0;
	recv_in[0].ri_fds_len = 0;
	recv_in[0].ri_flags = 0;

	cloudabi_recv_out_t recv_out[1];
	recv_out[0].ro_datalen = 0;

	sock_recv(recv_in, recv_out);
	assert(recv_out[0].ro_fdslen == 0);
	return recv_out[0].ro_datalen;
}

void sock_t::putstring(const char *str, size_t count)
{
	cloudabi_ciovec_t iovec[1];
	iovec[0].buf = str;
	iovec[0].buf_len = count;

	cloudabi_send_in_t send_in[1];
	send_in[0].si_data = &iovec[0];
	send_in[0].si_data_len = 1;
	send_in[0].si_fds = 0;
	send_in[0].si_fds_len = 0;
	send_in[0].si_flags = 0;

	cloudabi_send_out_t send_out[1];
	send_out[0].so_datalen = 0;

	sock_send(send_in, send_out);
	/* TODO: send_out[0].so_datalen is amount of bytes written */
}
