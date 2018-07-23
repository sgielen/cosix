#pragma once

#include <map>
#include <string>
#include <stdexcept>
#include <cosix/reverse_filesystem.hpp>
#include <memory>
#include <vector>
#include <functional>

struct ext2_superblock;
struct ext2_block_group_descriptor;
struct ext2_inode;

struct extfs_file_entry;

typedef std::shared_ptr<extfs_file_entry> file_entry_ptr;

struct pseudo_fd_entry {
	file_entry_ptr file;
};

typedef std::shared_ptr<pseudo_fd_entry> pseudo_fd_ptr;

/** An EXT2 filesystem implementation.
 */
struct extfs : public cosix::reverse_filesystem {
	extfs(int blockdev, cloudabi_device_t);
	~extfs() override;

	typedef cosix::file_entry file_entry;
	typedef cosix::pseudofd_t pseudofd_t;

	file_entry lookup_nonrecursive(cloudabi_inode_t inode, std::string const &filename) override;
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
	int blockdev;
	size_t block_size;
	size_t number_of_block_groups;
	size_t first_block;
	ext2_superblock *superblock = nullptr;
	size_t superblock_offset;
	ext2_block_group_descriptor *block_group_desc = nullptr;
	size_t block_group_desc_offset;
	std::map<cloudabi_inode_t, std::weak_ptr<extfs_file_entry>> open_inodes;
	std::map<pseudofd_t, pseudo_fd_ptr> pseudo_fds;

	using reverse_filesystem::dereference_path; // don't hide the base version
	std::string dereference_path(file_entry_ptr &directory, std::string path, cloudabi_lookupflags_t lookupflags);

	// Read entries from the directory; call the function once per entry; stop once the function
	// returns false, return whether there were any more entries in the directory.
	// NOTE: cookie is not set in the cloudabi_dirent_t, that's the caller's responsibility.
	bool readdir(file_entry_ptr directory, bool, std::function<bool(cloudabi_dirent_t, std::string, file_entry_ptr)> per_entry);
	size_t pread(file_entry_ptr entry, off_t offset, char *dest, size_t requested);
	void pwrite(file_entry_ptr entry, off_t offset, const char *buf, size_t requested);
	size_t allocate_block();
	void deallocate_block(size_t);
	void write_inode(cloudabi_inode_t inode, ext2_inode &inode_data);
	void add_entry_into_directory(file_entry_ptr directory, std::string filename, cloudabi_inode_t new_inode);
	void remove_entry_from_directory(file_entry_ptr directory, std::string filename);
	bool directory_is_empty(file_entry_ptr directory);
	void deallocate_inode(cloudabi_inode_t inode, cloudabi_filetype_t type, ext2_inode &inode_data);
	void update_file_entry_stat(file_entry_ptr entry, const cloudabi_filestat_t *buf, cloudabi_fsflags_t fsflags);
	void allocate(file_entry_ptr entry, size_t size);

	file_entry_ptr get_file_entry_from_inode(cloudabi_inode_t inode);
	file_entry_ptr get_file_entry_from_pseudo(pseudofd_t pseudo);

	friend struct extfs_file_entry;
};
