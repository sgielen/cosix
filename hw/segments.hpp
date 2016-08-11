#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cloudos {

struct tss_entry {
	uint32_t prev_tss;
	uint32_t esp0, ss0;

	/* all of the fields below are only used in hardware task switching */
	uint32_t esp1, ss1;
	uint32_t esp2, ss2;

	uint32_t cr3, eip, eflags;
	uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
	uint32_t es, cs, ss, ds, fs, gs;
	uint32_t ldt, trap, iomap_base;
};

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
#define SEGMENT_AVAILABLE 16

#define SEGMENT_MAX_ENTRIES 7

struct segment_table {
	segment_table();

	void load();

	void clear();
	size_t num_entries();

	int add_entry(uint32_t limit, uint32_t base, uint8_t access, uint8_t flags);
	bool add_tss_entry();
	bool add_fs_entry();
	void set_fsbase(void *virtual_address);
	void set_kernel_stack(void *stackptr);

	gdt_directory *directory_ptr();
	gdt_entry *entry_ptr();

private:
	size_t entry_i;
	size_t fs_idx;
	gdt_entry entries[SEGMENT_MAX_ENTRIES];
	gdt_directory directory;
	tss_entry tss;
};

};
