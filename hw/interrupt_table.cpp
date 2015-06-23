#include "interrupt_table.hpp"
#include <oslibc/string.h>

using namespace cloudos;

extern "C"
void idt_load(idt_directory*);

interrupt_table::interrupt_table() {
	directory.size = sizeof(entries) - 1;
	// use uint64_t to make the testing code compile on a x86-64 arch
	directory.offset = reinterpret_cast<uint64_t>(&entries[0]);
	clear();
}

void interrupt_table::load()
{
	idt_load(&directory);
}

void interrupt_table::clear()
{
	memset(&entries, 0, sizeof(entries));
}

void interrupt_table::set_entry(uint8_t num, uint32_t base, uint16_t segment, uint8_t flags)
{
	idt_entry &entry = entries[num];
	entry.offset_lower = base & 0xffff;
	entry.offset_upper = (base >> 16) & 0xffff;

	entry.segment = segment;
	entry.zero = 0;
	entry.type_attr = flags;
}

idt_directory *interrupt_table::directory_ptr() {
	return &directory;
}

idt_entry *interrupt_table::entry_ptr() {
	return &entries[0];
}
