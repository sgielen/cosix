#include "shmfs.hpp"

using namespace cloudos;

struct shmfd : public fd_t {
	shmfd(const char *name, cloudabi_device_t device);

	size_t pread(void *str, size_t count, size_t offset) override;
	size_t pwrite(const char *str, size_t count, size_t offset) override;
	void file_stat_fput(const cloudabi_filestat_t*, cloudabi_fsflags_t) override;
	void file_stat_fget(cloudabi_filestat_t *buf) override;

	cloudabi_inode_t inode;

private:
	void resize(size_t sz);

	// TODO: make this sparse
	Blk alloc;
};

shmfs::shmfs(cloudabi_device_t d)
: device(d)
{}

shared_ptr<fd_t> shmfs::get_shm()
{
	return make_shared<shmfd>("shmfd", device);
}

shmfd::shmfd(const char *n, cloudabi_device_t d)
: fd_t(CLOUDABI_FILETYPE_SHARED_MEMORY, 0, n)
, inode(reinterpret_cast<cloudabi_inode_t>(this))
{
	device = d;
}

size_t shmfd::pread(void *str, size_t count, size_t offset)
{
	if(alloc.size < offset) {
		// the whole area doesn't exist
		memset(str, 0, count);
	} else if(alloc.size < offset + count) {
		size_t to_copy = alloc.size - offset;
		memcpy(str, reinterpret_cast<char*>(alloc.ptr) + offset, to_copy);
		memset(str, 0, count - to_copy);
	} else {
		memcpy(str, reinterpret_cast<char*>(alloc.ptr) + offset, count);
	}
	error = 0;
	return count;
}

size_t shmfd::pwrite(const char *str, size_t count, size_t offset)
{
	if(alloc.size < count + offset) {
		resize(count + offset);
	}

	memcpy(reinterpret_cast<char*>(alloc.ptr) + offset, str, count);
	error = 0;
	return count;
}

void shmfd::file_stat_fput(const cloudabi_filestat_t *buf, cloudabi_fsflags_t flags)
{
	if(flags & CLOUDABI_FILESTAT_SIZE) {
		resize(buf->st_size);
	}
	error = 0;
}

void shmfd::file_stat_fget(cloudabi_filestat_t *buf)
{
	buf->st_dev = device;
	buf->st_ino = inode;
	buf->st_filetype = type;
	buf->st_nlink = 1;
	buf->st_size = alloc.size;
	buf->st_atim = 0;
	buf->st_mtim = 0;
	buf->st_ctim = 0;
	error = 0;
}

void shmfd::resize(size_t sz)
{
	if(alloc.size == sz) {
		return;
	}

	Blk alloc2 = allocate(sz);
	if(alloc.ptr != nullptr) {
		size_t copy = sz > alloc.size ? alloc.size : sz;
		memcpy(alloc2.ptr, alloc.ptr, copy);

		if(alloc.size < sz) {
			memset(reinterpret_cast<char*>(alloc2.ptr) + alloc.size, 0, sz - alloc.size);
		}

		deallocate(alloc);
	} else {
		memset(alloc2.ptr, 0, sz);
	}
	alloc = alloc2;
}
