#include <blockdev/blockdev_store.hpp>
#include <blockdev/blockdev.hpp>
#include <blockdev/partition.hpp>
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

	Blk arg_alloc = allocate(strlen(arg) + 1);
	memcpy(arg_alloc.ptr, arg, arg_alloc.size);
	auto *bdevname = reinterpret_cast<char*>(arg_alloc.ptr);
	auto *arg1 = strsplit(bdevname, ' ');

	auto bdev = get_blockdev_store()->get_blockdev(bdevname);
	if(!bdev) {
		set_response("NODEV");
		deallocate(arg_alloc);
		return;
	}

	if(strcmp(command, "FD") == 0) {
		auto process = get_scheduler()->get_running_thread()->get_process();
		int fd = process->add_fd(bdev, -1, -1);

		set_response("OK");
		add_fd_to_response(fd);
		deallocate(arg_alloc);
		return;
	} else if(strcmp(command, "PARTITION") == 0) {
		auto *lba_offset_str = arg1;
		auto *sector_count_str = strsplit(lba_offset_str, ' ');

		if(sector_count_str == nullptr) {
			// strsplit returns nullptr if its input str is nullptr, so this
			// means any of the arguments is missing
			set_response("ERROR");
			deallocate(arg_alloc);
			return;
		}

		int64_t lba_offset, sector_count;
		if(!atoi64_s(lba_offset_str, &lba_offset, 10)
		|| !atoi64_s(sector_count_str, &sector_count, 10)) {
			set_response("ERROR");
			deallocate(arg_alloc);
			return;
		}

		// TODO: check if lba_offset + sectorcount even falls within this bdev
		char name[64];
		size_t namelen = strlen(bdev->name);
		assert(namelen <= 62);
		memcpy(name, bdev->name, namelen);
		name[namelen] = 'p';
		name[namelen+1] = 0;

		auto part = make_shared<partition>(bdev, lba_offset, sector_count);

		if(get_blockdev_store()->register_blockdev(part, name) != 0) {
			set_response("ERROR");
			deallocate(arg_alloc);
			return;
		}

		auto process = get_scheduler()->get_running_thread()->get_process();
		int fd = process->add_fd(part, -1, -1);

		set_response(part->name);
		add_fd_to_response(fd);
		deallocate(arg_alloc);
		return;
	}

	deallocate(arg_alloc);
	set_response("ERROR");
}
