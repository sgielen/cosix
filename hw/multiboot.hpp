#pragma once
#include <stdint.h>
#include <stddef.h>

namespace cloudos {

#define MULTIBOOT_MAGIC 0x2BADB002

struct boot_info;

struct memory_map_entry {
	uint32_t entry_size;
	uint64_t mem_base;
	uint64_t mem_length;
	uint32_t mem_type;
}; // packed struct, but aligned by itself

struct multiboot_module {
	uint32_t mmo_start;
	uint32_t mmo_end;
	uint32_t mmo_cmdline; /* char* */
	uint32_t mmo_reserved;
};

struct multiboot_info {
	multiboot_info(void *addr, uint32_t magic = MULTIBOOT_MAGIC);

	bool is_valid() const;
	bool mem_amount(uint32_t *mem_lower, uint32_t *mem_upper) const;
	size_t memory_map(memory_map_entry **first) const;

	// Returns the virtual address pointing at a list of multiboot_modules
	multiboot_module *module_base_address() const;

private:
	boot_info *bi;
	bool valid;
};

}
