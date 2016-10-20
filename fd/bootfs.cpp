#include "bootfs.hpp"
#include "global.hpp"
#include <oslibc/numeric.h>
#include <memory/allocator.hpp>
#include "userland/external_binaries.h"

using namespace cloudos;

namespace cloudos {

struct bootfs_directory_fd : fd_t {
	bootfs_directory_fd(const char *n)
	: fd_t(CLOUDABI_FILETYPE_DIRECTORY, n) {}

	fd_t *openat(const char * /*path */, size_t /*pathlen*/, cloudabi_oflags_t /*oflags*/, const cloudabi_fdstat_t * /*fdstat*/) override;
};

struct bootfs_file_fd : fd_t {
	bootfs_file_fd(external_binary_t const &file, const char *n)
	: fd_t(CLOUDABI_FILETYPE_REGULAR_FILE, n)
	, addr(file.start)
	, length(file.end - file.start)
	{}

	size_t read(size_t offset, void *dest, size_t count) override;

private:
	uint8_t *addr;
	size_t length;
};

}

fd_t *bootfs_directory_fd::openat(const char *pathname, size_t pathlen, cloudabi_oflags_t, const cloudabi_fdstat_t *) {
	if(pathname == 0 || pathname[0] == 0 || pathname[0] == '/') {
		error = EINVAL;
		return nullptr;
	}

	char buf[pathlen + 1];
	memcpy(buf, pathname, pathlen);
	buf[pathlen] = 0;

	// TODO: check oflags and fdstat_t

	for(size_t i = 0; external_binaries_table[i].name; ++i) {
		if(strcmp(buf, external_binaries_table[i].name) == 0) {
			char name[64];
			strncpy(name, "bootfs/", sizeof(name));
			strncat(name, external_binaries_table[i].name, sizeof(name) - strlen(name) - 1);
			bootfs_file_fd *fd = get_allocator()->allocate<bootfs_file_fd>();
			new (fd) bootfs_file_fd(external_binaries_table[i], name);
			return fd;
		}
	}

	error = ENOENT;
	return nullptr;
}

size_t bootfs_file_fd::read(size_t offset, void *dest, size_t count) {
	error = 0;

	// TODO this is the same code for every file for which we
	// already have all contents in memory, so unify this
	if(offset + count > length) {
		return 0;
	}

	size_t bytes_left = length - offset;
	size_t copied = count < bytes_left ? count : bytes_left;
	memcpy(reinterpret_cast<char*>(dest), addr + offset, copied);
	return copied;
}

fd_t *bootfs::get_root_fd() {
	bootfs_directory_fd *fd = get_allocator()->allocate<bootfs_directory_fd>();
	new (fd) bootfs_directory_fd("bootfs_root");
	return fd;
}
