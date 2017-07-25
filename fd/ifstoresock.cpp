#include <fd/ifstoresock.hpp>
#include <fd/scheduler.hpp>
#include <oslibc/string.h>
#include <oslibc/numeric.h>
#include <net/interface_store.hpp>
#include <net/interface.hpp>
#include <fd/unixsock.hpp>
#include <fd/pseudo_fd.hpp>
#include <fd/rawsock.hpp>

using namespace cloudos;

ifstoresock::ifstoresock(const char *n)
: sock_t(CLOUDABI_FILETYPE_SOCKET_DGRAM, n)
{
	status = sockstatus_t::CONNECTED;
}

ifstoresock::~ifstoresock()
{
}

void ifstoresock::sock_shutdown(cloudabi_sdflags_t how)
{
	// ignore CLOUDABI_SHUT_RD
	if(how & CLOUDABI_SHUT_WR) {
		status = sockstatus_t::SHUTDOWN;
	}
	error = 0;
}

void ifstoresock::sock_stat_get(cloudabi_sockstat_t* buf, cloudabi_ssflags_t flags)
{
	assert(buf);
	buf->ss_error = error;
	buf->ss_state = 0;

	if(flags & CLOUDABI_SOCKSTAT_CLEAR_ERROR) {
		error = 0;
	}
}

void ifstoresock::sock_recv(const cloudabi_recv_in_t* in, cloudabi_recv_out_t *out)
{
	while(!has_message) {
		read_cv.wait();
	}

	// TODO: a generic function to copy iovecs/linked-list-of-buffers over
	// iovecs
	char *buffer = reinterpret_cast<char*>(message_buf.ptr);
	size_t datalen = 0;
	size_t buffer_size_remaining = message_buf.size;
	for(size_t i = 0; i < in->ri_data_len; ++i) {
		auto &iovec = in->ri_data[i];
		if(iovec.buf_len < buffer_size_remaining) {
			memcpy(iovec.buf, buffer, iovec.buf_len);
			datalen += iovec.buf_len;
			buffer += iovec.buf_len;
			buffer_size_remaining -= iovec.buf_len;
		} else {
			memcpy(iovec.buf, buffer, buffer_size_remaining);
			datalen += buffer_size_remaining;
			buffer_size_remaining = 0;
			break;
		}
	}

	if(buffer_size_remaining > 0) {
		// TODO: message is truncated
	}

	size_t fds_set = 0;
	if(message_fds != nullptr) {
		auto process = get_scheduler()->get_running_thread()->get_process();
		while(message_fds != nullptr) {
			if(fds_set < in->ri_fds_len) {
				fd_mapping_t &fd_map = message_fds->data;
				in->ri_fds[fds_set] = process->add_fd(fd_map.fd, fd_map.rights_base, fd_map.rights_inheriting);
				fds_set++;
			}
			auto d = message_fds;
			message_fds = message_fds->next;
			deallocate(d);
		}
	}

	// TODO: what if fds are truncated? move to next message?
	out->ro_datalen = datalen;
	out->ro_fdslen = fds_set;
	out->ro_flags = 0;

	deallocate(message_buf);
	has_message = false;
	write_cv.notify();
	error = 0;
}

void ifstoresock::sock_send(const cloudabi_send_in_t* in, cloudabi_send_out_t *out)
{
	if(status == sockstatus_t::SHUTDOWN) {
		error = EPIPE;
		return;
	}
	assert(status == sockstatus_t::CONNECTED);

	while(has_message) {
		write_cv.wait();
	}

	char message[80];

	size_t read = 0;
	for(size_t i = 0; i < in->si_data_len; ++i) {
		size_t remaining = sizeof(message) - read - 1 /* terminator */;
		size_t buf_len = in->si_data[i].buf_len;
		if(buf_len > remaining) {
			error = EMSGSIZE;
			return;
		}
		memcpy(message + read, in->si_data[i].buf, buf_len);
		read += buf_len;
	}

	assert(read < sizeof(message));
	out->so_datalen = read;
	message[read] = 0;

	char *command = message;
	char *arg = strsplit(command, ' ');

	char response[160];
	response[0] = 0;

	interface *iface = nullptr;

	// Commands without mandatory arguments
	if(strcmp(command, "LIST") == 0) {
		// List all interfaces
		auto *ifaces = get_interface_store()->get_interfaces();
		iterate(ifaces, [&](interface_list *item) {
			strlcat(response, item->data->get_name(), sizeof(response));
			strlcat(response, "\n", sizeof(response));
		});
		goto send;
	} else if(strcmp(command, "PSEUDOPAIR") == 0) {
		// Request a reverse/pseudo socketpair
		auto my_reverse = make_shared<unixsock>(CLOUDABI_FILETYPE_SOCKET_STREAM, "my_reverse");
		auto their_reverse = make_shared<unixsock>(CLOUDABI_FILETYPE_SOCKET_STREAM, "their_reverse");
		my_reverse->socketpair(their_reverse);
		assert(my_reverse->error == 0);
		assert(their_reverse->error == 0);

		cloudabi_rights_t all_rights = -1;
		auto process = get_scheduler()->get_running_thread()->get_process();
		int reverse_fd = process->add_fd(their_reverse, all_rights, all_rights);

		cloudabi_filetype_t f = CLOUDABI_FILETYPE_DIRECTORY;
		if(arg) {
			if(strcmp(arg, "REGULAR_FILE") == 0) {
				f = CLOUDABI_FILETYPE_REGULAR_FILE;
			} else if(strcmp(arg, "SOCKET_STREAM") == 0) {
				f = CLOUDABI_FILETYPE_SOCKET_STREAM;
			} else if(strcmp(arg, "SOCKET_DGRAM") == 0) {
				f = CLOUDABI_FILETYPE_SOCKET_DGRAM;
			} else if(strcmp(arg, "DIRECTORY") == 0) {
				f = CLOUDABI_FILETYPE_DIRECTORY;
			} else {
				strncpy(response, "ERROR", sizeof(response));
				goto send;
			}
		}
		auto pseudo = make_shared<pseudo_fd>(0, my_reverse, f, "pseudo");
		int pseudo_fd = process->add_fd(pseudo, all_rights, all_rights);

		fd_mapping_t *fd_mapping;
		error = process->get_fd(&fd_mapping, reverse_fd, 0);
		if(error != 0) {
			strncpy(response, "ERROR", sizeof(response));
			goto send;
		}
		assert(message_fds == nullptr);
		message_fds = allocate<linked_list<fd_mapping_t>>(*fd_mapping);
		error = process->get_fd(&fd_mapping, pseudo_fd, 0);
		if(error != 0) {
			strncpy(response, "ERROR", sizeof(response));
			goto send;
		}
		message_fds->next = allocate<linked_list<fd_mapping_t>>(*fd_mapping);

		strncpy(response, "OK", sizeof(response));
		goto send;
	} else if(strcmp(command, "COPY") == 0) {
		// Return a new socket to myself
		auto process = get_scheduler()->get_running_thread()->get_process();
		auto ifstore = make_shared<ifstoresock>("ifstoresock");
		auto ifstorefd = process->add_fd(ifstore, -1, -1);

		fd_mapping_t *fd_mapping;
		error = process->get_fd(&fd_mapping, ifstorefd, 0);
		if(error != 0) {
			strncpy(response, "ERROR", sizeof(response));
			goto send;
		}
		assert(message_fds == nullptr);
		message_fds = allocate<linked_list<fd_mapping_t>>(*fd_mapping);

		strncpy(response, "OK", sizeof(response));
		goto send;
	}

	// Commands with interface as arg
	if(arg[0] == 0) {
		strncpy(response, "ERROR", sizeof(response));
		goto send;
	}

	iface = get_interface_store()->get_interface(arg);

	if(iface == nullptr) {
		strncpy(response, "NOIFACE", sizeof(response));
		goto send;
	}

	if(strcmp(command, "MAC") == 0) {
		// Return MAC address of this interface
		size_t mac_size = 0;
		auto const *mac = iface->get_mac(&mac_size);
		for(uint8_t i = 0; i < mac_size; ++i) {
			if(i > 0) {
				strlcat(response, ":", sizeof(response));
			}
			if(mac[i] < 0x10) {
				strlcat(response, "0", sizeof(response));
			}
			char number[4];
			strlcat(response, uitoa_s(mac[i], number, sizeof(number), 16), sizeof(response));
		}
		if(mac_size == 0) {
			// device has no MAC, return a fake one
			strlcat(response, "00:00:00:00:00:00", sizeof(response));
		}
		goto send;
	} else if(strcmp(command, "HWTYPE") == 0) {
		// Return interface type of this interface
		auto hwtype = iface->get_hwtype();
		switch(hwtype) {
		case interface::hwtype_t::loopback:
			strncpy(response, "LOOPBACK", sizeof(response));
			goto send;
		case interface::hwtype_t::ethernet:
			strncpy(response, "ETHERNET", sizeof(response));
			goto send;
		}
		strncpy(response, "UNKNOWN", sizeof(response));
		goto send;
	} else if(strcmp(command, "RAWSOCK") == 0) {
		auto process = get_scheduler()->get_running_thread()->get_process();
		auto sock = make_shared<rawsock>(iface, "rawsock to ");
		strlcat(sock->name, iface->get_name(), sizeof(sock->name));
		auto rawsockfd = process->add_fd(sock, -1, -1);
		sock->init();

		fd_mapping_t *fd_mapping;
		error = process->get_fd(&fd_mapping, rawsockfd, 0);
		if(error != 0) {
			strncpy(response, "ERROR", sizeof(response));
			goto send;
		}
		assert(message_fds == nullptr);
		message_fds = allocate<linked_list<fd_mapping_t>>(*fd_mapping);

		strncpy(response, "OK", sizeof(response));
		goto send;
	}

	strncpy(response, "ERROR", sizeof(response));

send:
	if(strlen(response) == sizeof(response) - 1) {
		// full response may not have fit, error
		// TODO: this could happen if we have too many interfaces or
		// too many addresses on an interface
		strncpy(response, "EMSGSIZE\n", sizeof(response));
	}
	message_buf = allocate(strlen(response));
	memcpy(message_buf.ptr, response, message_buf.size);
	has_message = true;
	error = 0;
	read_cv.notify();
}
