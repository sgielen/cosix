#include <hw/segments.hpp>
#include <stdlib.h>
#include <catch.hpp>

TEST_CASE( "hw/segments" ) {
	using cloudos::segment_table;
	using cloudos::gdt_directory;
	using cloudos::gdt_entry;

	segment_table table;

	SECTION( "initial state" ) {
		gdt_directory *ptr = table.directory_ptr();
		// the directory seems correct
		REQUIRE(ptr->size == SEGMENT_MAX_ENTRIES * sizeof(gdt_entry) - 1);
		REQUIRE(ptr->offset != 0);

		// there must be no segments yet
		REQUIRE(table.num_entries() == 0);

		// the present bit must be unset for all segments initially
		gdt_entry *entries = table.entry_ptr();
		for(size_t i = 0; i < SEGMENT_MAX_ENTRIES; ++i) {
			REQUIRE((entries[i].access & 128) == 0);
		}
	}

	SECTION( "adding two entries, then clearing" ) {
		// normally the first entry is always null and ignored, but we ignore that
		// for testing
		table.add_entry(0x12345678, 0x87654321,
			SEGMENT_EXEC | SEGMENT_PRESENT,
			SEGMENT_PAGE_GRANULARITY | SEGMENT_16BIT);
		table.add_entry(0xabcdef00, 0x99fedcba,
			SEGMENT_RW | SEGMENT_ALWAYS | SEGMENT_PRESENT,
			SEGMENT_BYTE_GRANULARITY | SEGMENT_32BIT);

		REQUIRE(table.num_entries() == 2);
		gdt_directory *ptr = table.directory_ptr();
		REQUIRE(ptr->size == SEGMENT_MAX_ENTRIES * sizeof(gdt_entry) - 1);
		REQUIRE(ptr->offset != 0);

		gdt_entry *entries = table.entry_ptr();
		REQUIRE(entries[0].limit_lower == 0x5678);
		REQUIRE(entries[0].base_lower == 0x4321);
		REQUIRE(entries[0].base_middle == 0x65);
		REQUIRE(entries[0].access == 136);
		REQUIRE(entries[0].flags == 132);
		REQUIRE(entries[0].base_upper == 0x87);

		REQUIRE(entries[1].limit_lower == 0xef00);
		REQUIRE(entries[1].base_lower == 0xdcba);
		REQUIRE(entries[1].base_middle == 0xfe);
		REQUIRE(entries[1].access == 146);
		REQUIRE(entries[1].flags == 77);
		REQUIRE(entries[1].base_upper == 0x99);

		// and the rest must be non-present
		for(size_t i = 2; i < SEGMENT_MAX_ENTRIES; ++i) {
			REQUIRE((entries[i].access & 128) == 0);
		}

		table.clear();
		REQUIRE(table.num_entries() == 0);
		for(size_t i = 0; i < SEGMENT_MAX_ENTRIES; ++i) {
			REQUIRE((entries[i].access & 128) == 0);
		}
	}
}
