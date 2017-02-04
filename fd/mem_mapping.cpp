#include "mem_mapping.hpp"
#include "global.hpp"
#include "process_fd.hpp"
#include "memory/page_allocator.hpp"

using namespace cloudos;

#define PAGE_SIZE 4096

typedef uint8_t *addr_t;

size_t cloudos::len_to_pages(size_t len) {
	size_t num_pages = len / PAGE_SIZE;
	if((len % PAGE_SIZE) != 0) {
		return num_pages + 1;
	} else {
		return num_pages;
	}
}

mem_mapping_t::mem_mapping_t(process_fd *o, void *a,
	size_t n, fd_mapping_t *b,
	cloudabi_filesize_t offset, cloudabi_mprot_t p)
: protection(p)
, virtual_address(a)
, number_of_pages(n)
, owner(o)
, backing_fd(b)
, backing_offset(offset)
, advice(CLOUDABI_ADVICE_NORMAL)
, lock_count(0)
{
	if(reinterpret_cast<uint32_t>(virtual_address) % PAGE_SIZE != 0) {
		kernel_panic("non-page-aligned requested_address");
	}
}

mem_mapping_t::mem_mapping_t(process_fd *o, mem_mapping_t *other)
: protection(other->protection)
, virtual_address(other->virtual_address)
, number_of_pages(other->number_of_pages)
, owner(o)
, backing_fd(other->backing_fd)
, backing_offset(other->backing_offset)
, advice(other->advice)
, lock_count(0)
{
}

void mem_mapping_t::copy_from(mem_mapping_t *other)
{
	if(other->number_of_pages != number_of_pages) {
		kernel_panic("copy_from length mismatch");
	}
	// TODO: remove this method and use copy-on-write
	char buf[PAGE_SIZE];
	for(size_t i = 0; i < other->number_of_pages; ++i) {
		if(other->is_backed(i)) {
			uint8_t *copy_from = reinterpret_cast<uint8_t*>(other->virtual_address) + PAGE_SIZE * i;
			uint8_t *copy_to = reinterpret_cast<uint8_t*>(virtual_address) + PAGE_SIZE * i;
			ensure_backed(i);
			other->owner->install_page_directory();
			memcpy(buf, copy_from, PAGE_SIZE);
			owner->install_page_directory();
			memcpy(copy_to, buf, PAGE_SIZE);
		}
	}
}

bool mem_mapping_t::covers(void *addr, size_t len)
{
	addr_t my_start = reinterpret_cast<addr_t>(virtual_address);
	addr_t my_end = my_start + PAGE_SIZE * number_of_pages;

	addr_t his_start = reinterpret_cast<addr_t>(addr);
	addr_t his_end = his_start + len;

	return his_start >= my_start && his_end >= my_start && his_start < my_end && his_end <= my_end;
}

void mem_mapping_t::set_protection(cloudabi_mprot_t p)
{
	protection = p;
	// TODO: set all backed page table entries to these bits
}

bool mem_mapping_t::is_backed(size_t page)
{
	if(page > number_of_pages) {
		kernel_panic("page out of range");
	}
	uint8_t *address = reinterpret_cast<uint8_t*>(virtual_address) + PAGE_SIZE * page;
	uint16_t page_table_num = reinterpret_cast<uint64_t>(address) >> 22;
	uint32_t *page_table = owner->get_page_table(page_table_num);
	if(page_table == nullptr) {
		return false;
	}
	uint16_t page_entry_num = reinterpret_cast<uint64_t>(address) >> 12 & 0x03ff;
	uint32_t &page_entry = page_table[page_entry_num];
	return page_entry & 0x1;
}

void mem_mapping_t::ensure_backed(size_t page)
{
	if(page > number_of_pages) {
		kernel_panic("page out of range");
	}
	uint8_t *address = reinterpret_cast<uint8_t*>(virtual_address) + PAGE_SIZE * page;
	uint16_t page_table_num = reinterpret_cast<uint64_t>(address) >> 22;
	uint32_t *page_table = owner->ensure_get_page_table(page_table_num);
	uint16_t page_entry_num = reinterpret_cast<uint64_t>(address) >> 12 & 0x03ff;
	uint32_t &page_entry = page_table[page_entry_num];
	if(!(page_entry & 0x1)) {
		Blk b = get_map_virtual()->allocate(PAGE_SIZE);
		if(b.ptr == 0) {
			kernel_panic("Failed to allocate page to back a mapping");
		}

		assert((reinterpret_cast<uint32_t>(b.ptr) & 0xfff) == 0);
		// Fill mapping with zeroes
		// TODO: if this mapping is fd-backed, fill it with fd contents
		// instead of zeroes
		memset(b.ptr, 0, PAGE_SIZE);

		// Re-map to userland
		void *phys = get_map_virtual()->to_physical_address(b.ptr);
		page_entry = reinterpret_cast<uint32_t>(phys) | 0x07; // TODO: use the correct permission bits
		get_map_virtual()->unmap_page_only(b.ptr);
	}
}

void mem_mapping_t::ensure_completely_backed()
{
	for(size_t i = 0; i < number_of_pages; ++i) {
		ensure_backed(i);
	}
}

void mem_mapping_t::unmap(size_t page)
{
	if(page > number_of_pages) {
		kernel_panic("page out of range");
	}
	uint8_t *address = reinterpret_cast<uint8_t*>(virtual_address) + PAGE_SIZE * page;
	uint16_t page_table_num = reinterpret_cast<uint64_t>(address) >> 22;
	uint32_t *page_table = owner->get_page_table(page_table_num);
	if(page_table == nullptr) {
		return;
	}
	uint16_t page_entry_num = reinterpret_cast<uint64_t>(address) >> 12 & 0x03ff;
	uint32_t &page_entry = page_table[page_entry_num];
	void *phys = reinterpret_cast<void*>(page_entry & 0xfffff000);

	page_entry = 0;
	asm volatile ( "invlpg (%0)" : : "b"(address) : "memory");

	// TODO: don't deallocate physical page if the page is shared!
	get_page_allocator()->deallocate_phys({phys, PAGE_SIZE});
}

void mem_mapping_t::unmap_completely()
{
	for(size_t i = 0; i < number_of_pages; ++i) {
		unmap(i);
	}
}

