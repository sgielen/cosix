#include <blockdev/blockdev_store.hpp>
#include <blockdev/blockdev.hpp>
#include <fd/blockdevstoresock.hpp>
#include <fd/pseudo_fd.hpp>
#include <fd/rawsock.hpp>
#include <fd/reverse_fd.hpp>
#include <fd/scheduler.hpp>
#include <fd/unixsock.hpp>
#include <oslibc/iovec.hpp>
#include <oslibc/numeric.h>
#include <oslibc/string.h>

using namespace cloudos;

blockdevstoresock::blockdevstoresock(const char *n)
: userlandsock(n)
{}

void blockdevstoresock::handle_command(const char *command, const char *arg)
{
	// Commands without mandatory arguments
	if(strcmp(command, "LIST") == 0) {
		// List all known block devices
		size_t response_size = 0;
		auto *devices = get_blockdev_store()->get_blockdevs();
		iterate(devices, [&](blockdev_list *item) {
			response_size += strlen(item->data->get_name()) + 1;
		});

		Blk response = allocate(response_size);
		char *resp = reinterpret_cast<char*>(response.ptr);
		resp[0] = 0;

		iterate(devices, [&](blockdev_list *item) {
			strlcat(resp, item->data->get_name(), response.size);
			strlcat(resp, "\n", response.size);
		});
		set_response(resp);
		deallocate(response);
		return;
	} else if(strcmp(command, "COPY") == 0) {
		// Return a new socket to myself
		auto process = get_scheduler()->get_running_thread()->get_process();
		auto sock = make_shared<blockdevstoresock>("blockdevstoresock");
		int fd = process->add_fd(sock, -1, -1);

		set_response("OK");
		add_fd_to_response(fd);
		return;
	}

	// Commands with block device as arg
	if(arg == nullptr || arg[0] == 0) {
		set_response("ERROR");
		return;
	}

	auto bdev = get_blockdev_store()->get_blockdev(arg);
	if(!bdev) {
		set_response("NODEV");
		return;
	}

	if(strcmp(command, "FD") == 0) {
		auto process = get_scheduler()->get_running_thread()->get_process();
		int fd = process->add_fd(bdev, -1, -1);

		set_response("OK");
		add_fd_to_response(fd);
		return;
	}
	set_response("ERROR");
}
