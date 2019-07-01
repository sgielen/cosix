#include "bootfs.hpp"
#include <fd/memory_fd.hpp>
#include "global.hpp"
#include <oslibc/numeric.h>
#include "userland/external_binaries.h"

using namespace cloudos;

namespace cloudos {

struct bootfs_directory_fd : fd_t {
	bootfs_directory_fd(const char *n)
	: fd_t(CLOUDABI_FILETYPE_DIRECTORY, 0, n) {}

	void lookup(const char *file, size_t filelen, cloudabi_oflags_t oflags, cloudabi_filestat_t *filestat) override;
	shared_ptr<fd_t> inode_open(cloudabi_device_t, cloudabi_inode_t, const cloudabi_fdstat_t *) override;
};

struct bootfs_file_fd : public memory_fd {
	bootfs_file_fd(external_binary_t const &file, const char *n)
	: memory_fd(file.start, file.end - file.start, n)
	{}
};

}

void bootfs_directory_fd::lookup(const char *file, size_t filelen, cloudabi_oflags_t, cloudabi_filestat_t *filestat) {
	filestat->st_dev = device;
	filestat->st_nlink = 1;
	filestat->st_atim = 0;
	filestat->st_mtim = 0;
	filestat->st_ctim = 0;

	if (strncmp(file, ".", filelen) == 0) {
		filestat->st_ino = 0;
		filestat->st_filetype = CLOUDABI_FILETYPE_DIRECTORY;
		filestat->st_size = 0;
		error = 0;
		return;
	}

	for(size_t i = 0; external_binaries_table[i].name; ++i) {
		auto &binary = external_binaries_table[i];
		if(strncmp(file, binary.name, filelen) == 0) {
			filestat->st_ino = i + 1;
			filestat->st_filetype = CLOUDABI_FILETYPE_REGULAR_FILE;
			filestat->st_size = binary.end - binary.start;
			error = 0;
			return;
		}
	}

	error = ENOENT;
	return;
}

shared_ptr<fd_t> bootfs_directory_fd::inode_open(cloudabi_device_t st_dev, cloudabi_inode_t st_ino, const cloudabi_fdstat_t *) {
	if (st_dev != device) {
		error = EINVAL;
		return nullptr;
	}

	// TODO: check fdstat_t

	for(size_t i = 0; external_binaries_table[i].name; ++i) {
		if (st_ino == i + 1) {
			char name[64];
			strncpy(name, "bootfs/", sizeof(name));
			strncat(name, external_binaries_table[i].name, sizeof(name) - strlen(name) - 1);
			return make_shared<bootfs_file_fd>(external_binaries_table[i], name);
		}
	}

	error = EINVAL;
	return nullptr;
}

shared_ptr<fd_t> bootfs::get_root_fd() {
	return make_shared<bootfs_directory_fd>("bootfs_root");
}
