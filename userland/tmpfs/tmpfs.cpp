#include "tmpfs.hpp"
#include <errno.h>
#include <stdlib.h>
#include <cassert>

using namespace cosix;

tmpfs::tmpfs(cloudabi_device_t d)
: filesystem(d)
{
	// make directory entry /
	file_entry_ptr root(new tmpfs_file_entry);
	cloudabi_inode_t root_inode = reinterpret_cast<cloudabi_inode_t>(root.get());

	root->device = get_device();
	root->inode = root_inode;
	root->type = CLOUDABI_FILETYPE_DIRECTORY;
	inodes[root_inode] = root;

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
	if(it == directory->files.end()) {
		throw filesystem_error(ENOENT);
	} else {
		return *it->second;
	}
}

pseudofd_t tmpfs::open(cloudabi_inode_t inode, int flags)
{
	file_entry_ptr entry = get_file_entry_from_inode(inode);
	pseudo_fd_ptr pseudo(new pseudo_fd_entry);
	pseudo->file = entry;
	pseudofd_t fd = reinterpret_cast<pseudofd_t>(pseudo.get());
	pseudo_fds[fd] = pseudo;
	return fd;
}

void tmpfs::unlink(pseudofd_t pseudo, const char *path, size_t len, cloudabi_ulflags_t unlinkflags)
{
	file_entry_ptr directory = get_file_entry_from_pseudo(pseudo);

	std::string filename = normalize_path(directory, path, len, 0 /* TODO: lookupflags */);
	auto it = directory->files.find(filename);
	if(it == directory->files.end()) {
		throw filesystem_error(ENOENT);
	}

	auto entry = it->second;
	directory->files.erase(filename);

	// TODO: erase inode if this was the last reference to it
	// (possibly, using the deleter of the inode bound to this tmpfs*)
}

cloudabi_inode_t tmpfs::create(pseudofd_t pseudo, const char *path, size_t len, cloudabi_filetype_t type)
{
	if(type != CLOUDABI_FILETYPE_DIRECTORY && type != CLOUDABI_FILETYPE_REGULAR_FILE) {
		// TODO: implement this for types other than directories
		throw filesystem_error(EINVAL);
	}

	file_entry_ptr directory = get_file_entry_from_pseudo(pseudo);

	std::string filename = normalize_path(directory, path, len, 0 /* TODO: lookup flags */);
	auto it = directory->files.find(filename);
	if(it != directory->files.end()) {
		// TODO: only for directories, or if O_CREAT and O_EXCL are given?
		throw filesystem_error(EEXIST);
	}

	file_entry_ptr entry(new tmpfs_file_entry);
	cloudabi_inode_t inode = reinterpret_cast<cloudabi_inode_t>(entry.get());
	entry->device = get_device();
	entry->inode = inode;
	entry->type = type;

	if(type == CLOUDABI_FILETYPE_DIRECTORY) {
		entry->files["."] = entry;
		entry->files[".."] = directory;
	}

	inodes[inode] = entry;
	directory->files[filename] = entry;
	return inode;
}

void tmpfs::close(pseudofd_t pseudo)
{
	auto it = pseudo_fds.find(pseudo);
	if(it != pseudo_fds.end()) {
		pseudo_fds.erase(it);
	} else {
		throw filesystem_error(EBADF);
	}
}

size_t tmpfs::pread(pseudofd_t pseudo, off_t offset, char *dest, size_t requested)
{
	auto entry = get_file_entry_from_pseudo(pseudo);
	if(offset > entry->contents.size()) {
		throw filesystem_error(EINVAL /* The specified file offset is invalid */);
	}

	size_t remaining = entry->contents.size() - offset;
	size_t returned = remaining < requested ? remaining : requested;
	const char *data = entry->contents.c_str() + offset;
	memcpy(dest, data, returned);
	return returned;
}

void tmpfs::pwrite(pseudofd_t pseudo, off_t offset, const char *buf, size_t length)
{
	auto entry = get_file_entry_from_pseudo(pseudo);
	if(offset > entry->contents.size()) {
		// TODO: should we increase to this size?
		throw filesystem_error(EINVAL /* The specified file offset is invalid */);
	}

	if(entry->contents.size() < offset + length) {
		entry->contents.resize(offset + length);
	}

	entry->contents.replace(offset, length, std::string(buf, length));
}

/**
 * Reads the next entry from the given directory. If there are no more entries,
 * sets cookie to 0 and returns 0.
 */
size_t tmpfs::readdir(pseudofd_t pseudo, char *buffer, size_t buflen, cloudabi_dircookie_t &cookie)
{
	auto directory = get_file_entry_from_pseudo(pseudo);
	if(directory->type != CLOUDABI_FILETYPE_DIRECTORY) {
		throw filesystem_error(ENOTDIR);
	}

	// cookie is the index into the files map
	// TODO: if files are created or removed during a readdir, this may cause readdir to miss
	// files or return them twice
	auto it = directory->files.begin();
	std::advance(it, cookie);
	if(it == directory->files.end()) {
		cookie = 0;
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

/** Normalizes the given path. When it returns normally, directory
 * points at the innermost directory pointed to by path. It returns the
 * filename that is to be opened, created or unlinked.
 *
 * It returns an error if the given file_entry ptr is not a directory,
 * if any of the path components don't reference a directory, or if the
 * path (eventually) points outside of the given file_entry.
 */
std::string tmpfs::normalize_path(file_entry_ptr &directory, const char *p, size_t len, cloudabi_lookupflags_t lookupflags)
{
	if(directory->type != CLOUDABI_FILETYPE_DIRECTORY) {
		throw filesystem_error(ENOTDIR);
	}

	if(len == 0) {
		throw filesystem_error(ENOENT);
	}

	if(p[0] == '/') {
		// no absolute paths allowed
		throw filesystem_error(ENOTCAPABLE);
	}

	std::string path(p, len);
	// remove any slashes at the end of the path, so "foo////" is interpreted as "foo"
	while(path.back() == '/') {
		path.pop_back();
	}

	// depth may not go under 0, because that means a file outside of the
	// given file_entry is referenced. A depth of 0 means that the file is
	// opened immediately in the given directory.
	int depth = 0;
	do {
		size_t splitter = path.find('/');
		if(splitter == std::string::npos) {
			// filename component.
			if(lookupflags & CLOUDABI_LOOKUP_SYMLINK_FOLLOW) {
				auto it = directory->files.find(path);
				if(it != directory->files.end()) {
					if(it->second->type == CLOUDABI_FILETYPE_SYMBOLIC_LINK) {
						// TODO: follow symlinks
						// if we followed too many symlinks (e.g. 30) return ELOOP
						// for now, symlinks aren't supported, so just return the result
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
			throw filesystem_error(ENOENT);
		}
		auto entry = it->second;
		if(entry->type == CLOUDABI_FILETYPE_SYMBOLIC_LINK) {
			// TODO: follow symlinks
			// if we followed too many symlinks (e.g. 30) return ELOOP
			// for now, symlinks aren't supported, so just throw ENOTDIR
			throw filesystem_error(ENOTDIR);
		}
		if(entry->type != CLOUDABI_FILETYPE_DIRECTORY) {
			throw filesystem_error(ENOTDIR);
		}
		directory = entry;
		if(component == "..") {
			if(depth == 0) {
				// don't allow going outside of the file descriptor
				throw filesystem_error(ENOTCAPABLE);
			}
			depth--;
		} else {
			depth++;
		}
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
		throw filesystem_error(ENOENT);
	}
}

file_entry_ptr tmpfs::get_file_entry_from_pseudo(pseudofd_t pseudo)
{
	auto it = pseudo_fds.find(pseudo);
	if(it != pseudo_fds.end()) {
		return it->second->file;
	} else {
		throw filesystem_error(EBADF);
	}
}
