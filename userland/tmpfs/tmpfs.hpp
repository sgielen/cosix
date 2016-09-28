#pragma once

#include <map>
#include <string>
#include <cloudabi_types.h>
#include <stdexcept>
#include "../../fd/reverse_proto.hpp"
#include <memory>
#include <vector>

using reverse_proto::pseudofd_t;

struct file_entry;
typedef std::shared_ptr<file_entry> file_entry_ptr;

struct file_entry {
	cloudabi_device_t device;
	cloudabi_inode_t inode;
	cloudabi_filetype_t type;

	std::map<std::string, file_entry_ptr> files;
	std::string contents;
};

struct pseudo_fd_entry {
	file_entry_ptr file;
};

typedef std::shared_ptr<pseudo_fd_entry> pseudo_fd_ptr;

struct filesystem_error : public std::runtime_error {
	filesystem_error(cloudabi_errno_t e)
	: std::runtime_error(strerror(e))
	, error(e)
	{}

	cloudabi_errno_t error;
};

/** A temporary filesystem implementation.
 */
struct tmpfs {
	tmpfs(cloudabi_device_t device_id);

	file_entry_ptr lookup(pseudofd_t pseudo, const char *path, size_t len, cloudabi_lookupflags_t lookupflags);
	pseudofd_t open(cloudabi_inode_t inode, int flags);
	void unlink(pseudofd_t pseudo, const char *path, size_t len, cloudabi_ulflags_t unlinkflags);
	cloudabi_inode_t create(pseudofd_t pseudo, const char *path, size_t len, cloudabi_filetype_t type);
	void close(pseudofd_t pseudo);
	size_t pread(pseudofd_t pseudo, off_t offset, char *dest, size_t requested);
	void pwrite(pseudofd_t pseudo, off_t offset, const char *buf, size_t length);

private:
	cloudabi_device_t device;
	std::map<cloudabi_inode_t, file_entry_ptr> inodes;
	std::map<pseudofd_t, pseudo_fd_ptr> pseudo_fds;

	/** Normalizes the given path. When it returns normally, directory
	 * points at the innermost directory pointed to by path. It returns the
	 * filename that is to be opened, created or unlinked.
	 *
	 * It returns an error if the given file_entry ptr is not a directory,
	 * if any of the path components don't reference a directory, or if the
	 * path eventually points outside of the given file_entry.
	 */
	std::string normalize_path(file_entry_ptr &directory, const char *path, size_t len, cloudabi_lookupflags_t lookupflags);

	file_entry_ptr get_file_entry_from_inode(cloudabi_inode_t inode);
	file_entry_ptr get_file_entry_from_pseudo(pseudofd_t pseudo);
};
