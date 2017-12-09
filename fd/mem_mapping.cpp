#include <fd/mem_mapping.hpp>
#include <fd/process_fd.hpp>
#include <global.hpp>
#include <memory/map_virtual.hpp>
#include <memory/page_allocator.hpp>

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
	cloudabi_filesize_t offset, cloudabi_mprot_t p,
	cloudabi_advice_t adv)
: protection(p)
, virtual_address(a)
, number_of_pages(n)
, owner(o)
, backing_fd(b)
, backing_offset(offset)
, advice(adv)
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

static uint32_t prot_to_bits(cloudabi_mprot_t p) {
	const int USER_ACCESSIBLE = 4;
	const int WRITABLE = 2;
	// TODO: there is no NX bit in x86 unless we use PAE

	assert((p & ~(CLOUDABI_PROT_EXEC | CLOUDABI_PROT_READ | CLOUDABI_PROT_WRITE)) == 0);
	uint32_t bits = p == 0 ? 0 : USER_ACCESSIBLE;
	if(p & CLOUDABI_PROT_WRITE) {
		bits |= WRITABLE;
	}
	return bits;
}

void mem_mapping_t::set_protection(cloudabi_mprot_t p)
{
	protection = p;

	auto bits = prot_to_bits(protection);
	for(size_t page = 0; page < number_of_pages; ++page) {
		auto *page_entry = get_page_entry(page);
		if(page_entry && (*page_entry & 0x1)) {
			*page_entry = (*page_entry & 0xfffffff9) | bits;
		}
	}
}

uint32_t *mem_mapping_t::get_page_entry(size_t page)
{
	assert(page < number_of_pages);
	uint8_t *address = reinterpret_cast<uint8_t*>(virtual_address) + PAGE_SIZE * page;
	uint16_t page_table_num = reinterpret_cast<uint64_t>(address) >> 22;
	uint32_t *page_table = owner->get_page_table(page_table_num);
	if(page_table == nullptr) {
		return nullptr;
	}
	uint16_t page_entry_num = reinterpret_cast<uint64_t>(address) >> 12 & 0x03ff;
	return &page_table[page_entry_num];
}

uint32_t *mem_mapping_t::ensure_get_page_entry(size_t page)
{
	assert(page < number_of_pages);
	uint8_t *address = reinterpret_cast<uint8_t*>(virtual_address) + PAGE_SIZE * page;
	uint16_t page_table_num = reinterpret_cast<uint64_t>(address) >> 22;
	uint32_t *page_table = owner->ensure_get_page_table(page_table_num);
	uint16_t page_entry_num = reinterpret_cast<uint64_t>(address) >> 12 & 0x03ff;
	return &page_table[page_entry_num];
}

bool mem_mapping_t::is_backed(size_t page)
{
	auto *page_entry = get_page_entry(page);
	if(page_entry == nullptr) {
		return false;
	}
	return *page_entry & 0x1;
}

void mem_mapping_t::ensure_backed(size_t page)
{
	auto *page_entry = ensure_get_page_entry(page);
	if(!(*page_entry & 0x1)) {
		Blk b = get_map_virtual()->allocate(PAGE_SIZE);
		if(b.ptr == nullptr) {
			kernel_panic("Failed to allocate page to back a mapping");
		}

		assert((reinterpret_cast<uint32_t>(b.ptr) & 0xfff) == 0);
		// Fill mapping with zeroes
		// TODO: if this mapping is fd-backed, fill it with fd contents
		// instead of zeroes
		memset(b.ptr, 0, PAGE_SIZE);

		// Re-map to userland
		void *phys = get_map_virtual()->to_physical_address(b.ptr);
		auto bits = prot_to_bits(protection);
		*page_entry = reinterpret_cast<uint32_t>(phys) | bits | 0x01;

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
	auto *page_entry = get_page_entry(page);
	if(page_entry == nullptr) {
		return;
	}
	void *phys = reinterpret_cast<void*>(*page_entry & 0xfffff000);

	*page_entry = 0;

	uint8_t *address = reinterpret_cast<uint8_t*>(virtual_address) + PAGE_SIZE * page;
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

mem_mapping_t *mem_mapping_t::split_at(size_t page, bool return_left) {
	assert(page > 0);
	assert(page < number_of_pages);
	// if return_left is true, create a new mem_mapping_t for all pages < $page
	// otherwise, create a new mem_mapping_t for all pages >= $page
	// give it all physical pages currently held by this object, then resize this object accordingly

	size_t my_new_num_pages, their_new_num_pages;
	void *my_new_address, *their_new_address;
	cloudabi_filesize_t my_new_offset, their_new_offset;

	if(return_left) {
		my_new_num_pages = number_of_pages - page;
		their_new_num_pages = page;

		my_new_address = reinterpret_cast<void*>(reinterpret_cast<size_t>(virtual_address) + PAGE_SIZE * their_new_num_pages);
		their_new_address = virtual_address;

		my_new_offset = backing_offset + PAGE_SIZE * their_new_num_pages;
		their_new_offset = backing_offset;
	} else {
		// exactly the other way around
		my_new_num_pages = page;
		their_new_num_pages = number_of_pages - page;

		my_new_address = virtual_address;
		their_new_address = reinterpret_cast<void*>(reinterpret_cast<size_t>(virtual_address) + PAGE_SIZE * my_new_num_pages);

		my_new_offset = backing_offset;
		their_new_offset = backing_offset + PAGE_SIZE * my_new_num_pages;
	}

	mem_mapping_t *new_mapping =
		allocate<mem_mapping_t>(owner, their_new_address, their_new_num_pages, backing_fd, their_new_offset, protection, advice);

	// physical allocations are moved automatically, as they are stored
	// in the process page directory by address

	number_of_pages = my_new_num_pages;
	virtual_address = my_new_address;
	backing_offset = my_new_offset;

	return new_mapping;
}
