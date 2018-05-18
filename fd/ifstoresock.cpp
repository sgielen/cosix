#include <fd/ifstoresock.hpp>
#include <fd/pseudo_fd.hpp>
#include <fd/rawsock.hpp>
#include <fd/reverse_fd.hpp>
#include <fd/scheduler.hpp>
#include <fd/unixsock.hpp>
#include <net/interface.hpp>
#include <net/interface_store.hpp>
#include <oslibc/numeric.h>
#include <oslibc/string.h>
#include <oslibc/iovec.hpp>

using namespace cloudos;

ifstoresock::ifstoresock(const char *n)
: userlandsock(n)
{}

void ifstoresock::handle_command(const char *command, const char *arg)
{
	// Commands without mandatory arguments
	if(strcmp(command, "LIST") == 0) {
		// List all interfaces
		size_t response_size = 0;
		auto *ifaces = get_interface_store()->get_interfaces();
		iterate(ifaces, [&](interface_list *item) {
			response_size += strlen(item->data->get_name()) + 1;
		});

		Blk response = allocate(response_size);
		char *resp = reinterpret_cast<char*>(response.ptr);
		resp[0] = 0;

		iterate(ifaces, [&](interface_list *item) {
			strlcat(resp, item->data->get_name(), sizeof(response));
			strlcat(resp, "\n", sizeof(response));
		});
		set_response(resp);
		deallocate(response);
		return;
	} else if(strcmp(command, "PSEUDOPAIR") == 0) {
		// Request a reverse/pseudo socketpair
		auto my_reverse = make_shared<reversefd_t>(CLOUDABI_FILETYPE_SOCKET_STREAM, 0, "reversefd_t");
		auto their_reverse = make_shared<unixsock>(CLOUDABI_FILETYPE_SOCKET_STREAM, 0, "reverse_unixsock");
		my_reverse->socketpair(their_reverse);
		assert(my_reverse->error == 0);
		assert(their_reverse->error == 0);

		cloudabi_rights_t all_rights = -1;
		auto process = get_scheduler()->get_running_thread()->get_process();
		int reverse_fd = process->add_fd(their_reverse, all_rights, all_rights);

		int32_t filetype;
		if(!arg || !atoi_s(arg, &filetype, 10) || filetype < 0 || filetype > 0xff) {
			set_response("ERROR");
			return;
		}

		auto pseudo = make_shared<pseudo_fd>(0, my_reverse, filetype, 0, "pseudo");
		int pseudo_fd = process->add_fd(pseudo, all_rights, all_rights);

		set_response("OK");
		add_fd_to_response(reverse_fd);
		add_fd_to_response(pseudo_fd);
		return;
	} else if(strcmp(command, "COPY") == 0) {
		// Return a new socket to myself
		auto process = get_scheduler()->get_running_thread()->get_process();
		auto ifstore = make_shared<ifstoresock>("ifstoresock");
		int ifstorefd = process->add_fd(ifstore, -1, -1);

		set_response("OK");
		add_fd_to_response(ifstorefd);
		return;
	}

	// Commands with interface as arg
	if(arg[0] == 0) {
		set_response("ERROR");
		return;
	}

	interface *iface = get_interface_store()->get_interface(arg);

	if(iface == nullptr) {
		set_response("NOIFACE");
		return;
	}

	if(strcmp(command, "MAC") == 0) {
		// Return MAC address of this interface
		char response[18];
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
		set_response(response);
		return;
	} else if(strcmp(command, "HWTYPE") == 0) {
		// Return interface type of this interface
		auto hwtype = iface->get_hwtype();
		switch(hwtype) {
		case interface::hwtype_t::loopback:
			set_response("LOOPBACK");
			return;
		case interface::hwtype_t::ethernet:
			set_response("ETHERNET");
			return;
		}
		set_response("UNKNOWN");
		return;
	} else if(strcmp(command, "RAWSOCK") == 0) {
		auto process = get_scheduler()->get_running_thread()->get_process();
		auto sock = make_shared<rawsock>(iface, 0, "rawsock to ");
		strlcat(sock->name, iface->get_name(), sizeof(sock->name));
		int rawsockfd = process->add_fd(sock, -1, -1);
		sock->init();

		set_response("OK");
		add_fd_to_response(rawsockfd);
		return;
	}

	set_response("ERROR");
}
