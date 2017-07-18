#include <fd/initrdfs.hpp>
#include <fd/memory_fd.hpp>
#include <global.hpp>
#include <hw/multiboot.hpp>
#include <oslibc/numeric.h>
#include <userland/external_binaries.h>

using namespace cloudos;

namespace cloudos {

static const int INITRDFS_PATH_MAX = 100; /* longer paths can't exist in a tar file */

struct initrdfs_directory_fd : fd_t {
	initrdfs_directory_fd(uint8_t *st, size_t sz, const char *s, cloudabi_inode_t i, const char *n)
	: fd_t(CLOUDABI_FILETYPE_DIRECTORY, n)
	, initrd_start(st)
	, initrd_size(sz)
	, inode(i)
	{
		strncpy(subpath, s, sizeof(subpath));
	}

	shared_ptr<fd_t> openat(const char * /*path */, size_t /*pathlen*/, cloudabi_oflags_t /*oflags*/, const cloudabi_fdstat_t * /*fdstat*/) override;
	size_t readdir(char * /*buf*/, size_t /*nbyte*/, cloudabi_dircookie_t /*cookie*/) override;
	void file_stat_fget(cloudabi_filestat_t *buf) override;
	cloudabi_inode_t file_create(const char * /*path*/, size_t /*pathlen*/, cloudabi_filetype_t /*type*/) override;

private:
	char subpath[INITRDFS_PATH_MAX];
	uint8_t *initrd_start;
	size_t initrd_size;
	cloudabi_inode_t inode;
};

struct initrdfs_file_fd : public memory_fd {
	initrdfs_file_fd(uint8_t *a, size_t l, cloudabi_inode_t i, const char *n)
	: memory_fd(a, l, n, i)
	{}
};

}

struct header_posix_ustar {
	char name[100];
	char mode[8];
	char uid[8];
	char gid[8];
	char size[12];
	char mtime[12];
	char checksum[8];
	char typeflag[1];
	char linkname[100];
	char magic[6];
	char version[2];
	char uname[32];
	char gname[32];
	char devmajor[8];
	char devminor[8];
	char prefix[155];
	char pad[12];
};

template <typename Callable>
static cloudabi_errno_t for_every_initrd_file(uint8_t *initrd_start, size_t initrd_size, Callable &&f) {
	auto *header = reinterpret_cast<header_posix_ustar*>(initrd_start);
	void *end = reinterpret_cast<uint8_t*>(initrd_start) + initrd_size;
	while((header + 1) <= end /* one more header fits */) {
		size_t namelen = strnlen(header->name, sizeof(header->name));
		if(header->name[namelen] != 0) {
			get_vga_stream() << "Illegal initrd encountered: filename is not null terminated\n";
			return EIO;
		}
		size_t sizelen = strnlen(header->size, sizeof(header->size));
		if(header->size[sizelen] != 0 && header->size[sizelen] != ' ') {
			get_vga_stream() << "Illegal initrd encountered: size is not null or space terminated\n";
			return EIO;
		}

		header->size[sizelen] = 0;

		// convert octal to decimal
		size_t filesize = 0;
		for(size_t i = 0; i < sizelen; ++i) {
			uint8_t digit = header->size[i] - '0';
			if(digit >= 8) {
				get_vga_stream() << "Illegal initrd encountered: size not octal, returning error\n";
				return EIO;
			}
			filesize = filesize * 8 + digit;
		}

		if(reinterpret_cast<uint8_t*>(header + 1) + filesize > end) {
			/* this data doesn't fit anymore */
			get_vga_stream() << "Illegal initrd encountered: stopping before end of data, returning error\n";
			return EIO;
		}

		// Remove trailing slashes
		while(namelen > 0 && header->name[namelen-1] == '/') {
			header->name[--namelen] = 0;
		}

		bool stop = f(header, filesize);
		if(stop) {
			return 0;
		}

		// next entry
		if(filesize == 0) {
			header++;
		} else {
			size_t pad = filesize % 0x200;
			if(pad != 0) {
				pad = 0x200 - pad;
			}
			header = reinterpret_cast<header_posix_ustar*>(reinterpret_cast<char*>(header + 1) + filesize + pad);
		}
	}
	return 0;
}

initrdfs::initrdfs(multiboot_module *initrd)
{
	if(initrd == nullptr) {
		get_vga_stream() << "Not loading initrd.\n";
		return;
	}

	get_vga_stream() << "Loading initrd.\n";

	initrd_start = reinterpret_cast<uint8_t*>(initrd->mmo_start);
	initrd_size = initrd->mmo_end - initrd->mmo_start;

	// Check the file magic
	auto *magic = initrd_start + 0x101;
	if(memcmp(magic, "ustar", 5) != 0) {
		initrd_start = nullptr;
		initrd_size = 0;
	}
}

shared_ptr<fd_t> initrdfs::get_root_fd() {
	return make_shared<initrdfs_directory_fd>(initrd_start, initrd_size, "", 0, "initrdfs_root");
}

shared_ptr<fd_t> initrdfs_directory_fd::openat(const char *pathname, size_t pathlen, cloudabi_oflags_t, const cloudabi_fdstat_t *) {
	if(pathname == nullptr || pathname[0] == 0 || pathname[0] == '/') {
		error = EINVAL;
		return nullptr;
	}

	if(initrd_start == nullptr) {
		error = ENOENT;
		return nullptr;
	}

	char fullpath[INITRDFS_PATH_MAX];
	if(strlen(subpath) + pathlen + 1 /* slash */ + 1 /* terminator */ > sizeof(fullpath)) {
		// path doesn't fit
		error = ENAMETOOLONG;
		return nullptr;
	}

	if(subpath[0] == 0) {
		strncpy(fullpath, pathname, pathlen);
		fullpath[pathlen] = 0;
	} else {
		size_t subpathlen = strlen(subpath);
		strncpy(fullpath, subpath, sizeof(fullpath));
		strlcat(fullpath, "/", sizeof(fullpath));
		strncpy(fullpath + subpathlen + 1, pathname, pathlen);
		fullpath[subpathlen + pathlen + 1] = 0;
	}

	pathlen = strlen(fullpath);
	while(pathlen > 0 && fullpath[pathlen-1] == '/') {
		// remove slashes at the end
		fullpath[--pathlen] = 0;
	}

	// TODO: check oflags and fdstat_t

	// Walk through the initrd and find a file with this path
	size_t filenum = 1;
	shared_ptr<fd_t> fd;
	error = for_every_initrd_file(initrd_start, initrd_size, [&](header_posix_ustar *header, size_t filesize) {
		++filenum;
		bool is_file = header->typeflag[0] == 0 || header->typeflag[0] == '0';
		bool is_directory = header->name[strlen(header->name)-1] == '/' || header->typeflag[0] == '5';
		bool is_exact_match = strncmp(header->name, fullpath, sizeof(header->name)) == 0;

		if(is_directory && is_exact_match) {
			fd = make_shared<initrdfs_directory_fd>(initrd_start, initrd_size, fullpath, filenum, "initrd_directory");
			return true;
		}

		if(is_file && is_exact_match) {
			fd = make_shared<initrdfs_file_fd>(reinterpret_cast<uint8_t*>(header + 1), filesize, filenum, "initrd/");
			strlcat(fd->name, fullpath, sizeof(fd->name));
			return true;
		}

		// check if this is a file within the directory being opened, i.e. the header->name
		// contains the fullpath and the character after it is '/'
		if(pathlen < sizeof(header->name) && memcmp(header->name, fullpath, pathlen) == 0 && header->name[pathlen] == '/') {
			fd = make_shared<initrdfs_directory_fd>(initrd_start, initrd_size, fullpath, filenum, "initrd_directory");
			return true;
		}

		return false;
	});
	if(error != 0) {
		return nullptr;
	}
	if(fd) {
		return fd;
	}

	error = ENOENT;
	return nullptr;
}


size_t initrdfs_directory_fd::readdir(char *buf, size_t nbyte, cloudabi_dircookie_t cookie) {
	size_t subpathlen = strlen(subpath);

	size_t filenum = 1;
	size_t entrynum = 0;
	size_t written = 0;
	error = for_every_initrd_file(initrd_start, initrd_size, [&](header_posix_ustar *header, size_t) {
		++filenum;
		// is this entry within the search directory?
		if(subpathlen >= sizeof(header->name) || memcmp(header->name, subpath, subpathlen) != 0 || header->name[subpathlen] != '/') {
			// no
			return false;
		}

		// TODO: perhaps skip additional slashes?

		// is this entry /directly/ within the search directory?
		for(size_t i = subpathlen + 1; i < sizeof(header->name); ++i) {
			if(header->name[i] == '/') {
				// not directly within search directory
				return false;
			}
			if(header->name[i] == 0) {
				break;
			}
		}

		if(entrynum < cookie) {
			// skip this entry though, we already returned it
			entrynum++;
			return false;
		}

		bool is_file = header->typeflag[0] == 0 || header->typeflag[0] == '0';
		bool is_directory = header->name[strlen(header->name)-1] == '/' || header->typeflag[0] == '5';

		// return a dirent for this entry!
		cloudabi_dirent_t dirent;
		dirent.d_next = ++entrynum;
		dirent.d_ino = filenum;
		if(subpathlen == 0) {
			dirent.d_namlen = strlen(header->name);
		} else {
			dirent.d_namlen = strlen(header->name) - subpathlen - 1 /* slash */;
		}
		dirent.d_type = is_directory ? CLOUDABI_FILETYPE_DIRECTORY : is_file ? CLOUDABI_FILETYPE_REGULAR_FILE : CLOUDABI_FILETYPE_UNKNOWN;

		size_t copy = sizeof(dirent) < nbyte ? sizeof(dirent) : nbyte;
		memcpy(buf, &dirent, copy);
		buf += copy;
		written += copy;
		nbyte -= copy;

		copy = dirent.d_namlen < nbyte ? dirent.d_namlen : nbyte;
		if(subpathlen == 0) {
			memcpy(buf, header->name, copy);
		} else {
			memcpy(buf, header->name + subpathlen + 1, copy);
		}
		buf += copy;
		written += copy;
		nbyte -= copy;

		// are we done now?
		return nbyte == 0;
	});
	return written;
}

void initrdfs_directory_fd::file_stat_fget(cloudabi_filestat_t *buf) {
	buf->st_dev = device;
	buf->st_ino = inode;
	buf->st_filetype = type;
	buf->st_nlink = 0;
	buf->st_size = 0;
	buf->st_atim = 0;
	buf->st_mtim = 0;
	buf->st_ctim = 0;
	error = 0;
}

cloudabi_inode_t initrdfs_directory_fd::file_create(const char * /*path*/, size_t /*pathlen*/, cloudabi_filetype_t /*type*/) {
	error = EROFS;
	return 0;
}
