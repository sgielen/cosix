#include "cosix/reverse_filesystem.hpp"

using namespace cosix;

reverse_filesystem::reverse_filesystem(cloudabi_device_t d)
{
	device = d;
}

/** Normalizes the given path. When it returns normally, directory
 * points at the innermost directory pointed to by path. It returns the
 * filename that is to be opened, created or unlinked.
 *
 * It throws an error if the given inode is not a directory,
 * if any of the path components don't reference a directory, or if the
 * path (eventually) points outside of the given directory.
 */
std::pair<cloudabi_inode_t, std::string> reverse_filesystem::dereference_path(cloudabi_inode_t orig_dir_inode, std::string path, cloudabi_lookupflags_t lookupflags)
{
	file_entry directory = lookup_nonrecursive(orig_dir_inode, "");
	if(directory.type != CLOUDABI_FILETYPE_DIRECTORY) {
		throw cloudabi_system_error(ENOTDIR);
	}

	if(path.empty()) {
		throw cloudabi_system_error(ENOENT);
	}

	if(path[0] == '/') {
		// no absolute paths allowed
		throw cloudabi_system_error(ENOTCAPABLE);
	}

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
				try {
					auto const entry = lookup_nonrecursive(directory.inode, path);
					if(entry.type == CLOUDABI_FILETYPE_SYMBOLIC_LINK) {
						// follow symlink
						if(++symlinks_followed >= max_symlinks_followed) {
							throw cloudabi_system_error(ELOOP);
						}
						path = readlink(entry.inode);
						// continue with lookup; take current depth into account
						continue;
					}
				} catch(cloudabi_system_error &e) {
					if(e.error == ENOENT) {
						// ignore this error, caller may expect this
					} else {
						// TODO: just "throw;" doesn't work?
						throw cloudabi_system_error(e.error);
					}
				}
			}
			// done with lookup
			return std::make_pair(directory.inode, path);
		}

		// path component; it must exist
		std::string component = path.substr(0, splitter);
		path = path.substr(splitter + 1);
		if(component.empty() || component == ".") {
			// no-op path component, just continue
			continue;
		}
		auto const entry = lookup_nonrecursive(directory.inode, component);
		if(entry.type == CLOUDABI_FILETYPE_SYMBOLIC_LINK) {
			if(++symlinks_followed >= max_symlinks_followed) {
				throw cloudabi_system_error(ELOOP);
			}
			path = readlink(entry.inode) + "/" + path;
			// continue with lookup; take current depth into account
			continue;
		}
		if(entry.type != CLOUDABI_FILETYPE_DIRECTORY) {
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
			return std::make_pair(directory.inode, component);
		}
		directory = entry;
	} while(!path.empty());

	/* unreachable code */
	assert(!"Unreachable");
	exit(123);
}

