#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cloudos {

struct gdt_entry {
	uint16_t limit_lower;
	uint16_t base_lower;
	uint8_t base_middle;
	uint8_t access;
	uint8_t flags;
	uint8_t base_upper;
} __attribute__((packed));

struct gdt_directory {
	uint16_t size;
	uint32_t offset;
} __attribute__((packed));

#define SEGMENT_ACCESSED 1
#define SEGMENT_RW 2
#define SEGMENT_DC 4
#define SEGMENT_EXEC 8
#define SEGMENT_ALWAYS 16
#define SEGMENT_PRIV_RING0 0
#define SEGMENT_PRIV_RING1 32
#define SEGMENT_PRIV_RING2 64
#define SEGMENT_PRIV_RING3 96
#define SEGMENT_PRESENT 128

#define SEGMENT_BYTE_GRANULARITY 0
#define SEGMENT_PAGE_GRANULARITY 128
#define SEGMENT_16BIT 0
#define SEGMENT_32BIT 64

#define SEGMENT_MAX_ENTRIES 6

struct segment_table {
	segment_table();

	void load();

	void clear();
	size_t num_entries();

	bool add_entry(uint32_t limit, uint32_t base, uint8_t access, uint8_t flags);

	gdt_directory *directory_ptr();
	gdt_entry *entry_ptr();

private:
	size_t entry_i;
	gdt_entry entries[SEGMENT_MAX_ENTRIES];
	gdt_directory directory;
};

};
