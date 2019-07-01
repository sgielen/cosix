#pragma once

#include <memory/smart_ptr.hpp>

namespace cloudos {

static const size_t NAME_MAX = 255;

struct fd_t;

struct traverse_result {
	shared_ptr<fd_t> directory;
	Blk filename;
	cloudabi_errno_t lookup_errno;
	// If lookup_errno is 0, entry is filled in:
	cloudabi_filestat_t entry;
};

/// Traverse a path starting from the given root_directory, using permissions
/// from cloudabi_fdstat_t. If any of the path's components does not exist or
/// could not be looked up, root_directory->error is set to the respective error
/// and that value is also returned. If the final path component does not exist
/// or cannot be looked up, root_directory->error is 0 (and 0 is returned) and
/// the lookup() error is stored in res->lookup_errno. If the final lookup does
/// succeed, its result is stored in res->entry and res->lookup_errno is zero.
/// If the path ends in a slash, e.g. 'foo/', then 'foo' is the final filename
/// but this function checks that it exists as a directory.
///
/// If oflags is O_CREAT, the file will be created as a regular file. If oflags
/// is O_CREAT | O_EXCL and the file already exists, traverse() succeeds but
/// lookup_errno is set to EEXIST.
///
/// When this function returns 0, the 'traverse_result' parameter is filled in
/// using the following members:
/// - 'directory' is an fd to the innermost directory pointed to by path
/// - 'filename' is an allocation containing the null-terminated filename to be
///   opened, created or unlinked; **you must always deallocate this Blk
///   yourself**
/// - 'lookup_errno' contains the errno that lookup() on the final filename
///   returned
/// - 'entry' contains the results of that lookup() call if lookup_errno is 0.
///
/// The function returns an error if the given root_directory is not a
/// directory, if any of the path components don't reference a directory or
/// could not be looked up, if the path (eventually) points outside of the
/// given directory, or if the path is empty.
cloudabi_errno_t traverse(shared_ptr<fd_t> root_directory, const char *path, size_t pathlen, cloudabi_lookupflags_t lookupflags, cloudabi_oflags_t oflags, cloudabi_fdstat_t *fds, traverse_result *res);

/// Open a file descriptor to a file in a given directory. The return value is
/// the same as directory->error. If this function returns 0, the fd will be
/// returned through 'result'.
/// The base and inherited rights from fdstat are updated to reflect the kind
/// of file descriptor that is opened.
cloudabi_errno_t openat(shared_ptr<fd_t> directory, const char *path, size_t pathlen, cloudabi_lookupflags_t lookupflags, cloudabi_oflags_t oflags, cloudabi_fdstat_t *fdstat, shared_ptr<fd_t> &result);

/// Create a file of the given type on the given path. The return value is
/// the same as directory->error. If this function returns 0, the inode will
/// be returned through '*result' if it is nonzero.
cloudabi_errno_t file_create(shared_ptr<fd_t> directory, const char *path, size_t pathlen, cloudabi_filetype_t filetype, cloudabi_inode_t *result);

/// Unlink the file on the given path. The return value is the same as
/// directory->error.
cloudabi_errno_t file_unlink(shared_ptr<fd_t> directory, const char *path, size_t pathlen, cloudabi_ulflags_t flags);

/// Perform lookup() on a path.
cloudabi_errno_t file_stat_get(shared_ptr<fd_t> directory, const char *path, size_t pathlen, cloudabi_lookupflags_t lookupflags, cloudabi_fdstat_t *fds, cloudabi_filestat_t *entry);

/// Perform stat_put() on a path.
cloudabi_errno_t file_stat_put(shared_ptr<fd_t> directory, const char *path, size_t pathlen, cloudabi_lookupflags_t lookupflags, cloudabi_fdstat_t *fds, const cloudabi_filestat_t *entry, cloudabi_fsflags_t flags);

/// Read a symlink on a path. The return value is the same as directory->error;
/// the actual number of bytes used in the output buffer is written to buflen.
cloudabi_errno_t file_readlink(shared_ptr<fd_t> directory, const char *path, size_t pathlen, char *buf, size_t *buflen);

/// Create a symlink on a path. Is only possible if the device supports symlinks.
cloudabi_errno_t file_symlink(shared_ptr<fd_t> directory, const char *path, size_t pathlen, const char *target, size_t targetlen);

/// Create a hard link on a path. Is only possible if the given fds are on the
/// same device, and the device supports hardlinks. The return value is either
/// equal to sourcedir->error, or sourcedir->error is 0 and the return value
/// is equal to destdir->error.
cloudabi_errno_t file_link(shared_ptr<fd_t> sourcedir, const char *sourcepath, size_t sourcepathlen, cloudabi_lookupflags_t lookupflags, shared_ptr<fd_t> destdir, const char *destination, size_t destlen);

/// Rename a file on a path. Is only possible if the given fds are on the
/// same device. The return value is either equal to sourcedir->error, or
/// sourcedir->error is 0 and the return value is equal to destdir->error.
cloudabi_errno_t file_rename(shared_ptr<fd_t> sourcedir, const char *sourcepath, size_t sourcepathlen, shared_ptr<fd_t> destdir, const char *destination, size_t destlen);

}
