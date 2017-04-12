#pragma once

#include <stdint.h>
#include <stddef.h>
#include <oslibc/list.hpp>
#include <cloudabi_types.h>

namespace cloudos {

size_t len_to_pages(size_t len);

struct mem_mapping_t;
// TODO: use a more fitting data structure for mappings
typedef linked_list<mem_mapping_t*> mem_mapping_list;

struct process_fd;
struct fd_mapping_t;

/** A process memory mapping.
 *
 * Only private mappings are supported at this moment. Trying to create a
 * shared mapping in userland will result in EINVAL.
 *
 * Memory mappings can be anonymous or fd-backed. In both cases they are
 * lazy, which means that physical pages are only allocated when needed.
 * They are zero-filled when anonymous, and filled with file contents when
 * fd-backed.
 *
 * You can use msync() to synchronize fd-backed contents back to file. Since
 * only private mappings are supported, this will not happen automatically.
 * Synchronization is a no-op on anonymous mappings.
 */
struct mem_mapping_t {
	mem_mapping_t(process_fd *owner,
	  void *requested_address /* page aligned */,
	  size_t number_of_pages, fd_mapping_t *backing_fd /* or NULL */,
	  cloudabi_filesize_t offset, cloudabi_mprot_t protection,
	  cloudabi_advice_t adv = CLOUDABI_ADVICE_NORMAL);

	// Make a new mapping from the old one
	mem_mapping_t(process_fd *owner, mem_mapping_t *other);
	// Copy the contents of the old mapping. This assumes this->owner
	// is currently active, and also returns as such.
	void copy_from(mem_mapping_t *other);

	bool covers(void *addr, size_t len = 0);

	cloudabi_mprot_t protection;
	// set in this object and, if pages are backed, in the page tables
	void set_protection(cloudabi_mprot_t);

	bool is_backed(size_t page);

	// If needed, allocate a physical page for this page offset.
	// In the case of a backed mapping, it needs to be filled with
	// contents from the backing fd after this call.
	// Either way, all uninitialized bytes need to be filled with zeroes.
	void ensure_backed(size_t page);
	void ensure_completely_backed();

	void unmap(size_t page);
	void unmap_completely();

	mem_mapping_t *split_at(size_t page, bool return_left);

	uint32_t *get_page_entry(size_t page);
	uint32_t *ensure_get_page_entry(size_t page);

	void *virtual_address; /* always page-aligned */
	size_t number_of_pages;

	process_fd *owner;
	// backing fd, which should be used for synchronization, or nullptr if
	// this is a CLOUDABI_MAP_ANON_FD mapping
	fd_mapping_t *backing_fd;
	cloudabi_filesize_t backing_offset;

	cloudabi_advice_t advice;
};

}
