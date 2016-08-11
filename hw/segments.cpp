#include "segments.hpp"
#include "oslibc/string.h"
#include "global.hpp"

using namespace cloudos;

extern "C"
void gdt_load(gdt_directory*);

segment_table::segment_table()
: entry_i(0)
, fs_idx(0)
{
	clear();
	directory.size = sizeof(entries) - 1;
	directory.offset = reinterpret_cast<uint64_t>(&entries[0]);
}

void segment_table::load()
{
	gdt_load(&directory);
}

void segment_table::clear()
{
	memset(&entries, 0, sizeof(entries));
	entry_i = 0;
}

size_t segment_table::num_entries()
{
	return entry_i;
}

int segment_table::add_entry(uint32_t limit, uint32_t base, uint8_t access, uint8_t flags)
{
	if(entry_i == SEGMENT_MAX_ENTRIES) {
		return -1;
	}

	gdt_entry &entry = entries[entry_i];

	entry.limit_lower = limit & 0xffff;
	entry.flags = (limit >> 16) & 0x0f;

	entry.base_lower = base & 0xffff;
	entry.base_middle = (base >> 16) & 0xff;
	entry.base_upper = (base >> 24) & 0xff;

	entry.access = access;
	entry.flags = entry.flags | (flags & 0xf0);
	return entry_i++;
}

gdt_directory *segment_table::directory_ptr() {
	return &directory;
}

gdt_entry *segment_table::entry_ptr() {
	return &entries[0];
}

bool segment_table::add_tss_entry()
{
	memset(&tss, 0, sizeof(tss));
	tss.ss0 = 0x10;
	tss.esp0 = 0x200000;

	uint32_t base = reinterpret_cast<uint64_t>(&tss);
	uint32_t limit = base + sizeof(tss);

	return add_entry(limit, base,
		  1 /* is TSS, not LDT */
		| 8 /* 32bit */
		| SEGMENT_PRIV_RING3
		| SEGMENT_PRESENT,
		0);
}

bool segment_table::add_fs_entry()
{
	if(fs_idx != 0) {
		kernel_panic("fs_entry already exists");
	}
	// the base address of %fs is changed by set_fsbase() whenever we
	// context switch to a process, but the length is always the size of a
	// pointer. The userland is allowed to change this pointer.
	fs_idx = add_entry(sizeof(void*), 0,
		SEGMENT_RW | SEGMENT_ALWAYS | SEGMENT_PRIV_RING3 | SEGMENT_PRESENT,
		SEGMENT_PAGE_GRANULARITY | SEGMENT_32BIT | SEGMENT_AVAILABLE);
	return true;
}

void segment_table::set_fsbase(void *virtual_address)
{
	gdt_entry &entry = entries[fs_idx];

	uint32_t base = reinterpret_cast<uint32_t>(virtual_address);
	entry.base_lower = base & 0xffff;
	entry.base_middle = (base >> 16) & 0xff;
	entry.base_upper = (base >> 24) & 0xff;
}

void segment_table::set_kernel_stack(void *stackptr)
{
	tss.esp0 = reinterpret_cast<uint64_t>(stackptr);
}
