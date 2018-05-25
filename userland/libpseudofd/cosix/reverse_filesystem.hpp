#pragma once

#include "reverse.hpp"
#include <string>
#include <utility>

namespace cosix {

/** A helper implementation, which provides support for filesystem drivers.
 */
struct reverse_filesystem : reverse_handler {
	reverse_filesystem(cloudabi_device_t d);

	typedef cosix::file_entry file_entry;
	typedef cosix::pseudofd_t pseudofd_t;

	// Look up the file entry corresponding to the inode (if filename is empty), or the file entry
	// corresponding to the file pointed to by filename in the directory pointed to by inode.
	virtual file_entry &lookup_nonrecursive(cloudabi_inode_t inode, std::string const &filename) = 0;

	// Read the contents of the symlink pointed to by inode.
	virtual std::string readlink(cloudabi_inode_t inode) = 0;
	using reverse_handler::readlink; // don't override the base version

	/** Dereferences the given path. When it returns normally, directory
	 * points at the innermost directory pointed to by path. It returns the
	 * filename that is to be opened, created or unlinked.
	 *
	 * It returns an error if the given file_entry is not a directory,
	 * if any of the path components don't reference a directory, or if the
	 * path eventually points outside of the given file_entry.
	 */
	std::pair<cloudabi_inode_t, std::string> dereference_path(cloudabi_inode_t directory, std::string path, cloudabi_lookupflags_t lookupflags);
};

}
