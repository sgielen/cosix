#include <fd/mem_mapping.hpp>
#include <fd/process_fd.hpp>
#include <global.hpp>
#include <memory/map_virtual.hpp>
#include <memory/page_allocator.hpp>

using namespace cloudos;

#define PAGE_SIZE process_fd::PAGE_SIZE

typedef uint8_t *addr_t;

extern uint32_t _kernel_virtual_base;

size_t cloudos::len_to_pages(size_t len) {
	size_t num_pages = len / PAGE_SIZE;
	if((len % PAGE_SIZE) != 0) {
		return num_pages + 1;
	} else {
		return num_pages;
	}
}

mem_mapping_t::mem_mapping_t(process_fd *o, void *a,
	size_t n, shared_ptr<fd_t> b,
	cloudabi_filesize_t offset, cloudabi_mprot_t p,
	bool s, cloudabi_advice_t adv)
: protection(p)
, virtual_address(a)
, number_of_pages(n)
, owner(o)
, backing_fd(b)
, backing_offset(offset)
, shared(s)
, advice(adv)
{
	if(shared) {
		// a shared anonymous mapping makes no sense
		assert(backing_fd);
	}
	assert(reinterpret_cast<uint32_t>(virtual_address) % PAGE_SIZE == 0);
	assert(reinterpret_cast<uint32_t>(virtual_address) < _kernel_virtual_base);
	assert((reinterpret_cast<uint32_t>(virtual_address) + number_of_pages * PAGE_SIZE) <= _kernel_virtual_base);
}

mem_mapping_t::~mem_mapping_t()
{
	// assert it's fully unbacked
	for(size_t i = 0; i < number_of_pages; ++i) {
		assert(!is_backed(i));
	}
}

mem_mapping_t::mem_mapping_t(process_fd *o, mem_mapping_t *other)
: protection(other->protection)
, virtual_address(other->virtual_address)
, number_of_pages(other->number_of_pages)
, owner(o)
, backing_fd(other->backing_fd)
, backing_offset(other->backing_offset)
, shared(other->shared)
, advice(other->advice)
{
}

void mem_mapping_t::copy_from(mem_mapping_t *other)
{
	other->sync_completely(CLOUDABI_MS_SYNC);
	assert(other->number_of_pages == number_of_pages);
	// TODO: remove this method and use copy-on-write
	char buf[PAGE_SIZE];
	for(size_t i = 0; i < other->number_of_pages; ++i) {
		if(other->is_backed(i)) {
			uint8_t *copy_from = reinterpret_cast<uint8_t*>(other->page_virtual_address(i));
			uint8_t *copy_to = reinterpret_cast<uint8_t*>(page_virtual_address(i));
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

int mem_mapping_t::page_num(void *addr)
{
	addr_t my_start = reinterpret_cast<addr_t>(virtual_address);
	addr_t his_start = reinterpret_cast<addr_t>(addr);
	if(his_start < my_start) {
		return -1;
	}
	auto diff = his_start - my_start;
	size_t page_i = diff / PAGE_SIZE;
	return page_i < number_of_pages ? page_i : -1;
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
	uint8_t *address = reinterpret_cast<uint8_t*>(page_virtual_address(page));
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
	uint8_t *address = reinterpret_cast<uint8_t*>(page_virtual_address(page));
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

		if(shared) {
			assert(backing_fd);
			cloudabi_filestat_t stat;
			backing_fd->file_stat_fget(&stat);
			// TODO: find out if (stat.st_dev, stat.st_ino, page_virtual_address(page)) is already mapped
			// (assert that page_virtual_address is page-aligned)
			// somewhere; increase that physical refcount and map that physical page now.
			// For now, we act as if it's not mapped anywhere yet
		}

		size_t bytes_read = 0;
		if(backing_fd) {
			bytes_read = backing_fd->pread(reinterpret_cast<char*>(b.ptr), PAGE_SIZE, fd_offset(page));
			if(backing_fd->error != 0) {
				// TODO: what now?
				get_vga_stream() << "backing fd pread() failed for a page being backed!\n";
				bytes_read = 0;
			}
		}
		if(bytes_read < PAGE_SIZE) {
			// Fill (rest of) the mapping with zeroes
			memset(reinterpret_cast<char*>(b.ptr) + bytes_read, 0, PAGE_SIZE - bytes_read);
		}

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

		my_new_address = page_virtual_address(their_new_num_pages);
		their_new_address = virtual_address;

		my_new_offset = fd_offset(their_new_num_pages);
		their_new_offset = backing_offset;
	} else {
		// exactly the other way around
		my_new_num_pages = page;
		their_new_num_pages = number_of_pages - page;

		my_new_address = virtual_address;
		their_new_address = page_virtual_address(my_new_num_pages);

		my_new_offset = backing_offset;
		their_new_offset = fd_offset(my_new_num_pages);
	}

	mem_mapping_t *new_mapping =
		allocate<mem_mapping_t>(owner, their_new_address, their_new_num_pages, backing_fd, their_new_offset, protection, false, advice);

	// physical allocations are moved automatically, as they are stored
	// in the process page directory by address

	number_of_pages = my_new_num_pages;
	virtual_address = my_new_address;
	backing_offset = my_new_offset;

	return new_mapping;
}

cloudabi_errno_t mem_mapping_t::sync_completely(cloudabi_msflags_t flags) {
	for(size_t page = 0; page < number_of_pages; ++page) {
		auto *page_entry = get_page_entry(page);
		if(page_entry && (*page_entry & 0x1)) {
			auto res = sync(page, flags);
			if(res != 0) {
				return res;
			}
		}
	}

	return 0;
}

cloudabi_errno_t mem_mapping_t::sync(size_t page, cloudabi_msflags_t flags) {
	assert(page < number_of_pages);

	auto *page_entry = get_page_entry(page);
	if(!page_entry || !(*page_entry & 0x1)) {
		return 0;
	}

	if(flags & CLOUDABI_MS_ASYNC) {
		get_vga_stream() << "mem_mapping_t::sync: MS_ASYNC given, but unsupported, so reinterpreted as MS_SYNC\n";
		flags = CLOUDABI_MS_SYNC;
	}

	auto *page_addr = page_virtual_address(page);

	// if it's dirty and there's a backing fd, write it there
	if((flags & CLOUDABI_MS_SYNC) && shared && backing_fd && (*page_entry & 0x40 /* dirty */)) {
		// first, mark it nondirty
		*page_entry = *page_entry & ~0x40;
		// then, flush it to the fd
		auto res = backing_fd->pwrite(reinterpret_cast<char*>(page_addr), PAGE_SIZE, fd_offset(page));
		if(res != 0) {
			return res;
		}
	}

	// Note: this function is used for unmapping as well
	if(flags & CLOUDABI_MS_INVALIDATE) {
		void *phys = reinterpret_cast<void*>(*page_entry & 0xfffff000);

		*page_entry = 0;

		asm volatile ( "invlpg (%0)" : : "b"(page_addr) : "memory");

		// TODO: don't deallocate physical page if the page is shared!
		get_page_allocator()->deallocate_phys({phys, PAGE_SIZE});
	}

	return 0;
}

void *mem_mapping_t::page_virtual_address(size_t page) {
	return reinterpret_cast<uint8_t*>(virtual_address) + PAGE_SIZE * page;
}

cloudabi_filesize_t mem_mapping_t::fd_offset(size_t page) {
	return backing_offset + PAGE_SIZE * page;
}
