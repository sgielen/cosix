#include "fd.hpp"

namespace cloudos {

/** Memory file descriptor
 *
 * This file descriptor acts as a read-only fd to a regular file with the
 * given static contents.
 */
struct memory_fd : public seekable_fd_t {
	// Empty constructor: read() will fail, but you can override read()
	// to replace the returned string with reset(), call memory_fd::read(),
	// then reset() again.
	memory_fd(const char *name, cloudabi_inode_t inode = 0);

	// Owning memory constructor: will return the data in the allocation,
	// and free the Blk when destructed.
	memory_fd(Blk allocation, size_t file_length, const char *name, cloudabi_inode_t inode = 0);

	// Non-owning memory constructor: will return the data in the
	// allocation, will not free the Blk when destructed.
	memory_fd(void *address, size_t file_length, const char *name, cloudabi_inode_t inode = 0);
	~memory_fd() override;

	size_t read(void *dest, size_t count) override;
	void file_stat_fget(cloudabi_filestat_t *buf) override;

	void reset();
	void reset(Blk allocation, size_t file_length, cloudabi_inode_t inode = 0);
	void reset(void *address, size_t file_length, cloudabi_inode_t inode = 0);

private:
	Blk alloc;
	size_t file_length;
	cloudabi_inode_t inode;
	bool owned;
};

}
