#include <fd/vfs.hpp>
#include <fd/fd.hpp>
#include <oslibc/list.hpp>

using namespace cloudos;

typedef linked_list<shared_ptr<fd_t>> directory_list;

cloudabi_errno_t cloudos::traverse(shared_ptr<fd_t> rootdir, const char *path, size_t pathlen, cloudabi_lookupflags_t lookupflags, cloudabi_oflags_t oflags, cloudabi_fdstat_t *fds, traverse_result *res)
{
	assert(res);

	if ((oflags & ~(CLOUDABI_O_CREAT | CLOUDABI_O_DIRECTORY | CLOUDABI_O_EXCL | CLOUDABI_O_TRUNC)) != 0) {
		rootdir->error = EINVAL;
		return rootdir->error;
	}

	if ((oflags & (CLOUDABI_O_CREAT | CLOUDABI_O_DIRECTORY)) == (CLOUDABI_O_CREAT | CLOUDABI_O_DIRECTORY)) {
		// can't create directories
		rootdir->error = EINVAL;
		return rootdir->error;
	}

	Blk alloc_path;

	directory_list *entered = allocate<directory_list>(rootdir); // Traversed directories in reverse order
	const int max_symlinks_followed = 30;
	int symlinks_followed = 0;
	while(1) {
		shared_ptr<fd_t> this_directory = entered->data;
		assert(this_directory);

		if (this_directory->type != CLOUDABI_FILETYPE_DIRECTORY) {
			rootdir->error = ENOTDIR;
			goto error;
		}

		if (pathlen == 0) {
			rootdir->error = ENOENT;
			goto error;
		}

		if (path[0] == '/') {
			// no absolute paths allowed
			rootdir->error = ENOTCAPABLE;
			goto error;
		}

		size_t splitter;
		for (splitter = 0; splitter < pathlen; ++splitter) {
			if (path[splitter] == '/') {
				break;
			}
		}

		// example path: "foo/bar/../baz"
		//                    ^ path
		//                       ^ splitter
		//                              ^ pathlen
		// component = "bar"
		// directories = [foo]
		size_t component_len = splitter;
		assert(component_len > 0);
		if (component_len > NAME_MAX) {
			rootdir->error = ENAMETOOLONG;
			goto error;
		}

		// if full path is "foo/bar////baz":
		//                      ^ path
		//                            ^ splitter
		// but, component_len is still 3, so component is still "bar"
		while (splitter < pathlen && path[splitter+1] == '/') {
			++splitter;
		}

		assert(component_len <= pathlen);
		if (splitter == pathlen) {
			// filename component
			if (component_len == 2 && strncmp(path, "..", component_len) == 0) {
				if (entered->next == nullptr) {
					// allow "foo/..", but not "foo/../.."
					rootdir->error = ENOTCAPABLE;
					goto error;
				}
			}

			this_directory->lookup(path, component_len, oflags, &res->entry);
			res->lookup_errno = this_directory->error;

			if (component_len < pathlen && res->lookup_errno == 0) {
				// traverse("foo/") must fail if the file isn't an existing directory or a symlink to a directory
				// TODO: if filetype is symlink, follow
				if (res->entry.st_filetype != CLOUDABI_FILETYPE_DIRECTORY) {
					rootdir->error = ENOTDIR;
					goto error;
				}
			}

			// if the lookup succeeded, and it's a symlink to follow, follow it
			if (lookupflags & CLOUDABI_LOOKUP_SYMLINK_FOLLOW && res->lookup_errno == 0) {
				if (res->entry.st_filetype == CLOUDABI_FILETYPE_SYMBOLIC_LINK) {
					size_t size = res->entry.st_size;
					if (size == 0 || ++symlinks_followed >= max_symlinks_followed) {
						rootdir->error = ELOOP;
						goto error;
					}
					if (size > NAME_MAX) {
						rootdir->error = ENAMETOOLONG;
						goto error;
					}
					// follow symlinks: replace path by symlink contents
					Blk new_alloc = allocate(size);
					pathlen = this_directory->file_readlink(path, component_len, reinterpret_cast<char*>(new_alloc.ptr), size);
					if (this_directory->error != 0) {
						rootdir->error = this_directory->error;
						goto error;
					}
					assert(pathlen <= size);
					path = reinterpret_cast<char*>(new_alloc.ptr);
					if (alloc_path.ptr) {
						deallocate(alloc_path);
					}
					alloc_path = new_alloc;
					continue;
				}
			}

			// done traversing; we have the final directory and filename
			res->filename = allocate(component_len + 1);
			auto *name = reinterpret_cast<char*>(res->filename.ptr);
			strncpy(name, path, component_len);
			name[component_len] = 0;
			res->directory = this_directory;
			goto success;
		}

		if (component_len == 0 || (component_len == 1 && strncmp(path, ".", component_len) == 0)) {
			// "foo//bar" or "foo/./bar", no-op
			path += splitter + 1;
			pathlen -= splitter + 1;
			continue;
		}

		if (component_len == 2 && strncmp(path, "..", component_len) == 0) {
			// "foo/../bar"
			if (entered->next == nullptr) {
				// don't allow going outside of the rootdir
				rootdir->error = ENOTCAPABLE;
				goto error;
			}
			auto *entry = entered;
			entered = entered->next;
			deallocate(entry);
			path += splitter + 1;
			pathlen -= splitter + 1;
			continue;
		}

		this_directory->lookup(path, component_len, 0, &res->entry);
		res->lookup_errno = this_directory->error;
		if (this_directory->error != 0) {
			rootdir->error = this_directory->error;
			goto error;
		}

		if (res->entry.st_filetype == CLOUDABI_FILETYPE_SYMBOLIC_LINK) {
			if (res->entry.st_size == 0 || ++symlinks_followed >= max_symlinks_followed) {
				rootdir->error = ELOOP;
				goto error;
			}
			if (res->entry.st_size > NAME_MAX) {
				rootdir->error = ENAMETOOLONG;
				goto error;
			}
			// follow symlinks: path becomes <symlink>/<rest of path>
			Blk new_alloc = allocate(res->entry.st_size + pathlen - splitter);
			auto *newpath = reinterpret_cast<char*>(new_alloc.ptr);
			auto linklen = this_directory->file_readlink(path, component_len, newpath, res->entry.st_size);
			if (this_directory->error != 0) {
				rootdir->error = this_directory->error;
				goto error;
			}
			assert(linklen <= res->entry.st_size);
			memcpy(newpath + linklen, path + splitter, pathlen - splitter);
			pathlen = linklen + pathlen - splitter;
			path = newpath;
			if (alloc_path.ptr) {
				deallocate(alloc_path);
			}
			alloc_path = new_alloc;

			// continue with lookup for new path
			continue;
		}
		if (res->entry.st_filetype != CLOUDABI_FILETYPE_DIRECTORY) {
			rootdir->error = ENOTDIR;
			goto error;
		}
		if (splitter + 1 == pathlen) {
			// 'foo/' or 'foo///', this is the final path component
			res->filename = allocate(component_len + 1);
			auto *name = reinterpret_cast<char*>(res->filename.ptr);
			strncpy(name, path, component_len);
			name[component_len] = 0;
			res->directory = this_directory;
			goto success;
		}
		// enter this directory
		auto new_directory = this_directory->inode_open(res->entry.st_dev, res->entry.st_ino, fds);
		if (this_directory->error != 0 || !new_directory) {
			rootdir->error = this_directory->error;
			goto error;
		}
		directory_list *item = allocate<directory_list>(new_directory);
		prepend(&entered, item);
		path += splitter + 1;
		pathlen -= splitter + 1;
	}

success:
	rootdir->error = 0;
	// file must reference a filename because the path cannot be empty
	assert(res->filename.ptr != nullptr);
	assert(res->filename.size != 0);
	assert(res->directory);
	goto finish;

error:
	assert(rootdir->error != 0);
	assert(!res->directory);
	res->filename.ptr = nullptr;
	res->filename.size = 0;
	res->lookup_errno = 0;

finish:
	if (alloc_path.ptr) {
		deallocate(alloc_path);
	}
	clear(&entered);
	return rootdir->error;
}

cloudabi_errno_t cloudos::openat(shared_ptr<fd_t> rootdir, const char *path, size_t pathlen, cloudabi_lookupflags_t lookupflags, cloudabi_oflags_t oflags, cloudabi_fdstat_t *fds, shared_ptr<fd_t> &new_fd)
{
	new_fd.reset();

	traverse_result travres;
	if (traverse(rootdir, path, pathlen, lookupflags, oflags, fds, &travres) != 0) {
		return rootdir->error;
	}

	deallocate(travres.filename);

	if (travres.lookup_errno != 0) {
		rootdir->error = travres.lookup_errno;
		return rootdir->error;
	}

	if ((oflags & CLOUDABI_O_DIRECTORY) && travres.entry.st_filetype != CLOUDABI_FILETYPE_DIRECTORY) {
		rootdir->error = ENOTDIR;
		return rootdir->error;
	}

	new_fd = travres.directory->inode_open(travres.entry.st_dev, travres.entry.st_ino, fds);
	if (travres.directory->error) {
		assert(!new_fd);
		rootdir->error = travres.directory->error;
		return rootdir->error;
	}
	assert(new_fd);

	// A directory may not be opened read-write
	if(new_fd->type == CLOUDABI_FILETYPE_DIRECTORY && fds->fs_rights_base & CLOUDABI_RIGHT_FD_WRITE) {
		new_fd.reset();
		rootdir->error = EISDIR;
		return rootdir->error;
	}

	if ((oflags & CLOUDABI_O_TRUNC) && !(fds->fs_rights_base & CLOUDABI_RIGHT_FD_WRITE)) {
		new_fd.reset();
		rootdir->error = EINVAL;
		return rootdir->error;
	}

	if (oflags & CLOUDABI_O_TRUNC) {
		travres.entry.st_size = 0;
		new_fd->file_stat_fput(&travres.entry, CLOUDABI_FILESTAT_SIZE);
		if (new_fd->error) {
			rootdir->error = new_fd->error;
			new_fd.reset();
			return rootdir->error;
		}
	}

	// Depending on filetype, drop some rights if they don't make sense
	auto &base = fds->fs_rights_base;
	auto &inheriting = fds->fs_rights_inheriting;
	if(new_fd->type != CLOUDABI_FILETYPE_DIRECTORY) {
		inheriting = 0;
	}
	if(new_fd->type != CLOUDABI_FILETYPE_REGULAR_FILE) {
		base &= ~(CLOUDABI_RIGHT_PROC_EXEC);
	}
	if(new_fd->type != CLOUDABI_FILETYPE_DIRECTORY) {
		// remove all rights that only make sense on directories
		base &= ~(0
			| CLOUDABI_RIGHT_FILE_CREATE_DIRECTORY
			| CLOUDABI_RIGHT_FILE_CREATE_FILE
			| CLOUDABI_RIGHT_FILE_LINK_SOURCE
			| CLOUDABI_RIGHT_FILE_LINK_TARGET
			| CLOUDABI_RIGHT_FILE_OPEN
			| CLOUDABI_RIGHT_FILE_READDIR
			| CLOUDABI_RIGHT_FILE_READLINK
			| CLOUDABI_RIGHT_FILE_RENAME_SOURCE
			| CLOUDABI_RIGHT_FILE_RENAME_TARGET
			| CLOUDABI_RIGHT_FILE_STAT_GET
			| CLOUDABI_RIGHT_FILE_STAT_PUT_TIMES
			| CLOUDABI_RIGHT_FILE_SYMLINK
			| CLOUDABI_RIGHT_FILE_UNLINK
		);
	} else {
		// remove all rights that don't make sense on directories
		base &= ~(0
			| CLOUDABI_RIGHT_FD_READ
			| CLOUDABI_RIGHT_FD_SEEK
			| CLOUDABI_RIGHT_FD_TELL
			| CLOUDABI_RIGHT_MEM_MAP
			| CLOUDABI_RIGHT_MEM_MAP_EXEC
		);
	}

	return 0;
}

cloudabi_errno_t cloudos::file_create(shared_ptr<fd_t> directory, const char *path, size_t pathlen, cloudabi_filetype_t filetype, cloudabi_inode_t *result)
{
	cloudabi_fdstat_t fds;
	traverse_result travres;
	if (traverse(directory, path, pathlen, 0 /* lookupflags */, 0 /* oflags */, &fds, &travres) != 0) {
		return directory->error;
	}

	if (travres.lookup_errno == 0) {
		directory->error = EEXIST;
		deallocate(travres.filename);
		return directory->error;
	}

	if (travres.lookup_errno != ENOENT) {
		directory->error = travres.lookup_errno;
		deallocate(travres.filename);
		return directory->error;
	}

	// The file doesn't exist, create it now
	auto *filename = static_cast<const char*>(travres.filename.ptr);
	cloudabi_inode_t inode = travres.directory->file_create(filename, strnlen(filename, travres.filename.size), filetype);
	if (result) {
		*result = inode;
	}
	deallocate(travres.filename);
	directory->error = travres.directory->error;
	return directory->error;
}

cloudabi_errno_t cloudos::file_unlink(shared_ptr<fd_t> directory, const char *path, size_t pathlen, cloudabi_ulflags_t flags)
{
	cloudabi_fdstat_t fds;
	traverse_result travres;
	if (traverse(directory, path, pathlen, 0 /* lookupflags */, 0 /* oflags */, &fds, &travres) != 0) {
		return directory->error;
	}

	auto *filename = static_cast<const char*>(travres.filename.ptr);
	travres.directory->file_unlink(filename, strnlen(filename, travres.filename.size), flags);

	deallocate(travres.filename);

	directory->error = travres.directory->error;
	return directory->error;
}

cloudabi_errno_t cloudos::file_stat_get(shared_ptr<fd_t> directory, const char *path, size_t pathlen, cloudabi_lookupflags_t lookupflags, cloudabi_fdstat_t *fds, cloudabi_filestat_t *entry)
{
	traverse_result travres;
	if (traverse(directory, path, pathlen, lookupflags, 0 /* oflags */, fds, &travres) != 0) {
		return directory->error;
	}

	deallocate(travres.filename);

	if (travres.lookup_errno == 0) {
		*entry = travres.entry;
		return 0;
	} else {
		directory->error = travres.lookup_errno;
		return directory->error;
	}
}

cloudabi_errno_t cloudos::file_stat_put(shared_ptr<fd_t> directory, const char *path, size_t pathlen, cloudabi_lookupflags_t lookupflags, cloudabi_fdstat_t *fds, const cloudabi_filestat_t *entry, cloudabi_fsflags_t flags)
{
	traverse_result travres;
	if (traverse(directory, path, pathlen, lookupflags, 0 /* oflags */, fds, &travres) != 0) {
		return directory->error;
	}

	if (travres.lookup_errno != 0) {
		directory->error = travres.lookup_errno;
		deallocate(travres.filename);
		return directory->error;
	}

	auto *filename = static_cast<const char*>(travres.filename.ptr);
	travres.directory->file_stat_put(filename, strnlen(filename, travres.filename.size), entry, flags);

	deallocate(travres.filename);

	directory->error = travres.directory->error;
	return directory->error;
}

cloudabi_errno_t cloudos::file_readlink(shared_ptr<fd_t> directory, const char *path, size_t pathlen, char *buf, size_t *buflen)
{
	cloudabi_fdstat_t fds;
	traverse_result travres;
	if (traverse(directory, path, pathlen, 0 /* lookupflags */, 0 /* oflags */, &fds, &travres) != 0) {
		return directory->error;
	}

	if (travres.lookup_errno != 0) {
		directory->error = travres.lookup_errno;
		deallocate(travres.filename);
		return directory->error;
	}

	if (travres.entry.st_filetype != CLOUDABI_FILETYPE_SYMBOLIC_LINK) {
		directory->error = EINVAL;
		deallocate(travres.filename);
		return directory->error;
	}

	auto *filename = static_cast<const char*>(travres.filename.ptr);
	*buflen = travres.directory->file_readlink(filename, strnlen(filename, travres.filename.size), buf, *buflen);

	deallocate(travres.filename);

	directory->error = travres.directory->error;
	return directory->error;
}

cloudabi_errno_t cloudos::file_symlink(shared_ptr<fd_t> directory, const char *path, size_t pathlen, const char *target, size_t targetlen)
{
	cloudabi_fdstat_t fds;
	traverse_result travres;
	if (traverse(directory, path, pathlen, 0 /* lookupflags */, 0 /* oflags */, &fds, &travres) != 0) {
		return directory->error;
	}

	if (travres.lookup_errno == 0) {
		directory->error = EEXIST;
		deallocate(travres.filename);
		return directory->error;
	}

	if (travres.lookup_errno != ENOENT) {
		directory->error = travres.lookup_errno;
		deallocate(travres.filename);
		return directory->error;
	}

	// The symlink doesn't exist, create it now
	auto *filename = static_cast<const char*>(travres.filename.ptr);
	travres.directory->file_symlink(target, targetlen, filename, strnlen(filename, travres.filename.size));
	deallocate(travres.filename);
	directory->error = travres.directory->error;
	return directory->error;
}

cloudabi_errno_t cloudos::file_link(shared_ptr<fd_t> sourcedir, const char *sourcepath, size_t sourcepathlen, cloudabi_lookupflags_t lookupflags, shared_ptr<fd_t> destdir, const char *destination, size_t destlen)
{
	cloudabi_fdstat_t fds1;
	traverse_result travres1;
	if (traverse(sourcedir, sourcepath, sourcepathlen, lookupflags, 0 /* oflags */, &fds1, &travres1) != 0) {
		return sourcedir->error;
	}

	// source path must exist
	if (travres1.lookup_errno != 0) {
		sourcedir->error = travres1.lookup_errno;
		deallocate(travres1.filename);
		return sourcedir->error;
	}

	if (travres1.entry.st_filetype == CLOUDABI_FILETYPE_DIRECTORY) {
		sourcedir->error = EPERM;
		deallocate(travres1.filename);
		return sourcedir->error;
	}

	cloudabi_fdstat_t fds2;
	traverse_result travres2;
	if (traverse(destdir, destination, destlen, 0, 0 /* oflags */, &fds2, &travres2) != 0) {
		return destdir->error;
	}

	// dest file must not
	if (travres2.lookup_errno == 0) {
		destdir->error = EEXIST;
		deallocate(travres2.filename);
		return destdir->error;
	}

	if (travres2.lookup_errno != ENOENT) {
		destdir->error = travres2.lookup_errno;
		deallocate(travres2.filename);
		return destdir->error;
	}

	// create destination as a hardlink of source
	auto *filename1 = static_cast<const char*>(travres1.filename.ptr);
	auto *filename2 = static_cast<const char*>(travres2.filename.ptr);
	travres1.directory->file_link(filename1, strnlen(filename1, travres1.filename.size), travres2.directory, filename2, strnlen(filename2, travres2.filename.size));
	deallocate(travres1.filename);
	deallocate(travres2.filename);
	sourcedir->error = travres1.directory->error;
	return sourcedir->error;
}

cloudabi_errno_t cloudos::file_rename(shared_ptr<fd_t> sourcedir, const char *sourcepath, size_t sourcepathlen, shared_ptr<fd_t> destdir, const char *destination, size_t destlen)
{
	cloudabi_fdstat_t fds1;
	traverse_result travres1;
	if (traverse(sourcedir, sourcepath, sourcepathlen, 0 /* lookupflags */, 0 /* oflags */, &fds1, &travres1) != 0) {
		return sourcedir->error;
	}

	// source path must exist
	if (travres1.lookup_errno != 0) {
		sourcedir->error = travres1.lookup_errno;
		deallocate(travres1.filename);
		return sourcedir->error;
	}

	cloudabi_fdstat_t fds2;
	traverse_result travres2;
	if (traverse(destdir, destination, destlen, 0, 0 /* oflags */, &fds2, &travres2) != 0) {
		return destdir->error;
	}

	// dest file may exist and will be overwritten
	if (travres2.lookup_errno != ENOENT) {
		destdir->error = travres2.lookup_errno;
		deallocate(travres2.filename);
		return destdir->error;
	}

	// rename source to destination
	auto *filename1 = static_cast<const char*>(travres1.filename.ptr);
	auto *filename2 = static_cast<const char*>(travres2.filename.ptr);
	travres1.directory->file_rename(filename1, strnlen(filename1, travres1.filename.size), travres2.directory, filename2, strnlen(filename2, travres2.filename.size));
	deallocate(travres1.filename);
	deallocate(travres2.filename);
	sourcedir->error = travres1.directory->error;
	return sourcedir->error;
}
