#include "bootfs.hpp"
#include <fd/memory_fd.hpp>
#include "global.hpp"
#include <oslibc/numeric.h>
#include "userland/external_binaries.h"

using namespace cloudos;

namespace cloudos {

struct bootfs_directory_fd : fd_t {
	bootfs_directory_fd(const char *n)
	: fd_t(CLOUDABI_FILETYPE_DIRECTORY, n) {}

	shared_ptr<fd_t> openat(const char * /*path */, size_t /*pathlen*/, cloudabi_lookupflags_t /*lookupflags*/, cloudabi_oflags_t /*oflags*/, const cloudabi_fdstat_t * /*fdstat*/) override;
};

struct bootfs_file_fd : public memory_fd {
	bootfs_file_fd(external_binary_t const &file, const char *n)
	: memory_fd(file.start, file.end - file.start, n)
	{}
};

}

shared_ptr<fd_t> bootfs_directory_fd::openat(const char *pathname, size_t pathlen, cloudabi_lookupflags_t, cloudabi_oflags_t, const cloudabi_fdstat_t *) {
	if(pathname == nullptr || pathname[0] == 0 || pathname[0] == '/') {
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
			return make_shared<bootfs_file_fd>(external_binaries_table[i], name);
		}
	}

	error = ENOENT;
	return nullptr;
}

shared_ptr<fd_t> bootfs::get_root_fd() {
	return make_shared<bootfs_directory_fd>("bootfs_root");
}
