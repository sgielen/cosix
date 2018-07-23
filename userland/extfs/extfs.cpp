#include "extfs.hpp"
#include <cloudabi_syscalls.h>

#include <cassert>
#include <cmath>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

using namespace cosix;

struct ext2_superblock {
	uint32_t num_inodes;
	uint32_t num_blocks;
	uint32_t num_blocks_reserved;
	uint32_t num_unallocated_blocks;
	uint32_t num_unallocated_inodes;
	uint32_t block_of_superblock;
	uint32_t block_size_shifted;
	uint32_t fragment_size_shifted;
	uint32_t blocks_per_group;
	uint32_t fragments_per_group;
	uint32_t inodes_per_group;
	uint32_t last_mount_time;
	uint32_t last_write_time;
	uint16_t times_mounted_since_last_fsck;
	uint16_t times_to_mount_before_fsck;
	char magic[2];
	uint16_t fs_state;
	uint16_t fs_error_handling;
	uint16_t version_minor;
	uint32_t last_fsck_time;
	uint32_t fsck_interval;
	uint32_t creator_os_id;
	uint32_t version_major;
	uint16_t reserved_uid;
	uint16_t reserved_gid;

	// Extended superblock fields:
	uint32_t first_nonreserved_inode;
	uint16_t inode_size;
	uint16_t this_block_group;
	uint32_t optional_features_present;
	uint32_t required_features_present;
	uint32_t rw_required_features_present;
	char filesystem_id[16];
	char volume_name[16];
	char last_mount_path[64];
	uint32_t compression_algorithm_used;
	uint8_t preallocate_blocks_for_files;
	uint8_t preallocate_blocks_for_directories;
	uint16_t unused1;
	char journal_id[16];
	uint32_t journal_inode;
	uint32_t journal_device;
	uint32_t orphan_inode_head;
	char unused2[788];
} __attribute__((packed));

struct ext2_block_group_descriptor {
	uint32_t block_usage_bitmap_addr;
	uint32_t inode_usage_bitmap_addr;
	uint32_t inode_table_addr;
	uint16_t num_unallocated_blocks;
	uint16_t num_unallocated_inodes;
	uint16_t num_directories;
	char unused[14];
} __attribute__((packed));

struct ext2_inode {
	uint16_t type_and_permissions;
	uint16_t userid;
	uint32_t size1;
	uint32_t atime;
	uint32_t ctime;
	uint32_t mtime;
	uint32_t dtime;
	uint16_t groupid;
	uint16_t nlink;
	uint32_t sectorcount;
	uint32_t flags;
	uint32_t os_value1;
	uint32_t blockptr[12];
	uint32_t singly_blockptr;
	uint32_t doubly_blockptr;
	uint32_t triply_blockptr;
	uint32_t generation;
	uint32_t file_acl_blockptr;
	uint32_t size2_or_dir_acl_blockptr;
	uint32_t fragment_blockptr;
	uint32_t os_value2[3];
} __attribute__((packed));

struct ext2_direntry {
	uint32_t inode;
	uint16_t size_of_entry;
	uint8_t namelen1;
	uint8_t type_or_namelen2;
	char name[0];
} __attribute__((packed));

static_assert(sizeof(ext2_superblock) == 1024, "Size of ext2 superblock");
static_assert(sizeof(ext2_block_group_descriptor) == 32, "Size of ext2 block group descriptor");
static_assert(sizeof(ext2_inode) == 128, "Size of ext2 inode");

/* TODO: don't hardcode this size */
#define SECTOR_SIZE 512

static void check_ssize(ssize_t res, size_t expect) {
	if(res < 0) {
		throw cloudabi_system_error(errno);
	} else if(size_t(res) != expect) {
		throw cloudabi_system_error(EIO);
	}
}

struct ext2_block_iterator {
	ext2_block_iterator(int b, int s, ext2_inode &i)
	: blockdev(b)
	, block_size(s)
	, pointers_per_block(s / 4)
	, inode(&i)
	, it(-1)
	{
		assert(block_size % 4 == 0);

		// Don't use the block iterator on a symbolic link
		auto type = inode->type_and_permissions & 0xf000;
		assert(type != 0xa000);

		// Find first block
		++(*this);
	}

	ext2_block_iterator()
	: blockdev(-1)
	, it(INT_MAX)
	{
	}

	bool operator==(ext2_block_iterator const &o) {
		if(it != INT_MAX && it == o.it) {
			assert(blockdev == o.blockdev);
			assert(inode == o.inode);
		}
		return it == o.it;
	}

	bool operator!=(ext2_block_iterator const &o) {
		return !(*this == o);
	}

	uint32_t operator*() {
		if(it < 12) {
			assert(inode->blockptr[it] != 0);
			return inode->blockptr[it];
		} else if(it < 12 + pointers_per_block) {
			assert(inode->singly_blockptr != 0);

			int deref_it = it - 12;
			assert(deref_it < pointers_per_block);

			uint32_t refblock[pointers_per_block];
			auto res = ::pread(blockdev, refblock, block_size, block_size * inode->singly_blockptr);
			check_ssize(res, block_size);
			assert(refblock[it - 12] != 0);
			return refblock[it - 12];
		} else if(it < 12 + pointers_per_block + pointers_per_block * pointers_per_block) {
			assert(inode->doubly_blockptr != 0);

			int deref_it1 = (it - 12 - pointers_per_block) / pointers_per_block;
			int deref_it2 = (it - 12 - pointers_per_block) % pointers_per_block;
			assert(deref_it1 < pointers_per_block);
			assert(deref_it2 < pointers_per_block);

			uint32_t refblock[pointers_per_block];
			auto res = ::pread(blockdev, refblock, block_size, block_size * inode->doubly_blockptr);
			check_ssize(res, block_size);
			assert(refblock[deref_it1] != 0);

			res = ::pread(blockdev, refblock, block_size, block_size * refblock[deref_it1]);
			check_ssize(res, block_size);
			assert(refblock[deref_it2] != 0);
			return refblock[deref_it2];
		} else if(it < 12 + pointers_per_block + pointers_per_block * pointers_per_block + pointers_per_block * pointers_per_block * pointers_per_block) {
			// TODO: read triply_blockptr, find index in there
			assert(false);
		} else {
			assert(!"operator* on an invalid block");
		}
		abort();
	}

	void operator++() {
		assert(it >= -1);
		assert(it != INT_MAX);
		auto it_before = it;
		(void)it_before;
		// Iterate through block pointers, finding the next one that is set.
		// Once one is 0, the end of the file has been reached and it
		// is mandatory for all block pointers after it to also be 0, so we
		// can stop looking then.
		if(it < 11) {
			if(!inode->blockptr[++it]) {
				would_be_next_it = it;
				it = INT_MAX;
			}
		} else if(it < 11 + pointers_per_block) {
			if(!inode->singly_blockptr) {
				assert(it == 11);
				would_be_next_it = it + 1;
				it = INT_MAX;
			} else {
				++it;
				int deref_it = it - 12;
				assert(deref_it < pointers_per_block);

				uint32_t refblock[pointers_per_block];
				auto res = ::pread(blockdev, refblock, block_size, block_size * inode->singly_blockptr);
				check_ssize(res, block_size);
				if(!refblock[deref_it]) {
					would_be_next_it = it;
					it = INT_MAX;
				}
			}
		} else if(it < 11 + pointers_per_block + pointers_per_block * pointers_per_block) {
			if(!inode->doubly_blockptr) {
				assert(it == 11 + pointers_per_block);
				would_be_next_it = it + 1;
				it = INT_MAX;
			} else {
				++it;
				int deref_it1 = (it - 12 - pointers_per_block) / pointers_per_block;
				int deref_it2 = (it - 12 - pointers_per_block) % pointers_per_block;
				assert(deref_it1 < pointers_per_block);
				assert(deref_it2 < pointers_per_block);

				uint32_t refblock[pointers_per_block];
				auto res = ::pread(blockdev, refblock, block_size, block_size * inode->doubly_blockptr);
				check_ssize(res, block_size);
				if(!refblock[deref_it1]) {
					would_be_next_it = it;
					it = INT_MAX;
				} else {
					res = ::pread(blockdev, refblock, block_size, block_size * refblock[deref_it1]);
					check_ssize(res, block_size);
					if(!refblock[deref_it2]) {
						would_be_next_it = it;
						it = INT_MAX;
					}
				}
			}
		} else if(it < 11 + pointers_per_block + pointers_per_block * pointers_per_block + pointers_per_block * pointers_per_block * pointers_per_block) {
			// TODO: increase through triply blockptr
			assert(false);
		} else {
			// End of blocks reached
			// TODO: end of all possible blocks reached, how to react?
			assert(false);
		}
		assert(it > it_before);
	}

	void assign_new_block(std::function<int(void)> allocate_block) {
		assert(it == INT_MAX);
		assert(would_be_next_it != INT_MAX);

		it = would_be_next_it;
		would_be_next_it = INT_MAX;

		assert(block_size % SECTOR_SIZE == 0);
		inode->sectorcount += block_size / SECTOR_SIZE;

		// TODO: what if we have already put an inode in a block group descriptor table,
		// but then it turns out there's not enough blocks for this inode? Can we get
		// blocks from _another_ block group into this one for an inode? Or is the size
		// of all files within a block group limited to the amount of blocks within
		// that group?

		if(it < 12) {
			assert(inode->blockptr[it] == 0);
			inode->blockptr[it] = allocate_block();
		} else if(it == 12) {
			assert(inode->singly_blockptr == 0);
			inode->singly_blockptr = allocate_block();

			uint32_t refblock[pointers_per_block];
			refblock[0] = allocate_block();

			auto res = ::pwrite(blockdev, refblock, block_size, block_size * inode->singly_blockptr);
			check_ssize(res, block_size);
		} else if(it < 12 + pointers_per_block) {
			assert(inode->singly_blockptr != 0);
			uint32_t refblock[pointers_per_block];
			auto res = ::pread(blockdev, refblock, block_size, block_size * inode->singly_blockptr);
			check_ssize(res, block_size);
			assert(refblock[it - 12] == 0);
			refblock[it - 12] = allocate_block();

			res = ::pwrite(blockdev, refblock, block_size, block_size * inode->singly_blockptr);
			check_ssize(res, block_size);
		} else if(it == 12 + pointers_per_block) {
			// TODO: create doubly indirect blockptr (need a second block)
			assert(false);
		} else if(it < 12 + pointers_per_block + pointers_per_block * pointers_per_block) {
			// TODO: append to doubly indirect blockptr
			assert(false);
		} else if(it == 12 + pointers_per_block + pointers_per_block * pointers_per_block) {
			// TODO: create triply indirect blockptr (need a second and third block)
			assert(false);
		} else if(it < 12 + pointers_per_block + pointers_per_block * pointers_per_block + pointers_per_block * pointers_per_block * pointers_per_block) {
			// TODO: append to triply indirect blockptr (may need an extra second block)
			assert(false);
		} else {
			// should be unreachable
			assert(false);
		}
	}

private:
	int blockdev;
	int block_size;
	int pointers_per_block;
	ext2_inode *inode;
	// -1 is initializing;
	// 0 to 11 are direct inode blockptrs;
	// 12 to 12+ppb are singly indirect inode blockptrs;
	// 12+ppb to 12+ppb+ppb^2 are doubly indirect inode blockptrs;
	// 12+ppb+ppb^2 to 12+ppb+ppb^2+ppb^3 are triply indirect inode blockptrs
	// INT_MAX is end
	int it;
	int would_be_next_it = INT_MAX;
	// TODO: add a cache here/in the FS implementation for indirection blocks so we don't have to re-read them all the time
};

struct extfs_file_entry : public cosix::file_entry {
	extfs_file_entry(extfs *fs, cloudabi_inode_t i, cloudabi_device_t d, ext2_inode &id) : e(fs), inode_data(id) {
		inode = i;
		device = d;
		inode_data.nlink++;
		e->write_inode(inode, inode_data);
	}

	~extfs_file_entry() {
		inode_data.nlink--;
		if(inode_data.nlink == 0) {
			e->deallocate_inode(inode, type, inode_data);
		} else {
			e->write_inode(inode, inode_data);
		}
	}

	extfs *e;
	ext2_inode inode_data;
};

// This function takes an object that spans multiple sectors that is already on
// the disk, and a range inside the object that should be updated on the disk.
// It computes which exact pwrite command will take care of this.
// NOTE: It assumes object[offset + length - 1, rounded up to sector size] can be
// dereferenced, even if that exceeds offset + length. This is no problem if the
// object was also pread() from the disk, since then the allocation must be large
// enough for that as well.
// Throws cloudabi_system_error if the write failed.
static void pwrite_partial(int fd, const void *object, size_t offset_in_obj, size_t length, off_t object_offset_on_disk)
{
	const size_t sector_size = SECTOR_SIZE;
	assert(object_offset_on_disk % sector_size == 0); // object offset itself must be sector-aligned

	off_t range_offset_on_disk = object_offset_on_disk + offset_in_obj;

	// make sure the disk offset is on a disk boundary
	size_t offset_from_sector = range_offset_on_disk % sector_size;
	range_offset_on_disk -= offset_from_sector;
	assert(offset_in_obj >= offset_from_sector);
	offset_in_obj -= offset_from_sector;
	length += offset_from_sector;

	// make sure the disk length is on a disk boundary
	if(length % sector_size) {
		length += sector_size - (length % sector_size);
	}

	auto *o = reinterpret_cast<const char*>(object);
	auto res = ::pwrite(fd, o + offset_in_obj, length, object_offset_on_disk + offset_in_obj);
	check_ssize(res, length);
}

static void pwrite_partial(int fd, const void *object, const void *changed_ptr, size_t length, off_t object_offset_on_disk)
{
	assert(changed_ptr >= object);
	return pwrite_partial(fd, object,
		reinterpret_cast<const char*>(changed_ptr) - reinterpret_cast<const char*>(object),
		length, object_offset_on_disk);
}

extfs::extfs(int b, cloudabi_device_t d)
: cosix::reverse_filesystem(d)
, blockdev(b)
{
	superblock = reinterpret_cast<ext2_superblock*>(malloc(sizeof(ext2_superblock)));
	superblock_offset = 1024;
	ssize_t res = ::pread(blockdev, superblock, sizeof(ext2_superblock), superblock_offset);
	if(res != sizeof(ext2_superblock)) {
		throw std::runtime_error("Failed to read superblock");
	}

	if(memcmp(superblock->magic, "\x53\xef", 2) != 0) {
		throw std::runtime_error("ext2 superblock magic incorrect");
	}

	if(superblock->fs_state != 1) {
		throw std::runtime_error("filesystem needs fsck, refusing to mount");
	}

	if(superblock->version_major == 0) {
		// Extended superblock does not exist. Fill it with default values.
		superblock->first_nonreserved_inode = 11;
		superblock->inode_size = 128;
		memset(reinterpret_cast<char*>(superblock) + offsetof(ext2_superblock, this_block_group),
			0, sizeof(ext2_superblock) - offsetof(ext2_superblock, this_block_group));
	}

	if(superblock->required_features_present != 0) {
		throw std::runtime_error("required features are not supported");
	}

	if(superblock->rw_required_features_present != 0) {
		throw std::runtime_error("required r/w features are not supported");
	}

	number_of_block_groups = std::ceil(float(superblock->num_blocks) / superblock->blocks_per_group);
	if(number_of_block_groups != std::ceil(float(superblock->num_inodes) / superblock->inodes_per_group)) {
		throw std::runtime_error("number of block groups inconsistent within filesystem");
	}

	block_size = 1024 << superblock->block_size_shifted;
	first_block = block_size == 1024 ? 1 : 0;

	int block_after_superblock = block_size == 1024 ? 2 : 1;
	size_t readsz = sizeof(ext2_block_group_descriptor) * number_of_block_groups;
	if(readsz % SECTOR_SIZE) {
		// round up to nearest boundary
		readsz += SECTOR_SIZE - (readsz % SECTOR_SIZE);
	}
	block_group_desc = reinterpret_cast<ext2_block_group_descriptor*>(malloc(readsz));
	block_group_desc_offset = block_size * block_after_superblock;
	res = ::pread(blockdev, block_group_desc, readsz, block_group_desc_offset);
	check_ssize(res, readsz);

	// Open root directory as pseudo 0
	file_entry_ptr root = get_file_entry_from_inode(2 /* root directory inode in ext2 */);
	pseudo_fd_ptr root_pseudo(new pseudo_fd_entry);
	root_pseudo->file = root;
	pseudo_fds[0] = root_pseudo;
}

extfs::~extfs()
{
	free(block_group_desc);
	free(superblock);
}

file_entry extfs::lookup_nonrecursive(cloudabi_inode_t inode, std::string const &filename)
{
	file_entry_ptr entry = get_file_entry_from_inode(inode);
	assert(entry->inode == inode);
	if(filename.empty()) {
		return *entry;
	}
	if(entry->type != CLOUDABI_FILETYPE_DIRECTORY) {
		throw cloudabi_system_error(ENOTDIR);
	}

	bool entry_found = false;
	cloudabi_dirent_t dirent;
	readdir(entry, false, [&](cloudabi_dirent_t d, std::string n, file_entry_ptr) -> bool {
		assert(!entry_found);
		if(n == filename) {
			entry_found = true;
			dirent = d;
			return false;
		}
		return true;
	});

	if(!entry_found) {
		throw cloudabi_system_error(ENOENT);
	} else {
		return *get_file_entry_from_inode(dirent.d_ino);
	}
}

std::string extfs::readlink(cloudabi_inode_t inode)
{
	file_entry_ptr entry = get_file_entry_from_inode(inode);
	if(entry->type != CLOUDABI_FILETYPE_SYMBOLIC_LINK) {
		throw cloudabi_system_error(EINVAL);
	}
	// TODO: currently we only copy the direct block ptrs, and assume
	// the indirect ones aren't set
	// this approach allows links of max 12*4=48 bytes in length though
	// with singly_blockptr support, we allow 1024 + 48 bytes, should be plenty
	char buf[sizeof(entry->inode_data.blockptr)];
	memcpy(buf, entry->inode_data.blockptr, sizeof(entry->inode_data.blockptr));
	assert(entry->inode_data.singly_blockptr == 0);
	return std::string(buf, strnlen(buf, sizeof(buf)));
}

file_entry extfs::lookup(pseudofd_t pseudo, const char *path, size_t len, cloudabi_lookupflags_t lookupflags)
{
	file_entry_ptr directory = get_file_entry_from_pseudo(pseudo);

	std::string filename = dereference_path(directory, std::string(path, len), lookupflags);
	directory->inode_data.atime = time(nullptr);
	write_inode(directory->inode, directory->inode_data);
	return lookup_nonrecursive(directory->inode, filename);
}

pseudofd_t extfs::open(cloudabi_inode_t inode, cloudabi_oflags_t oflags)
{
	file_entry_ptr entry = get_file_entry_from_inode(inode);

	if(entry->type == CLOUDABI_FILETYPE_SYMBOLIC_LINK) {
		throw cloudabi_system_error(ELOOP);
	} else if(entry->type != CLOUDABI_FILETYPE_REGULAR_FILE && entry->type != CLOUDABI_FILETYPE_DIRECTORY) {
		// can't open via the extfs
		throw cloudabi_system_error(EINVAL);
	}

	if(oflags & CLOUDABI_O_DIRECTORY && entry->type != CLOUDABI_FILETYPE_DIRECTORY) {
		throw cloudabi_system_error(ENOTDIR);
	}

	auto &idata = entry->inode_data;
	idata.atime = time(nullptr);

	if(oflags & CLOUDABI_O_TRUNC) {
		if(entry->type != CLOUDABI_FILETYPE_REGULAR_FILE) {
			throw cloudabi_system_error(EINVAL);
		}
		ext2_block_iterator it(blockdev, block_size, idata);
		for(; it != ext2_block_iterator(); ++it) {
			deallocate_block(*it);
		}
		memset(idata.blockptr, 0, sizeof(idata.blockptr));
		if(idata.singly_blockptr) {
			deallocate_block(idata.singly_blockptr);
			idata.singly_blockptr = 0;
		}
		if(idata.doubly_blockptr) {
			assert(!"TODO");
			idata.doubly_blockptr = 0;
		}
		if(idata.triply_blockptr) {
			assert(!"TODO");
			idata.triply_blockptr = 0;
		}

		idata.size1 = 0;
		idata.size2_or_dir_acl_blockptr = 0;
		idata.sectorcount = 0;
		idata.ctime = idata.mtime = time(nullptr);
	}

	write_inode(entry->inode, idata);

	pseudo_fd_ptr pseudo(new pseudo_fd_entry);
	pseudo->file = entry;
	pseudofd_t fd = reinterpret_cast<pseudofd_t>(pseudo.get());
	pseudo_fds[fd] = pseudo;
	return fd;
}

void extfs::link(pseudofd_t pseudo1, const char *path1, size_t path1len, cloudabi_lookupflags_t lookupflags, pseudofd_t pseudo2, const char *path2, size_t path2len) {
	file_entry_ptr dir1 = get_file_entry_from_pseudo(pseudo1);
	std::string filename1 = dereference_path(dir1, std::string(path1, path1len), lookupflags);
	assert(dir1->type == CLOUDABI_FILETYPE_DIRECTORY);

	auto entry1 = lookup_nonrecursive(dir1->inode, filename1);
	if(entry1.type == CLOUDABI_FILETYPE_DIRECTORY) {
		throw cloudabi_system_error(EPERM);
	}

	file_entry_ptr dir2 = get_file_entry_from_pseudo(pseudo2);
	std::string filename2 = dereference_path(dir2, std::string(path2, path2len), 0);
	assert(dir2->type == CLOUDABI_FILETYPE_DIRECTORY);

	bool entry_found = false;
	readdir(dir2, false, [&](cloudabi_dirent_t, std::string n, file_entry_ptr) -> bool {
		assert(!entry_found);
		if(n == filename2) {
			entry_found = true;
			return false;
		}
		return true;
	});
	if(entry_found) {
		throw cloudabi_system_error(EEXIST);
	}

	auto entry1_ptr = get_file_entry_from_inode(entry1.inode);
	entry1_ptr->inode_data.nlink += 1;
	entry1_ptr->inode_data.mtime = time(nullptr);
	write_inode(entry1_ptr->inode, entry1_ptr->inode_data);

	add_entry_into_directory(dir2, filename2, entry1_ptr->inode);
	dir2->inode_data.ctime = dir2->inode_data.mtime = time(nullptr);
	write_inode(dir2->inode, dir2->inode_data);
}

void extfs::allocate(pseudofd_t pseudo, off_t offset, off_t length) {
	file_entry_ptr entry = get_file_entry_from_pseudo(pseudo);
	if(entry->type != CLOUDABI_FILETYPE_REGULAR_FILE) {
		throw cloudabi_system_error(EINVAL);
	}

	size_t minsize = offset + length;

	size_t size = entry->inode_data.size1;
	if(entry->type == CLOUDABI_FILETYPE_REGULAR_FILE) {
		size += (uint64_t(entry->inode_data.size2_or_dir_acl_blockptr) & 0xffffffff) << 32;
	}

	if(size < minsize) {
		allocate(entry, minsize);
		entry->inode_data.ctime = entry->inode_data.mtime = time(nullptr);
		write_inode(entry->inode, entry->inode_data);
	}
}

size_t extfs::readlink(pseudofd_t pseudo, const char *path, size_t pathlen, char *buf, size_t buflen) {
	auto entry = lookup(pseudo, path, pathlen, 0);
	std::string contents = readlink(entry.inode);
	size_t copy = std::min(buflen, contents.size());
	memcpy(buf, contents.c_str(), copy);
	return copy;
}

void extfs::rename(pseudofd_t pseudo1, const char *path1, size_t path1len, pseudofd_t pseudo2, const char *path2, size_t path2len)
{
	file_entry_ptr dir1 = get_file_entry_from_pseudo(pseudo1);
	file_entry_ptr dir2 = get_file_entry_from_pseudo(pseudo2);

	std::string filename1 = dereference_path(dir1, std::string(path1, path1len), 0);
	std::string filename2 = dereference_path(dir2, std::string(path2, path2len), 0);

	assert(dir1->type == CLOUDABI_FILETYPE_DIRECTORY);
	assert(dir2->type == CLOUDABI_FILETYPE_DIRECTORY);

	// TODO: check if dir1/fn1 is a parent of dir2/fn2 (i.e. directory would be moved inside itself)

	if(filename1 == "." || filename1 == "..") {
		throw cloudabi_system_error(EINVAL);
	}

	// source file exists?
	file_entry entry = lookup_nonrecursive(dir1->inode, filename1);

	// destination doesn't exist? -> rename file
	file_entry_ptr newentry;
	readdir(dir2, false, [&](cloudabi_dirent_t d, std::string n, file_entry_ptr) -> bool {
		assert(!newentry);
		if(n == filename2) {
			newentry = get_file_entry_from_inode(d.d_ino);
			return false;
		}
		return true;
	});
	if(!newentry) {
		file_entry_ptr entryp = get_file_entry_from_inode(entry.inode);
		assert(entry.type == entryp->type);
		if(entry.type == CLOUDABI_FILETYPE_DIRECTORY && dir1->inode != dir2->inode) {
			remove_entry_from_directory(entryp, "..");
			add_entry_into_directory(entryp, "..", dir2->inode);
			// TODO this needs to be in the block group, not the inode
			/*
			dir1->inode_data.num_directories -= 1;
			dir2->inode_data.num_directories += 1;
			*/
			dir1->inode_data.nlink -= 1;
			dir2->inode_data.nlink += 2;
		}
		remove_entry_from_directory(dir1, filename1);
		add_entry_into_directory(dir2, filename2, entry.inode);
		auto t = time(nullptr);
		dir1->inode_data.ctime = dir1->inode_data.mtime = t;
		dir2->inode_data.ctime = dir2->inode_data.mtime = t;
		entryp->inode_data.ctime = entryp->inode_data.mtime = t;
		write_inode(dir1->inode, dir1->inode_data);
		write_inode(dir2->inode, dir2->inode_data);
		write_inode(entryp->inode, entryp->inode_data);
		return;
	}

	// destination is directory? -> must be empty, overwrite it
	if(newentry->type == CLOUDABI_FILETYPE_DIRECTORY) {
		if(entry.type != CLOUDABI_FILETYPE_DIRECTORY) {
			throw cloudabi_system_error(EISDIR);
		}
		if(!directory_is_empty(newentry)) {
			throw cloudabi_system_error(ENOTEMPTY);
		}
		// remove destination directory
		remove_entry_from_directory(newentry, ".");
		remove_entry_from_directory(newentry, "..");
		remove_entry_from_directory(dir2, filename2);
		newentry->inode_data.nlink -= 2;
		assert(newentry->inode_data.nlink == 0);

		// rename source directory
		remove_entry_from_directory(dir1, filename1);
		dir1->inode_data.nlink -= 1;
		// TODO: in the block group, decrease number of directories by 1

		add_entry_into_directory(dir2, filename2, entry.inode);

		file_entry_ptr entryp = get_file_entry_from_inode(entry.inode);
		auto t = time(nullptr);
		dir1->inode_data.ctime = dir1->inode_data.mtime = t;
		dir2->inode_data.ctime = dir2->inode_data.mtime = t;
		entryp->inode_data.ctime = entryp->inode_data.mtime = t;
		write_inode(dir1->inode, dir1->inode_data);
		write_inode(dir2->inode, dir2->inode_data);
		write_inode(entryp->inode, entryp->inode_data);
		return;
	}

	if(entry.type == CLOUDABI_FILETYPE_DIRECTORY) {
		throw cloudabi_system_error(ENOTDIR);
	}

	remove_entry_from_directory(dir1, filename1);
	remove_entry_from_directory(dir2, filename2);
	add_entry_into_directory(dir2, filename2, entry.inode);

	file_entry_ptr entryp = get_file_entry_from_inode(entry.inode);
	auto t = time(nullptr);
	dir1->inode_data.ctime = dir1->inode_data.mtime = t;
	dir2->inode_data.ctime = dir2->inode_data.mtime = t;
	entryp->inode_data.ctime = entryp->inode_data.mtime = t;
	write_inode(dir1->inode, dir1->inode_data);
	write_inode(dir2->inode, dir2->inode_data);
	write_inode(entryp->inode, entryp->inode_data);
}

void extfs::symlink(pseudofd_t pseudo ,const char *path1, size_t path1len, const char *path2, size_t path2len) {
	auto inode = create(pseudo, path2, path2len, CLOUDABI_FILETYPE_SYMBOLIC_LINK);
	file_entry_ptr entry = get_file_entry_from_inode(inode);

	// entry has no blocks yet
	assert(entry->inode_data.blockptr[0] == 0);
	assert(entry->type == CLOUDABI_FILETYPE_SYMBOLIC_LINK);

	// TODO: currently we copy only to the direct block ptrs, and set the
	// rest to 0. Not sure if that's correct.
	size_t to_copy = std::min(sizeof(entry->inode_data.blockptr), path1len);
	memcpy(entry->inode_data.blockptr, path1, to_copy);
	memset(reinterpret_cast<char*>(entry->inode_data.blockptr) + to_copy,
		0, sizeof(entry->inode_data.blockptr) - to_copy);
	entry->inode_data.singly_blockptr = 0;

	entry->inode_data.atime = entry->inode_data.ctime = entry->inode_data.mtime = time(nullptr);
	write_inode(entry->inode, entry->inode_data);
}

void extfs::unlink(pseudofd_t pseudo, const char *path, size_t len, cloudabi_ulflags_t unlinkflags)
{
	file_entry_ptr directory = get_file_entry_from_pseudo(pseudo);

	std::string filename = dereference_path(directory, std::string(path, len), 0);

	auto entry = lookup_nonrecursive(directory->inode, filename);
	auto entry_ptr = get_file_entry_from_inode(entry.inode);

	bool removedir = unlinkflags & CLOUDABI_UNLINK_REMOVEDIR;

	if(entry.type == CLOUDABI_FILETYPE_DIRECTORY) {
		if(!removedir) {
			throw cloudabi_system_error(EPERM);
		}
		if(!directory_is_empty(entry_ptr)) {
			throw cloudabi_system_error(ENOTEMPTY);
		}
	} else {
		if(removedir) {
			throw cloudabi_system_error(ENOTDIR);
		}
	}

	remove_entry_from_directory(directory, filename);
	directory->inode_data.ctime = directory->inode_data.mtime = time(nullptr);

	if(entry.type == CLOUDABI_FILETYPE_DIRECTORY) {
		remove_entry_from_directory(entry_ptr, "..");
		remove_entry_from_directory(entry_ptr, ".");
		entry_ptr->inode_data.nlink -= 2;
		directory->inode_data.nlink -= 1;
		// TODO: decrease directory count by 1 for this block group
	} else {
		entry_ptr->inode_data.nlink -= 1;
	}

	write_inode(directory->inode, directory->inode_data);

	entry_ptr->inode_data.ctime = entry_ptr->inode_data.mtime = time(nullptr);
	write_inode(entry_ptr->inode, entry_ptr->inode_data);
}

cloudabi_inode_t extfs::create(pseudofd_t pseudo, const char *path, size_t len, cloudabi_filetype_t type)
{
	file_entry_ptr directory = get_file_entry_from_pseudo(pseudo);

	std::string filename = dereference_path(directory, std::string(path, len), 0);
	assert(directory->type == CLOUDABI_FILETYPE_DIRECTORY);

	bool entry_found = false;
	readdir(directory, false, [&](cloudabi_dirent_t, std::string n, file_entry_ptr) -> bool {
		assert(!entry_found);
		if(n == filename) {
			entry_found = true;
			return false;
		}
		return true;
	});
	if(entry_found) {
		throw cloudabi_system_error(EEXIST);
	}

	if(superblock->num_unallocated_inodes == 0) {
		throw cloudabi_system_error(ENOSPC);
	}

	ext2_inode inode_data;
	memset(&inode_data, 0, sizeof(ext2_inode));
	// u=rw,g=rw,o=r default mask
	inode_data.type_and_permissions = 0x1b4;
	if(type == CLOUDABI_FILETYPE_BLOCK_DEVICE) {
		inode_data.type_and_permissions |= 0x6000;
	} else if(type == CLOUDABI_FILETYPE_CHARACTER_DEVICE) {
		inode_data.type_and_permissions |= 0x2000;
	} else if(type == CLOUDABI_FILETYPE_DIRECTORY) {
		inode_data.type_and_permissions |= 0x4000 | 0x049 /* +x */;
	} else if(type == CLOUDABI_FILETYPE_REGULAR_FILE) {
		inode_data.type_and_permissions |= 0x8000;
	} else if(type == CLOUDABI_FILETYPE_SYMBOLIC_LINK) {
		inode_data.type_and_permissions |= 0xA000;
	} else {
		throw cloudabi_system_error(EINVAL);
	}

	inode_data.nlink = type == CLOUDABI_FILETYPE_DIRECTORY ? 2 : 1;
	inode_data.atime = inode_data.mtime = inode_data.ctime = time(nullptr);
	// TODO: generation?

	// TODO: could make this more efficient by searching only from the last created inode
	// (but with possibility of wrapping around to the first nonreserved inode again, and
	// not looping forever)
	cloudabi_inode_t new_inode = superblock->first_nonreserved_inode;

	// if the next is untrue, the bitmap would span more than one block
	assert(superblock->inodes_per_group <= uint64_t(8 * block_size));

	bool free_inode_found = false;
	while(new_inode < superblock->num_inodes) {
		// in the block group of new_inode, find a free inode in its bitmap
		size_t blockgroup = (new_inode - 1) / superblock->inodes_per_group;
		assert(blockgroup < number_of_block_groups);
		auto &descriptor = block_group_desc[blockgroup];

		uint8_t bitmap[block_size];
		ssize_t res = ::pread(blockdev, bitmap, block_size, block_size * descriptor.inode_usage_bitmap_addr);
		check_ssize(res, block_size);

		// find an unused inode in this bitmap
		for(size_t i = 0; i < size_t(block_size); ++i) {
			if(bitmap[i] == 0xff) {
				continue;
			}
			// free inode found
			for(int j = 0; j < 8; ++j) {
				if(~bitmap[i] & (1 << j)) {
					new_inode = blockgroup * superblock->inodes_per_group + i * 8 + j + 1;
					if(new_inode >= superblock->first_nonreserved_inode) {
						free_inode_found = true;
						break;
					}
				}
			}
			if(free_inode_found) {
				break;
			}
		}

		if(free_inode_found) {
			break;
		} else {
			new_inode = (blockgroup + 1) * superblock->inodes_per_group + 1;
		}
	}

	if(!free_inode_found) {
		throw cloudabi_system_error(ENOSPC);
	}

	size_t blockgroup = (new_inode - 1) / superblock->inodes_per_group;
	assert(blockgroup < number_of_block_groups);
	auto &descriptor = block_group_desc[blockgroup];
	size_t index = (new_inode - 1) % superblock->inodes_per_group;

	// Write inode data into the inode table of this block group
	write_inode(new_inode, inode_data);

	// Set this bit in the bitmask
	{
		uint8_t bitmap[block_size];
		ssize_t res = ::pread(blockdev, bitmap, block_size, block_size * descriptor.inode_usage_bitmap_addr);
		check_ssize(res, block_size);

		size_t byte = index / 8;
		uint8_t bit = 1 << (index % 8);
		assert((bitmap[byte] & bit) == 0);
		bitmap[byte] |= bit;
		res = ::pwrite(blockdev, bitmap, block_size, block_size * descriptor.inode_usage_bitmap_addr);
		check_ssize(res, block_size);
	}

	assert(descriptor.num_unallocated_inodes > 0);
	assert(superblock->num_unallocated_inodes > 0);
	descriptor.num_unallocated_inodes -= 1;
	if(type == CLOUDABI_FILETYPE_DIRECTORY) {
		descriptor.num_directories += 1;
	}
	superblock->num_unallocated_inodes -= 1;

	// Write updated block group descriptor
	pwrite_partial(blockdev, block_group_desc, &descriptor, sizeof(descriptor), block_group_desc_offset);

	// Write updated superblock
	pwrite_partial(blockdev, superblock, int(0), sizeof(ext2_superblock), superblock_offset);

	// Add an entry into the directory
	add_entry_into_directory(directory, filename, new_inode);

	if(type == CLOUDABI_FILETYPE_DIRECTORY) {
		auto new_directory = get_file_entry_from_inode(new_inode);
		assert(new_directory->type == CLOUDABI_FILETYPE_DIRECTORY);
		add_entry_into_directory(new_directory, ".", new_inode);
		add_entry_into_directory(new_directory, "..", directory->inode);

		directory->inode_data.nlink += 1;
	}

	directory->inode_data.ctime = directory->inode_data.mtime = time(nullptr);
	write_inode(directory->inode, directory->inode_data);

	return new_inode;
}

void extfs::close(pseudofd_t pseudo)
{
	auto it = pseudo_fds.find(pseudo);
	if(it != pseudo_fds.end()) {
		pseudo_fds.erase(it);
	} else {
		throw cloudabi_system_error(EBADF);
	}
}

size_t extfs::pread(file_entry_ptr entry, off_t offset, char *dest, size_t requested)
{
	size_t size = entry->inode_data.size1;
	if(entry->type == CLOUDABI_FILETYPE_REGULAR_FILE) {
		size += (uint64_t(entry->inode_data.size2_or_dir_acl_blockptr) & 0xffffffff) << 32;
	}

	if(offset > size) {
		// EOF
		return 0;
	}

	if(offset + requested > size) {
		// request the remaining data
		requested = size - offset;
	}

	ext2_block_iterator it(blockdev, block_size, entry->inode_data);

	size_t block_skip = offset / block_size;
	offset %= block_size;

	// Skip blocks we don't need to pread
	for(; block_skip > 0; ++it, --block_skip) {
		assert(it != ext2_block_iterator());
	}

	size_t read = 0;
	// Read block-by-block, since they may not be in the same location on the disk
	// TODO: could optimize this by checking if the disk data is contiguous
	for(; it != ext2_block_iterator() && read < requested; ++it) {
		auto datablock = *it;
		size_t remaining = requested - read;

		if(offset == 0 && remaining > size_t(block_size)) {
			// Copy directly into dest
			ssize_t res = ::pread(blockdev, dest + read, block_size, block_size * datablock);
			check_ssize(res, block_size);
			read += block_size;
		} else {
			// Partial block copy
			char contents[block_size];
			ssize_t res = ::pread(blockdev, contents, block_size, block_size * datablock);
			check_ssize(res, block_size);

			size_t to_copy = std::min(remaining, block_size - size_t(offset));
			memcpy(dest + read, &contents[offset], to_copy);
			read += to_copy;
			assert(offset < block_size);
			offset = 0;
		}
	}
	assert(read <= requested);
	return read;
}

size_t extfs::pread(pseudofd_t pseudo, off_t offset, char *dest, size_t requested)
{
	auto entry = get_file_entry_from_pseudo(pseudo);

	if(entry->type != CLOUDABI_FILETYPE_REGULAR_FILE) {
		// Don't perform reads on non-files through the extfs
		throw cloudabi_system_error(EBADF);
	}

	entry->inode_data.atime = time(nullptr);
	write_inode(entry->inode, entry->inode_data);

	return pread(entry, offset, dest, requested);
}

void extfs::pwrite(file_entry_ptr entry, off_t offset, const char *buf, size_t requested)
{
	size_t size = entry->inode_data.size1;
	if(entry->type == CLOUDABI_FILETYPE_REGULAR_FILE) {
		size += (uint64_t(entry->inode_data.size2_or_dir_acl_blockptr) & 0xffffffff) << 32;
	}

	size_t data_end = offset + requested;
	if(data_end > size) {
		// Make sure we have enough blocks for the file to contain data_end
		allocate(entry, data_end);
		entry->inode_data.ctime = entry->inode_data.mtime = time(nullptr);
		write_inode(entry->inode, entry->inode_data);
		size = data_end;
	}

	size_t block_skip = offset / block_size;
	offset %= block_size;
	ext2_block_iterator it(blockdev, block_size, entry->inode_data);
	assert(it != ext2_block_iterator());

	// Skip blocks we don't need to pwrite
	for(; block_skip > 0; ++it, --block_skip) {
		assert(it != ext2_block_iterator());
	}

	size_t wrote = 0;
	// Write block-by-block, since they may not be in the same location on the disk
	// TODO: could optimize this by checking if the disk data is contiguous
	for(; it != ext2_block_iterator() && wrote < requested; ++it) {
		auto datablock = *it;
		size_t remaining = requested - wrote;

		if(offset == 0 && remaining > size_t(block_size)) {
			// Write directly from buf since we write a full block
			ssize_t res = ::pwrite(blockdev, buf + wrote, block_size, block_size * datablock);
			check_ssize(res, block_size);
			wrote += block_size;
		} else {
			// Overwrite buffer partially
			char contents[block_size];
			ssize_t res = ::pread(blockdev, contents, block_size, block_size * datablock);
			check_ssize(res, block_size);

			size_t to_copy = std::min(remaining, block_size - size_t(offset));
			memcpy(contents + offset, buf + wrote, to_copy);

			res = ::pwrite(blockdev, contents, block_size, block_size * datablock);
			check_ssize(res, block_size);
			wrote += to_copy;
			offset = 0;
		}
	}
	assert(wrote == requested);
}

void extfs::pwrite(pseudofd_t pseudo, off_t offset, const char *buf, size_t length)
{
	auto entry = get_file_entry_from_pseudo(pseudo);

	if(entry->type != CLOUDABI_FILETYPE_REGULAR_FILE) {
		// Don't perform writes on non-files through the extfs
		throw cloudabi_system_error(EBADF);
	}

	entry->inode_data.ctime = time(nullptr);
	write_inode(entry->inode, entry->inode_data);

	return pwrite(entry, offset, buf, length);
}

void extfs::datasync(pseudofd_t)
{
	// there's nothing to sync
}

void extfs::sync(pseudofd_t)
{
	// there's nothing to sync
}

bool extfs::readdir(file_entry_ptr directory, bool get_type, std::function<bool(cloudabi_dirent_t, std::string name, file_entry_ptr)> per_entry)
{
	if(directory->type != CLOUDABI_FILETYPE_DIRECTORY) {
		throw cloudabi_system_error(ENOTDIR);
	}

	bool continue_reading = true;

	ext2_block_iterator it(blockdev, block_size, directory->inode_data);
	for(; it != ext2_block_iterator(); ++it) {
		auto datablock = *it;
		char dircontents[block_size];
		ssize_t res = ::pread(blockdev, dircontents, block_size, block_size * datablock);
		check_ssize(res, sizeof(dircontents));

		char *ci = dircontents;
		auto entry = reinterpret_cast<ext2_direntry*>(ci);
		for(; ci < dircontents + block_size; ci += entry->size_of_entry) {
			entry = reinterpret_cast<ext2_direntry*>(ci);
			if(entry->inode == 0) {
				// removed entry
				continue;
			}

			if(!continue_reading) {
				// another entry was found, but the receiver said he's not interested anymore
				// stop here, and return that there were more entries
				return true;
			}

			cloudabi_dirent_t dirent;
			dirent.d_next = 0;
			dirent.d_ino = entry->inode;
			dirent.d_namlen = entry->namelen1;
			// TODO: if "directories have type byte" feature is set, write this differently
			dirent.d_namlen += (entry->type_or_namelen2 << 8);
			dirent.d_type = 0;

			assert(entry->size_of_entry >= sizeof(ext2_direntry) + dirent.d_namlen);

			std::string name(entry->name, dirent.d_namlen);

			file_entry_ptr in_entry;
			if(get_type) {
				try {
					in_entry = get_file_entry_from_inode(entry->inode);
					dirent.d_type = in_entry->type;
				} catch(cloudabi_system_error &e) {
					if(e.error == ENOENT) {
						fprintf(stderr, "[extfs] INODE %u DID NOT EXIST IN DIR (XXX)\n", entry->inode);
					} else {
						fprintf(stderr, "[extfs] UNEXPECTED ERROR CHECKING INODE %u (XXX): %s\n", entry->inode,
							strerror(e.error));
					}
				}
			}

			continue_reading = per_entry(dirent, name, in_entry);
		}
	}

	// end of entries reached
	return false;
}

/**
 * Reads the next entry from the given directory. If there are no more entries,
 * sets cookie to 0 and returns 0.
 */
size_t extfs::readdir(pseudofd_t pseudo, char *buffer, size_t buflen, cloudabi_dircookie_t &cookie)
{
	auto directory = get_file_entry_from_pseudo(pseudo);
	if(directory->type != CLOUDABI_FILETYPE_DIRECTORY) {
		throw cloudabi_system_error(ENOTDIR);
	}

	directory->inode_data.atime = time(nullptr);
	write_inode(directory->inode, directory->inode_data);

	cloudabi_dircookie_t skipped_entries = 0;
	size_t copied = 0;

	bool more_entries = readdir(directory, true, [&](cloudabi_dirent_t dirent, std::string name, file_entry_ptr) -> bool {
		if(buflen == copied) {
			// no more space for other entries
			return false;
		}

		if(skipped_entries++ == cookie) {
			dirent.d_next = cookie + 1;

			size_t to_copy = std::min(sizeof(cloudabi_dirent_t), buflen - copied);
			memcpy(buffer + copied, &dirent, to_copy);
			copied += to_copy;
			if(buflen > copied) {
				to_copy = std::min(dirent.d_namlen, buflen - copied);
				memcpy(buffer + copied, name.c_str(), to_copy);
				copied += to_copy;
			}

			cookie++;

			if(buflen == copied) {
				// no more space for other entries
				return false;
			}
		}

		return true;
	});

	if(!more_entries) {
		// We're at the end of the directory, make cookie 0 again
		cookie = 0;
	}
	return copied;
}

static void file_entry_to_filestat(file_entry_ptr const &entry, cloudabi_filestat_t *buf) {
	buf->st_dev = entry->device;
	buf->st_ino = entry->inode;
	buf->st_filetype = entry->type;
	buf->st_nlink = entry->inode_data.nlink;
	buf->st_size = entry->inode_data.size1;
	if(entry->type == CLOUDABI_FILETYPE_REGULAR_FILE) {
		buf->st_size += (uint64_t(entry->inode_data.size2_or_dir_acl_blockptr) & 0xffffffff) << 32;
	}
	buf->st_atim = uint64_t(entry->inode_data.atime) * 1'000'000'000LLU;
	buf->st_mtim = uint64_t(entry->inode_data.ctime) * 1'000'000'000LLU;
	buf->st_ctim = uint64_t(entry->inode_data.mtime) * 1'000'000'000LLU;
}

void extfs::stat_get(pseudofd_t pseudo, cloudabi_lookupflags_t flags, char *path, size_t len, cloudabi_filestat_t *buf) {
	auto file_entry = lookup(pseudo, path, len, flags);
	file_entry_to_filestat(get_file_entry_from_inode(file_entry.inode), buf);
}

void extfs::stat_fget(pseudofd_t pseudo, cloudabi_filestat_t *buf) {
	file_entry_ptr entry = get_file_entry_from_pseudo(pseudo);
	file_entry_to_filestat(entry, buf);
}

void extfs::update_file_entry_stat(file_entry_ptr entry, const cloudabi_filestat_t *buf, cloudabi_fsflags_t fsflags)
{
	auto now = time(NULL);
	entry->inode_data.mtime = now;

	if(fsflags & CLOUDABI_FILESTAT_SIZE) {
		if(entry->type != CLOUDABI_FILETYPE_REGULAR_FILE) {
			throw cloudabi_system_error(EINVAL);
		}

		size_t size = entry->inode_data.size1;
		size += (uint64_t(entry->inode_data.size2_or_dir_acl_blockptr) & 0xffffffff) << 32;

		if(size < buf->st_size) {
			allocate(entry, buf->st_size);
		} else if(size > buf->st_size) {
			// file is too large.
			// TODO: deallocate unnecessary blocks
			entry->inode_data.size1 = buf->st_size & 0xffffffff;
			entry->inode_data.size2_or_dir_acl_blockptr = (buf->st_size >> 32) & 0xffffffff;
		}

		entry->inode_data.ctime = now;
	}

	if(fsflags & CLOUDABI_FILESTAT_ATIM) {
		entry->inode_data.atime = buf->st_atim / 1'000'000'000LLU;
	}
	if(fsflags & CLOUDABI_FILESTAT_ATIM_NOW) {
		entry->inode_data.atime = now;
	}

	if(fsflags & CLOUDABI_FILESTAT_MTIM) {
		entry->inode_data.ctime = buf->st_mtim / 1'000'000'000LLU;
	}
	if(fsflags & CLOUDABI_FILESTAT_MTIM_NOW) {
		entry->inode_data.ctime = now;
	}

	write_inode(entry->inode, entry->inode_data);
}

void extfs::stat_fput(pseudofd_t pseudo, const cloudabi_filestat_t *buf, cloudabi_fsflags_t fsflags) {
	file_entry_ptr entry = get_file_entry_from_pseudo(pseudo);
	update_file_entry_stat(entry, buf, fsflags);
}

void extfs::stat_put(pseudofd_t pseudo, cloudabi_lookupflags_t lookupflags, const char *path, size_t pathlen, const cloudabi_filestat_t *buf, cloudabi_fsflags_t fsflags) {
	file_entry_ptr directory = get_file_entry_from_pseudo(pseudo);
	std::string filename = dereference_path(directory, std::string(path, pathlen), lookupflags);

	auto entry = lookup_nonrecursive(directory->inode, filename);
	auto entry_ptr = get_file_entry_from_inode(entry.inode);

	update_file_entry_stat(entry_ptr, buf, fsflags);
}

std::string extfs::dereference_path(file_entry_ptr &dir, std::string path, cloudabi_lookupflags_t lookupflags)
{
	auto dereferenced = dereference_path(dir->inode, path, lookupflags);
	dir = get_file_entry_from_inode(dereferenced.first);
	return dereferenced.second;
}

file_entry_ptr extfs::get_file_entry_from_inode(cloudabi_inode_t inode)
{
	assert(inode > 0 /* invalid inode value */);
	auto it = open_inodes.find(inode);
	if(it != open_inodes.end()) {
		auto weakptr = it->second;
		auto sharedptr = weakptr.lock();
		if(sharedptr) {
			// already open, return it!
			return sharedptr;
		}
	}

	size_t blockgroup = (inode - 1) / superblock->inodes_per_group;
	assert(blockgroup < number_of_block_groups);
	auto &descriptor = block_group_desc[blockgroup];

	size_t index = (inode - 1) % superblock->inodes_per_group;

	// Check in the bitmap table whether this inode exists
	{
		uint8_t bitmap[block_size];
		ssize_t res = ::pread(blockdev, bitmap, block_size, block_size * descriptor.inode_usage_bitmap_addr);
		check_ssize(res, block_size);

		size_t byte = index / 8;
		uint8_t bit = 1 << (index % 8);
		if((bitmap[byte] & bit) == 0) {
			throw cloudabi_system_error(ENOENT);
		}
	}

	// TODO: we should not use sizeof(ext2_inode) but instead superblock->inode_size; for now
	// we assume the two are the same
	assert(sizeof(ext2_inode) == superblock->inode_size);

	// read a block of inode data
	size_t inodes_per_block = block_size / sizeof(ext2_inode);
	size_t inode_block = index / inodes_per_block;
	size_t index_in_block = index % inodes_per_block;

	ext2_inode block_inodes[(block_size / sizeof(ext2_inode)) + 1];
	ssize_t res = ::pread(blockdev, block_inodes, block_size, block_size * (descriptor.inode_table_addr + inode_block));
	check_ssize(res, block_size);

	assert(index_in_block < (sizeof(block_inodes) / sizeof(ext2_inode)));
	auto &inode_data = block_inodes[index_in_block];
	file_entry_ptr entry = std::make_shared<extfs_file_entry>(this, inode, device, inode_data);

	auto type = entry->inode_data.type_and_permissions & 0xf000;
	if(type == 0x2000) {
		entry->type = CLOUDABI_FILETYPE_CHARACTER_DEVICE;
	} else if(type == 0x4000) {
		entry->type = CLOUDABI_FILETYPE_DIRECTORY;
	} else if(type == 0x6000) {
		entry->type = CLOUDABI_FILETYPE_BLOCK_DEVICE;
	} else if(type == 0x8000) {
		entry->type = CLOUDABI_FILETYPE_REGULAR_FILE;
	} else if(type == 0xA000) {
		entry->type = CLOUDABI_FILETYPE_SYMBOLIC_LINK;
	} else {
		entry->type = CLOUDABI_FILETYPE_UNKNOWN;
	}

	open_inodes[inode] = entry;
	return entry;
}

file_entry_ptr extfs::get_file_entry_from_pseudo(pseudofd_t pseudo)
{
	auto it = pseudo_fds.find(pseudo);
	if(it != pseudo_fds.end()) {
		return it->second->file;
	} else {
		throw cloudabi_system_error(EBADF);
	}
}

bool extfs::is_readable(pseudofd_t pseudo, size_t &nbytes, bool &hangup) {
	auto entry = get_file_entry_from_pseudo(pseudo);

	if(entry->type != CLOUDABI_FILETYPE_REGULAR_FILE) {
		throw cloudabi_system_error(EINVAL);
	}

	size_t size = entry->inode_data.size1;
	size += (uint64_t(entry->inode_data.size2_or_dir_acl_blockptr) & 0xffffffff) << 32;

	// TODO: what is pos of pseudo, so we can see the remaining bytes in this file?
	nbytes = size;
	hangup = false;
	return true;
}

void extfs::deallocate_block(size_t b) {
	size_t blockgroup = (b - first_block) / superblock->blocks_per_group;
	size_t block = (b - first_block) % superblock->blocks_per_group;

	// Unset this bit in the bitmask
	assert(blockgroup < number_of_block_groups);
	auto &descriptor = block_group_desc[blockgroup];
	{
		uint8_t bitmap[block_size];
		ssize_t res = ::pread(blockdev, bitmap, block_size, block_size * descriptor.block_usage_bitmap_addr);
		check_ssize(res, block_size);

		size_t byte = block / 8;
		uint8_t bit = 1 << (block % 8);
		assert((bitmap[byte] & bit) != 0);
		bitmap[byte] &= ~bit;
		res = ::pwrite(blockdev, bitmap, block_size, block_size * descriptor.block_usage_bitmap_addr);
		check_ssize(res, block_size);
	}

	assert(descriptor.num_unallocated_blocks < superblock->blocks_per_group);
	assert(superblock->num_unallocated_blocks < superblock->num_blocks);
	descriptor.num_unallocated_blocks += 1;
	superblock->num_unallocated_blocks += 1;

	// Write updated block group descriptor
	pwrite_partial(blockdev, block_group_desc, &descriptor, sizeof(descriptor), block_group_desc_offset);

	// Write updated superblock
	pwrite_partial(blockdev, superblock, int(0), sizeof(ext2_superblock), superblock_offset);
}

size_t extfs::allocate_block() {
	// TODO: is it OK to take blocks from a different block group? I guess so, otherwise
	// that would provide an arbitrary limit on files in a block group

	// TODO: a search from the first block group may be slow, because quickly the first
	// block groups will be filled up. Perhaps start with the block group the inode is
	// in, but allow looping around to 0.

	bool free_block_found = false;
	size_t blockgroup = 0;
	size_t block = 0;

	// if the next is untrue, the bitmap would span more than one block
	assert(superblock->blocks_per_group <= uint64_t(8 * block_size));

	for(blockgroup = 0; blockgroup < number_of_block_groups; ++blockgroup) {
		auto &descriptor = block_group_desc[blockgroup];
		uint8_t bitmap[block_size];
		ssize_t res = ::pread(blockdev, bitmap, block_size, block_size * descriptor.block_usage_bitmap_addr);
		check_ssize(res, block_size);

		// find an unused block in this bitmap
		for(size_t i = 0; i < size_t(block_size); ++i) {
			if(bitmap[i] == 0xff) {
				continue;
			}
			for(int j = 0; j < 8; ++j) {
				if(~bitmap[i] & (1 << j)) {
					// free block found
					block = i * 8 + j + first_block /* offset of the first block group */;
					free_block_found = true;
					break;
				}
			}
			if(free_block_found) {
				break;
			}
		}

		if(free_block_found) {
			break;
		}
	}

	if(!free_block_found) {
		throw cloudabi_system_error(ENOSPC);
	}

	// Set this bit in the bitmask
	assert(blockgroup < number_of_block_groups);
	auto &descriptor = block_group_desc[blockgroup];
	{
		uint8_t bitmap[block_size];
		ssize_t res = ::pread(blockdev, bitmap, block_size, block_size * descriptor.block_usage_bitmap_addr);
		check_ssize(res, block_size);

		size_t byte = (block - 1) / 8;
		uint8_t bit = 1 << ((block - 1) % 8);
		assert((bitmap[byte] & bit) == 0);
		bitmap[byte] |= bit;
		res = ::pwrite(blockdev, bitmap, block_size, block_size * descriptor.block_usage_bitmap_addr);
		check_ssize(res, block_size);
	}

	assert(descriptor.num_unallocated_blocks > 0);
	assert(superblock->num_unallocated_blocks > 0);
	descriptor.num_unallocated_blocks -= 1;
	superblock->num_unallocated_blocks -= 1;

	// Overwrite block with zeroes
	block += blockgroup * superblock->blocks_per_group;
	char zeroes[block_size];
	memset(zeroes, 0, block_size);
	ssize_t res = ::pwrite(blockdev, zeroes, block_size, block_size * block);
	check_ssize(res, block_size);

	// Write updated block group descriptor
	pwrite_partial(blockdev, block_group_desc, &descriptor, sizeof(descriptor), block_group_desc_offset);

	// Write updated superblock
	pwrite_partial(blockdev, superblock, int(0), sizeof(ext2_superblock), superblock_offset);

	return block;
}

void extfs::write_inode(cloudabi_inode_t inode, ext2_inode &inode_data) {
	size_t blockgroup = (inode - 1) / superblock->inodes_per_group;
	assert(blockgroup < number_of_block_groups);
	auto &descriptor = block_group_desc[blockgroup];
	size_t index = (inode - 1) % superblock->inodes_per_group;

	// TODO: we should not use sizeof(ext2_inode) but instead superblock->inode_size; for now
	// we assume the two are the same
	assert(sizeof(ext2_inode) == superblock->inode_size);

	size_t inodes_per_block = block_size / sizeof(ext2_inode);
	size_t inode_block = index / inodes_per_block;
	size_t index_in_block = index % inodes_per_block;

	ext2_inode block_inodes[(block_size / sizeof(ext2_inode)) + 1];
	ssize_t res = ::pread(blockdev, block_inodes, block_size, block_size * (descriptor.inode_table_addr + inode_block));
	check_ssize(res, block_size);

	assert(sizeof(block_inodes[0]) * (index_in_block + 1) <= sizeof(block_inodes));
	block_inodes[index_in_block] = inode_data;

	pwrite_partial(blockdev, block_inodes, block_inodes + index_in_block, sizeof(ext2_inode), block_size * (descriptor.inode_table_addr + inode_block));
}

void extfs::add_entry_into_directory(file_entry_ptr directory, std::string filename, cloudabi_inode_t new_inode) {
	assert(directory->type == CLOUDABI_FILETYPE_DIRECTORY);

	size_t const max_filename_length = block_size - sizeof(ext2_direntry);
	if(filename.size() > max_filename_length) {
		filename.resize(max_filename_length);
	}

	size_t entry_size_needed = sizeof(ext2_direntry) + filename.size();
	assert(entry_size_needed <= block_size);

	bool direntry_written = false;
	ext2_block_iterator it(blockdev, block_size, directory->inode_data);
	for(; it != ext2_block_iterator(); ++it) {
		auto datablock = *it;
		char dircontents[block_size];
		ssize_t res = ::pread(blockdev, dircontents, block_size, block_size * datablock);
		check_ssize(res, sizeof(dircontents));

		char *ci = dircontents;
		ext2_direntry *entry = nullptr;
		for(; ci < dircontents + block_size; ci += entry->size_of_entry) {
			entry = reinterpret_cast<ext2_direntry*>(ci);
			// TODO: if "directories have type byte" feature is set, write this differently

			size_t actual_size = sizeof(ext2_direntry) + entry->namelen1;
			actual_size += (entry->type_or_namelen2 << 8);
			// Align to 4 bytes
			if(actual_size % 4) {
				actual_size += 4 - (actual_size % 4);
			}

			if(entry->inode == 0 && entry->size_of_entry >= entry_size_needed) {
				// cannibalize this entry
				// TODO: does inode == 0 actually happen, or is the direntry just removed in that case?
			} else if(entry->size_of_entry - actual_size >= entry_size_needed) {
				// create an extra entry here
				size_t new_entry_size = entry->size_of_entry - actual_size;

				entry->size_of_entry = actual_size;
				ci += entry->size_of_entry;
				entry = reinterpret_cast<ext2_direntry*>(ci);
				entry->size_of_entry = new_entry_size;
			} else {
				// doesn't fit here
				continue;
			}

			assert(entry->size_of_entry >= entry_size_needed);
			assert(entry->size_of_entry >= sizeof(ext2_direntry) + filename.size());

			// reuse this entry
			entry->inode = new_inode;
			// TODO: if "directories have type byte" feature is set, write this differently
			entry->namelen1 = filename.size() & 0xff;
			entry->type_or_namelen2 = filename.size() >> 8;
			memcpy(entry->name, filename.c_str(), filename.size());
			direntry_written = true;
			break;
		}

		// File entries must span to the end of the block. If this isn't true, the filesystem
		// is corrupt.
		assert(direntry_written || ci == dircontents + block_size);

		if(direntry_written) {
			res = ::pwrite(blockdev, dircontents, block_size, block_size * datablock);
			check_ssize(res, sizeof(dircontents));
			break;
		}
	}

	if(!direntry_written) {
		it.assign_new_block([&]() -> int {
			return allocate_block();
		});
		assert(it != ext2_block_iterator());
		ext2_block_iterator next = it;
		++next;
		assert(next == ext2_block_iterator());

		auto datablock = *it;
		directory->inode_data.size1 += block_size;
		write_inode(directory->inode, directory->inode_data);

		char dircontents[block_size];
		ext2_direntry *entry = reinterpret_cast<ext2_direntry*>(dircontents);
		entry->inode = new_inode;
		entry->size_of_entry = block_size;
		entry->namelen1 = filename.size() & 0xff;
		entry->type_or_namelen2 = filename.size() >> 8;
		assert(entry->size_of_entry >= sizeof(ext2_direntry) + filename.size());
		memcpy(entry->name, filename.c_str(), filename.size());
		memset(entry->name + filename.size(), 0, sizeof(dircontents) - sizeof(ext2_direntry) - filename.size());
		ssize_t res = ::pwrite(blockdev, dircontents, block_size, block_size * datablock);
		check_ssize(res, sizeof(dircontents));
	}
}

void extfs::remove_entry_from_directory(file_entry_ptr directory, std::string filename) {
	assert(directory->type == CLOUDABI_FILETYPE_DIRECTORY);

	bool entry_removed = false;
	ext2_block_iterator it(blockdev, block_size, directory->inode_data);
	for(; it != ext2_block_iterator(); ++it) {
		auto datablock = *it;
		char dircontents[block_size];
		ssize_t res = ::pread(blockdev, dircontents, block_size, block_size * datablock);
		check_ssize(res, sizeof(dircontents));

		char *ci = dircontents;
		ext2_direntry *entry = nullptr;
		ext2_direntry *prev_entry = nullptr;
		for(; ci < dircontents + block_size; ci += entry->size_of_entry) {
			entry = reinterpret_cast<ext2_direntry*>(ci);
			// TODO: if "directories have type byte" feature is set, write this differently

			size_t namelen = entry->namelen1 + (entry->type_or_namelen2 << 8);
			if(namelen == filename.size() && memcmp(entry->name, filename.c_str(), namelen) == 0) {
				if(ci == dircontents && entry->size_of_entry == block_size) {
					// we're removing the last entry in this block
					// TODO: should deallocate this block. Instead, for now, we mark the entry
					// invalid by setting inode to 0.
					entry->inode = 0;
					entry->namelen1 = 0;
					entry->type_or_namelen2 = 0;
				} else if(ci == dircontents) {
					// we're removing the first entry, but there is an entry after this;
					// move it over this one
					assert(entry->size_of_entry < block_size - sizeof(ext2_direntry));
					ext2_direntry *next_entry = reinterpret_cast<ext2_direntry*>(ci + entry->size_of_entry);
					size_t new_size = entry->size_of_entry + next_entry->size_of_entry;
					size_t new_namelen = next_entry->namelen1 + (next_entry->type_or_namelen2 << 8);
					assert(new_size >= sizeof(ext2_direntry) + new_namelen);
					assert(new_namelen <= next_entry->size_of_entry - sizeof(ext2_direntry));
					*entry = *next_entry;
					entry->size_of_entry = new_size;
					// if this entry used to be very short, then the new length of entry->name
					// may overlap with next_entry->name, so use memmove instead of memcpy here
					memmove(entry->name, next_entry->name, new_namelen);
				} else {
					assert(prev_entry != nullptr);
					prev_entry->size_of_entry += entry->size_of_entry;
				}
				entry_removed = true;
				break;
			}
			prev_entry = entry;
		}

		// File entries must span to the end of the block. If this isn't true, the filesystem
		// is corrupt.
		assert(entry_removed || ci == dircontents + block_size);

		if(entry_removed) {
			res = ::pwrite(blockdev, dircontents, block_size, block_size * datablock);
			check_ssize(res, sizeof(dircontents));
			break;
		}
	}

	if(!entry_removed) {
		fprintf(stderr, "[extfs] Remove_entry called for an entry that doesn't exist?\n");
		throw cloudabi_system_error(ENOENT);
	}
}

bool extfs::directory_is_empty(file_entry_ptr directory)
{
	bool is_empty = true;
	readdir(directory, false, [&](cloudabi_dirent_t, std::string n, file_entry_ptr) -> bool {
		assert(is_empty);
		if(n != "." && n != "..") {
			is_empty = false;
			return false;
		}
		return true;
	});
	return is_empty;
}

void extfs::deallocate_inode(cloudabi_inode_t inode, cloudabi_filetype_t type, ext2_inode &inode_data)
{
	assert(inode_data.nlink == 0);

	// first, deallocate all blocks
	if(type != CLOUDABI_FILETYPE_SYMBOLIC_LINK) {
		ext2_block_iterator it(blockdev, block_size, inode_data);
		for(; it != ext2_block_iterator(); ++it) {
			deallocate_block(*it);
		}
	}

	memset(&inode_data, 0, sizeof(inode_data));
	inode_data.ctime = inode_data.mtime = inode_data.dtime = time(nullptr);
	write_inode(inode, inode_data);

	// then, remove the inode in the bitmap
	size_t blockgroup = (inode - 1) / superblock->inodes_per_group;
	assert(blockgroup < number_of_block_groups);
	auto &descriptor = block_group_desc[blockgroup];
	size_t index = (inode - 1) % superblock->inodes_per_group;

	uint8_t bitmap[block_size];
	ssize_t res = ::pread(blockdev, bitmap, block_size, block_size * descriptor.inode_usage_bitmap_addr);
	check_ssize(res, block_size);

	size_t byte = index / 8;
	uint8_t bit = 1 << (index % 8);
	assert((bitmap[byte] & bit) == bit);
	bitmap[byte] &= ~bit;
	res = ::pwrite(blockdev, bitmap, block_size, block_size * descriptor.inode_usage_bitmap_addr);
	check_ssize(res, block_size);

	descriptor.num_unallocated_inodes += 1;
	if(type == CLOUDABI_FILETYPE_DIRECTORY) {
		descriptor.num_directories += 1;
	}
	superblock->num_unallocated_inodes += 1;

	// Write updated block group descriptor
	pwrite_partial(blockdev, block_group_desc, &descriptor, sizeof(descriptor), block_group_desc_offset);

	// Write updated superblock
	pwrite_partial(blockdev, superblock, int(0), sizeof(ext2_superblock), superblock_offset);
}

void extfs::allocate(file_entry_ptr entry, size_t size) {
	ext2_block_iterator it(blockdev, block_size, entry->inode_data);
	size_t has_size = 0;
	while(has_size < size) {
		if(it == ext2_block_iterator()) {
			// this block doesn't exist, but we don't have enough place yet, so allocate it
			it.assign_new_block([&]() -> int {
				return allocate_block();
			});
			assert(it != ext2_block_iterator());
			ext2_block_iterator next = it;
			++next;
			assert(next == ext2_block_iterator());
		}
		has_size += 1024;
		++it;
	}

	// Update the inode struct
	entry->inode_data.size1 = size & 0xffffffff;
	if(entry->type == CLOUDABI_FILETYPE_REGULAR_FILE) {
		entry->inode_data.size2_or_dir_acl_blockptr = (uint64_t(size) >> 32) & 0xffffffff;
	}
}
