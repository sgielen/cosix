#include "tmpfs.hpp"
#include <errno.h>
#include <stdlib.h>
#include <cassert>
#include <cloudabi_syscalls.h>

using namespace cosix;

static cloudabi_timestamp_t timestamp() {
	cloudabi_timestamp_t ts;
	if(cloudabi_sys_clock_time_get(CLOUDABI_CLOCK_REALTIME, 1, &ts) == 0) {
		return ts;
	}
	return 0;
}

tmpfs::tmpfs(cloudabi_device_t d)
{
	device = d;

	// make directory entry /
	file_entry_ptr root(new tmpfs_file_entry);
	cloudabi_inode_t root_inode = reinterpret_cast<cloudabi_inode_t>(root.get());

	root->device = device;
	root->inode = root_inode;
	root->type = CLOUDABI_FILETYPE_DIRECTORY;
	inodes[root_inode] = root;

	root->access_time = root->content_time = root->metadata_time = timestamp();

	root->files["."] = root;
	root->files[".."] = root;

	pseudo_fd_ptr root_pseudo(new pseudo_fd_entry);
	root_pseudo->file = root;
	pseudo_fds[0] = root_pseudo;
}

file_entry tmpfs::lookup(pseudofd_t pseudo, const char *path, size_t len, cloudabi_lookupflags_t lookupflags)
{
	file_entry_ptr directory = get_file_entry_from_pseudo(pseudo);

	std::string filename = normalize_path(directory, path, len, lookupflags);
	auto it = directory->files.find(filename);
	directory->access_time = timestamp();
	if(it == directory->files.end()) {
		throw cloudabi_system_error(ENOENT);
	} else {
		return *it->second;
	}
}

pseudofd_t tmpfs::open(cloudabi_inode_t inode, cloudabi_oflags_t oflags)
{
	file_entry_ptr entry = get_file_entry_from_inode(inode);

	if(entry->type == CLOUDABI_FILETYPE_SYMBOLIC_LINK) {
		throw cloudabi_system_error(ELOOP);
	} else if(entry->type != CLOUDABI_FILETYPE_REGULAR_FILE && entry->type != CLOUDABI_FILETYPE_DIRECTORY) {
		// can't open via the tmpfs
		throw cloudabi_system_error(EINVAL);
	}

	entry->access_time = timestamp();

	if(oflags & CLOUDABI_O_TRUNC) {
		entry->contents.clear();
		entry->content_time = entry->metadata_time = timestamp();
	}

	pseudo_fd_ptr pseudo(new pseudo_fd_entry);
	pseudo->file = entry;
	pseudofd_t fd = reinterpret_cast<pseudofd_t>(pseudo.get());
	pseudo_fds[fd] = pseudo;
	return fd;
}

void tmpfs::link(pseudofd_t pseudo1, const char *path1, size_t path1len, cloudabi_lookupflags_t lookupflags, pseudofd_t pseudo2, const char *path2, size_t path2len) {
	file_entry_ptr dir1 = get_file_entry_from_pseudo(pseudo1);
	std::string filename1 = normalize_path(dir1, path1, path1len, lookupflags);

	auto it1 = dir1->files.find(filename1);
	if(it1 == dir1->files.end()) {
		throw cloudabi_system_error(ENOENT);
	} else if(it1->second->type == CLOUDABI_FILETYPE_DIRECTORY) {
		throw cloudabi_system_error(EPERM);
	}

	file_entry_ptr dir2 = get_file_entry_from_pseudo(pseudo2);
	std::string filename2 = normalize_path(dir2, path2, path2len, 0);

	auto it2 = dir2->files.find(filename2);
	if(it2 != dir2->files.end()) {
		throw cloudabi_system_error(EEXIST);
	}

	it1->second->hardlinks += 1;
	auto ts = timestamp();
	it1->second->metadata_time = ts;

	dir2->files[filename2] = it1->second;
	dir2->content_time = dir2->metadata_time = ts;
}

void tmpfs::allocate(pseudofd_t pseudo, off_t offset, off_t length) {
	file_entry_ptr file = get_file_entry_from_pseudo(pseudo);
	if(file->type != CLOUDABI_FILETYPE_REGULAR_FILE) {
		throw cloudabi_system_error(EINVAL);
	}

	size_t minsize = offset + length;
	if(file->contents.size() < minsize) {
		file->contents.resize(minsize);
		file->content_time = file->metadata_time = timestamp();
	}
}

size_t tmpfs::readlink(pseudofd_t pseudo, const char *path, size_t pathlen, char *buf, size_t buflen) {
	file_entry_ptr dir = get_file_entry_from_pseudo(pseudo);
	std::string filename = normalize_path(dir, path, pathlen, 0);
	auto it = dir->files.find(filename);
	if(it == dir->files.end()) {
		throw cloudabi_system_error(ENOENT);
	}

	if(it->second->type != CLOUDABI_FILETYPE_SYMBOLIC_LINK) {
		throw cloudabi_system_error(EINVAL);
	}

	size_t copy = std::min(it->second->contents.size(), buflen);
	memcpy(buf, it->second->contents.c_str(), copy);
	it->second->access_time = timestamp();
	return copy;
}

void tmpfs::rename(pseudofd_t pseudo1, const char *path1, size_t path1len, pseudofd_t pseudo2, const char *path2, size_t path2len)
{
	file_entry_ptr dir1 = get_file_entry_from_pseudo(pseudo1);
	file_entry_ptr dir2 = get_file_entry_from_pseudo(pseudo2);

	std::string filename1 = normalize_path(dir1, path1, path1len, 0);
	std::string filename2 = normalize_path(dir2, path2, path2len, 0);

	// TODO: check if dir1/fn1 is a parent of dir2/fn2 (i.e. directory would be moved inside itself)

	if(filename1 == "." || filename1 == "..") {
		throw cloudabi_system_error(EINVAL);
	}

	// source file exists?
	auto it1 = dir1->files.find(filename1);
	if(it1 == dir1->files.end()) {
		throw cloudabi_system_error(ENOENT);
	}

	file_entry_ptr entry = it1->second;

	// destination doesn't exist? -> rename file
	auto it2 = dir2->files.find(filename2);
	if(it2 == dir2->files.end()) {
		dir1->files.erase(it1);
		if(entry->type == CLOUDABI_FILETYPE_DIRECTORY) {
			entry->files[".."] = dir2;
		}
		dir2->files[filename2] = entry;
		auto ts = timestamp();
		dir1->content_time = dir1->metadata_time = ts;
		dir2->content_time = dir2->metadata_time = ts;
		entry->content_time = entry->metadata_time = ts;
		return;
	}

	// destination is directory? -> move directory
	if(it2->second->type == CLOUDABI_FILETYPE_DIRECTORY) {
		if(entry->type != CLOUDABI_FILETYPE_DIRECTORY) {
			throw cloudabi_system_error(EISDIR);
		}
		for(auto const &e : it2->second->files) {
			if(e.first != "." && e.first != "..") {
				throw cloudabi_system_error(ENOTEMPTY);
			}
		}
		dir2->files[filename2] = entry;
		entry->files[".."] = dir2;
		dir1->files.erase(it1);
		auto ts = timestamp();
		dir1->content_time = dir1->metadata_time = ts;
		dir2->content_time = dir2->metadata_time = ts;
		entry->content_time = entry->metadata_time = ts;
		return;
	}

	// both are non-directory? move file
	if(entry->type == CLOUDABI_FILETYPE_DIRECTORY) {
		throw cloudabi_system_error(ENOTDIR);
	}

	dir2->files[filename2] = entry;
	dir1->files.erase(it1);
	auto ts = timestamp();
	dir1->content_time = dir1->metadata_time = ts;
	dir2->content_time = dir2->metadata_time = ts;
}

void tmpfs::symlink(pseudofd_t pseudo ,const char *path1, size_t path1len, const char *path2, size_t path2len) {
	file_entry_ptr directory = get_file_entry_from_pseudo(pseudo);

	std::string filename = normalize_path(directory, path2, path2len, 0);
	auto it = directory->files.find(filename);
	if(it != directory->files.end()) {
		throw cloudabi_system_error(EEXIST);
	}

	file_entry_ptr entry(new tmpfs_file_entry);
	cloudabi_inode_t inode = reinterpret_cast<cloudabi_inode_t>(entry.get());
	entry->device = device;
	entry->inode = inode;
	entry->type = CLOUDABI_FILETYPE_SYMBOLIC_LINK;
	entry->contents = std::string(path1, path1len);

	entry->access_time = entry->content_time = entry->metadata_time = timestamp();

	inodes[inode] = entry;
	directory->files[filename] = entry;

	directory->content_time = directory->metadata_time = timestamp();
}

void tmpfs::unlink(pseudofd_t pseudo, const char *path, size_t len, cloudabi_ulflags_t unlinkflags)
{
	file_entry_ptr directory = get_file_entry_from_pseudo(pseudo);

	std::string filename = normalize_path(directory, path, len, 0);
	auto it = directory->files.find(filename);
	if(it == directory->files.end()) {
		throw cloudabi_system_error(ENOENT);
	}

	bool removedir = unlinkflags & CLOUDABI_UNLINK_REMOVEDIR;

	auto entry = it->second;
	if(entry->type == CLOUDABI_FILETYPE_DIRECTORY) {
		if(!removedir) {
			throw cloudabi_system_error(EPERM);
		}
		for(auto &file : entry->files) {
			if(file.first != "." && file.first != "..") {
				throw cloudabi_system_error(ENOTEMPTY);
			}
		}
	} else {
		if(removedir) {
			throw cloudabi_system_error(ENOTDIR);
		}
	}

	directory->files.erase(filename);
	auto ts = timestamp();
	directory->content_time = directory->metadata_time = ts;
	entry->hardlinks -= 1;
	entry->metadata_time = ts;
	if(entry->hardlinks == 0) {
		// TODO: erase inode if this was the last reference to it
		// (possibly, using the deleter of the inode bound to this tmpfs*)
	}
}

cloudabi_inode_t tmpfs::create(pseudofd_t pseudo, const char *path, size_t len, cloudabi_filetype_t type)
{
	file_entry_ptr directory = get_file_entry_from_pseudo(pseudo);

	std::string filename = normalize_path(directory, path, len, 0);
	auto it = directory->files.find(filename);
	if(it != directory->files.end()) {
		// TODO: only for directories, or if O_CREAT and O_EXCL are given?
		throw cloudabi_system_error(EEXIST);
	}

	file_entry_ptr entry(new tmpfs_file_entry);
	cloudabi_inode_t inode = reinterpret_cast<cloudabi_inode_t>(entry.get());
	entry->device = device;
	entry->inode = inode;
	entry->type = type;

	if(type == CLOUDABI_FILETYPE_DIRECTORY) {
		entry->files["."] = entry;
		entry->files[".."] = directory;
	}

	auto ts = timestamp();
	entry->access_time = entry->content_time = entry->metadata_time = ts;

	inodes[inode] = entry;
	directory->files[filename] = entry;
	directory->content_time = directory->metadata_time = ts;
	return inode;
}

void tmpfs::close(pseudofd_t pseudo)
{
	auto it = pseudo_fds.find(pseudo);
	if(it != pseudo_fds.end()) {
		pseudo_fds.erase(it);
	} else {
		throw cloudabi_system_error(EBADF);
	}
}

size_t tmpfs::pread(pseudofd_t pseudo, off_t offset, char *dest, size_t requested)
{
	auto entry = get_file_entry_from_pseudo(pseudo);

	if(entry->type != CLOUDABI_FILETYPE_REGULAR_FILE) {
		// Don't perform reads on non-files through the tmpfs
		throw cloudabi_system_error(EBADF);
	}

	if(offset > entry->contents.size()) {
		throw cloudabi_system_error(EINVAL /* The specified file offset is invalid */);
	}

	size_t remaining = entry->contents.size() - offset;
	size_t returned = remaining < requested ? remaining : requested;
	const char *data = entry->contents.c_str() + offset;
	memcpy(dest, data, returned);
	entry->access_time = timestamp();
	return returned;
}

void tmpfs::pwrite(pseudofd_t pseudo, off_t offset, const char *buf, size_t length)
{
	auto entry = get_file_entry_from_pseudo(pseudo);

	if(entry->type != CLOUDABI_FILETYPE_REGULAR_FILE) {
		// Don't perform writes on non-files through the tmpfs
		throw cloudabi_system_error(EBADF);
	}

	if(entry->contents.size() < offset + length) {
		entry->contents.resize(offset + length, 0);
	}

	entry->contents.replace(offset, length, std::string(buf, length));
	entry->content_time = timestamp();
}

void tmpfs::datasync(pseudofd_t)
{
	// there's nothing to sync
}

void tmpfs::sync(pseudofd_t)
{
	// there's nothing to sync
}

/**
 * Reads the next entry from the given directory. If there are no more entries,
 * sets cookie to 0 and returns 0.
 */
size_t tmpfs::readdir(pseudofd_t pseudo, char *buffer, size_t buflen, cloudabi_dircookie_t &cookie)
{
	auto directory = get_file_entry_from_pseudo(pseudo);
	if(directory->type != CLOUDABI_FILETYPE_DIRECTORY) {
		throw cloudabi_system_error(ENOTDIR);
	}

	// cookie is the index into the files map
	// TODO: if files are created or removed during a readdir, this may cause readdir to miss
	// files or return them twice
	auto it = directory->files.begin();
	directory->access_time = timestamp();
	for(auto c = cookie; c-- > 0 && it != directory->files.end(); ++it);
	if(it == directory->files.end()) {
		cookie = CLOUDABI_DIRCOOKIE_START;
		return 0;
	}

	std::string filename = it->first;
	file_entry_ptr entry = it->second;
	cookie += 1;

	cloudabi_dirent_t dirent;
	dirent.d_next = cookie;
	dirent.d_ino = entry->inode;
	dirent.d_namlen = filename.length();
	dirent.d_type = entry->type;

	size_t to_copy = sizeof(cloudabi_dirent_t) < buflen ? sizeof(cloudabi_dirent_t) : buflen;
	memcpy(buffer, &dirent, to_copy);
	if(buflen - to_copy > 0) {
		to_copy = dirent.d_namlen < buflen - to_copy ? dirent.d_namlen : buflen - to_copy;
		memcpy(buffer + sizeof(cloudabi_dirent_t), filename.c_str(), to_copy);
		return sizeof(cloudabi_dirent_t) + to_copy;
	}
	return to_copy;
}

static void file_entry_to_filestat(file_entry_ptr &entry, cloudabi_filestat_t *buf) {
	buf->st_dev = entry->device;
	buf->st_ino = entry->inode;
	buf->st_filetype = entry->type;
	buf->st_nlink = entry->hardlinks;
	if(entry->type == CLOUDABI_FILETYPE_REGULAR_FILE) {
		buf->st_size = entry->contents.size();
	} else {
		buf->st_size = 0;
	}
	buf->st_atim = entry->access_time;
	buf->st_mtim = entry->content_time;
	buf->st_ctim = entry->metadata_time;
}

void tmpfs::stat_get(pseudofd_t pseudo, cloudabi_lookupflags_t flags, char *path, size_t len, cloudabi_filestat_t *buf) {
	file_entry_ptr directory = get_file_entry_from_pseudo(pseudo);

	std::string filename = normalize_path(directory, path, len, flags);
	auto it = directory->files.find(filename);
	if(it == directory->files.end()) {
		throw cloudabi_system_error(ENOENT);
	}

	file_entry_to_filestat(it->second, buf);
}

void tmpfs::stat_fget(pseudofd_t pseudo, cloudabi_filestat_t *buf) {
	file_entry_ptr entry = get_file_entry_from_pseudo(pseudo);
	file_entry_to_filestat(entry, buf);
}

static void update_file_entry_stat(file_entry_ptr entry, const cloudabi_filestat_t *buf, cloudabi_fsflags_t fsflags) {
	auto ts = timestamp();
	entry->metadata_time = ts;

	if(fsflags & CLOUDABI_FILESTAT_SIZE) {
		if(entry->type != CLOUDABI_FILETYPE_REGULAR_FILE) {
			throw cloudabi_system_error(EINVAL);
		}

		entry->contents.resize(buf->st_size);
		entry->content_time = ts;
	}

	if(fsflags & CLOUDABI_FILESTAT_ATIM) {
		entry->access_time = buf->st_atim;
	}
	if(fsflags & CLOUDABI_FILESTAT_ATIM_NOW) {
		entry->access_time = ts;
	}

	if(fsflags & CLOUDABI_FILESTAT_MTIM) {
		entry->content_time = buf->st_mtim;
	}
	if(fsflags & CLOUDABI_FILESTAT_MTIM_NOW) {
		entry->content_time = ts;
	}
}

void tmpfs::stat_fput(pseudofd_t pseudo, const cloudabi_filestat_t *buf, cloudabi_fsflags_t fsflags) {
	file_entry_ptr entry = get_file_entry_from_pseudo(pseudo);
	update_file_entry_stat(entry, buf, fsflags);
}

void tmpfs::stat_put(pseudofd_t pseudo, cloudabi_lookupflags_t lookupflags, const char *path, size_t pathlen, const cloudabi_filestat_t *buf, cloudabi_fsflags_t fsflags) {
	file_entry_ptr directory = get_file_entry_from_pseudo(pseudo);
	std::string filename = normalize_path(directory, path, pathlen, lookupflags);
	auto it = directory->files.find(filename);
	if(it == directory->files.end()) {
		throw cloudabi_system_error(ENOENT);
	}

	update_file_entry_stat(it->second, buf, fsflags);
}

/** Normalizes the given path. When it returns normally, directory
 * points at the innermost directory pointed to by path. It returns the
 * filename that is to be opened, created or unlinked.
 *
 * It throws an error if the given file_entry ptr is not a directory,
 * if any of the path components don't reference a directory, or if the
 * path (eventually) points outside of the given file_entry.
 */
std::string tmpfs::normalize_path(file_entry_ptr &directory, const char *p, size_t len, cloudabi_lookupflags_t lookupflags)
{
	if(directory->type != CLOUDABI_FILETYPE_DIRECTORY) {
		throw cloudabi_system_error(ENOTDIR);
	}

	if(len == 0) {
		throw cloudabi_system_error(ENOENT);
	}

	if(p[0] == '/') {
		// no absolute paths allowed
		throw cloudabi_system_error(ENOTCAPABLE);
	}

	std::string path(p, len);

	// depth may not go under 0, because that means a file outside of the
	// given file_entry is referenced. A depth of 0 means that the file is
	// opened immediately in the given directory.
	int depth = 0;
	const int max_symlinks_followed = 30;
	int symlinks_followed = 0;
	do {
		size_t splitter = path.find('/');
		if(splitter == std::string::npos) {
			// filename component.
			if(path == ".." && depth == 0) {
				// allow "foo/..", but not "foo/../.."
				throw cloudabi_system_error(ENOTCAPABLE);
			}
			if(lookupflags & CLOUDABI_LOOKUP_SYMLINK_FOLLOW) {
				auto it = directory->files.find(path);
				if(it != directory->files.end()) {
					if(it->second->type == CLOUDABI_FILETYPE_SYMBOLIC_LINK) {
						// follow symlink
						if(++symlinks_followed >= max_symlinks_followed) {
							throw cloudabi_system_error(ELOOP);
						}
						path = it->second->contents;
						// continue with lookup; take current depth into account
						continue;
					}
				}
			}
			// done with lookup
			return path;
		}

		// path component; it must exist
		std::string component = path.substr(0, splitter);
		path = path.substr(splitter + 1);
		if(component.empty() || component == ".") {
			// no-op path component, just continue
			continue;
		}
		auto it = directory->files.find(component);
		if(it == directory->files.end()) {
			throw cloudabi_system_error(ENOENT);
		}
		auto entry = it->second;
		if(entry->type == CLOUDABI_FILETYPE_SYMBOLIC_LINK) {
			if(++symlinks_followed >= max_symlinks_followed) {
				throw cloudabi_system_error(ELOOP);
			}
			path = it->second->contents + "/" + path;
			// continue with lookup; take current depth into account
			continue;
		}
		if(entry->type != CLOUDABI_FILETYPE_DIRECTORY) {
			throw cloudabi_system_error(ENOTDIR);
		}
		if(component == "..") {
			if(depth == 0) {
				// don't allow going outside of the file descriptor
				throw cloudabi_system_error(ENOTCAPABLE);
			}
			depth--;
		} else {
			depth++;
		}
		if(path.empty()) {
			// this is actually the last component, don't enter it
			return component;
		}
		directory = entry;
	} while(!path.empty());

	/* unreachable code */
	assert(!"Unreachable");
	exit(123);
}

file_entry_ptr tmpfs::get_file_entry_from_inode(cloudabi_inode_t inode)
{
	auto it = inodes.find(inode);
	if(it != inodes.end()) {
		return it->second;
	} else {
		throw cloudabi_system_error(ENOENT);
	}
}

file_entry_ptr tmpfs::get_file_entry_from_pseudo(pseudofd_t pseudo)
{
	auto it = pseudo_fds.find(pseudo);
	if(it != pseudo_fds.end()) {
		return it->second->file;
	} else {
		throw cloudabi_system_error(EBADF);
	}
}
