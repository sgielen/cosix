#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cloudos {

struct idt_entry {
	uint16_t offset_lower;
	uint16_t segment;
	uint8_t zero;
	uint8_t type_attr;
	uint16_t offset_upper;
} __attribute__((packed));

struct idt_directory {
	uint16_t size;
	uint32_t offset;
} __attribute__((packed));

#define NUM_INTERRUPTS 256

enum interrupt_type {
	DIVISION_BY_ZERO = 0,
	DEBUG = 1,
	NON_MASKABLE = 2,
	BREAKPOINT = 3,
	INTO_DETECTED_OVERFLOW = 4,
	OUT_OF_BOUNDS = 5,
	INVALID_OPCODE = 6,
	NO_COPROCESSOR = 7,
	DOUBLE_FAULT = 8,
	COPROCESSOR_SEGMENT = 9,
	BAD_TSS = 10,
	SEGMENT_NOT_PRESENT = 11,
	STACK_FAULT = 12,
	GENERAL_PROTECTION_FAULT = 13,
	PAGE_FAULT = 14,
	UNKNOWN_INTERRUPT = 15,
	COPROCESSOR_FAULT = 16,
	ALIGNMENT_CHECK = 17,
	MACHINE_CHECK = 18,
};

struct interrupt_table {
	interrupt_table();

	void load();
	void clear();

	void set_entry(uint8_t num, uint32_t base, uint16_t segment, uint8_t flags);

	idt_directory *directory_ptr();
	idt_entry *entry_ptr();

private:
	idt_entry entries[NUM_INTERRUPTS];
	idt_directory directory;
};

};
