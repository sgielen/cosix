#pragma once

#include <map>
#include <string>
#include <stdexcept>
#include <cosix/reverse_filesystem.hpp>
#include <memory>
#include <vector>

struct tmpfs_file_entry;

typedef std::shared_ptr<tmpfs_file_entry> file_entry_ptr;

struct tmpfs_file_entry : public cosix::file_entry {
	std::map<std::string, file_entry_ptr> files;
	std::string contents;
	int hardlinks = 1;
	cloudabi_timestamp_t access_time = 0; // Last time contents were read
	cloudabi_timestamp_t content_time = 0; // Last time contents were changed
	cloudabi_timestamp_t metadata_time = 0; // Last time contents or metadata were changed
};

struct pseudo_fd_entry {
	file_entry_ptr file;
};

typedef std::shared_ptr<pseudo_fd_entry> pseudo_fd_ptr;

/** A temporary filesystem implementation.
 */
struct tmpfs : public cosix::reverse_filesystem {
	tmpfs(cloudabi_device_t);

	// Look up the file entry corresponding to the inode (if filename is empty), or the file entry
	// corresponding to the file pointed to by filename in the directory pointed to by inode.
	file_entry &lookup_nonrecursive(cloudabi_inode_t inode, std::string const &filename) override;
	std::string readlink(cloudabi_inode_t inode) override;

	file_entry lookup(pseudofd_t pseudo, const char *path, size_t len, cloudabi_lookupflags_t lookupflags) override;
	pseudofd_t open(cloudabi_inode_t inode, cloudabi_oflags_t flags) override;
	void allocate(pseudofd_t pseudo, off_t offset, off_t length) override;
	size_t readlink(pseudofd_t pseudo, const char *path, size_t pathlen, char *buf, size_t buflen) override;
	void rename(pseudofd_t pseudo1, const char *path1, size_t path1len, pseudofd_t pseudo2, const char *path2, size_t path2len) override;
	void symlink(pseudofd_t pseudo ,const char *path1, size_t path1len, const char *path2, size_t path2len) override;
	void link(pseudofd_t pseudo1, const char *path1, size_t path1len, cloudabi_lookupflags_t, pseudofd_t pseudo2, const char *path2, size_t path2len) override;
	void unlink(pseudofd_t pseudo, const char *path, size_t len, cloudabi_ulflags_t unlinkflags) override;
	cloudabi_inode_t create(pseudofd_t pseudo, const char *path, size_t len, cloudabi_filetype_t type) override;
	void close(pseudofd_t pseudo) override;
	size_t pread(pseudofd_t pseudo, off_t offset, char *dest, size_t requested) override;
	void pwrite(pseudofd_t pseudo, off_t offset, const char *buf, size_t length) override;
	void datasync(pseudofd_t pseudo) override;
	void sync(pseudofd_t pseudo) override;

	size_t readdir(pseudofd_t pseudo, char *buffer, size_t buflen, cloudabi_dircookie_t &cookie) override;
	void stat_get(pseudofd_t pseudo, cloudabi_lookupflags_t flags, char *path, size_t len, cloudabi_filestat_t *statbuf) override;
	void stat_fget(pseudofd_t pseudo, cloudabi_filestat_t *statbuf) override;
	void stat_fput(pseudofd_t pseudo, const cloudabi_filestat_t *buf, cloudabi_fsflags_t fsflags) override;
	void stat_put(pseudofd_t pseudo, cloudabi_lookupflags_t lookupflags, const char *path, size_t pathlen, const cloudabi_filestat_t *buf, cloudabi_fsflags_t fsflags) override;
	bool is_readable(pseudofd_t pseudo, size_t &nbytes, bool &hangup) override;

private:
	std::map<cloudabi_inode_t, file_entry_ptr> inodes;
	std::map<pseudofd_t, pseudo_fd_ptr> pseudo_fds;

	using reverse_filesystem::dereference_path; // don't hide the base version
	std::string dereference_path(file_entry_ptr &directory, std::string path, cloudabi_lookupflags_t lookupflags);

	file_entry_ptr get_file_entry_from_inode(cloudabi_inode_t inode);
	file_entry_ptr get_file_entry_from_pseudo(pseudofd_t pseudo);
};
