#include <memory/allocator.hpp>
#include <hw/multiboot.hpp>
#include <catch.hpp>

#define REQUIRE_ALLOC(x, y) \
	REQUIRE(alloc.allocate(x) == reinterpret_cast<void*>(y))

TEST_CASE("allocations from a single block") {
	cloudos::memory_map_entry mmap[2];
	mmap[0].entry_size = 20;
	mmap[0].mem_base.addr = reinterpret_cast<void*>(0x1000);
	mmap[0].mem_length = 0x200;
	mmap[0].mem_type = 1; /* available */
	mmap[1].entry_size = 0;

	cloudos::allocator alloc(reinterpret_cast<void*>(0x1000), mmap, 20);
	REQUIRE_ALLOC(0x20, 0x1000);
	REQUIRE_ALLOC(0x80, 0x1020);
	REQUIRE_ALLOC(0x01, 0x10a0);
}

TEST_CASE("allocations from two blocks") {
	cloudos::memory_map_entry mmap[3];
	mmap[0].entry_size = 20;
	mmap[0].mem_base.addr = reinterpret_cast<void*>(0x1000);
	mmap[0].mem_length = 0x200;
	mmap[0].mem_type = 1;
	mmap[1].entry_size = 20;
	mmap[1].mem_base.addr = reinterpret_cast<void*>(0x4000);
	mmap[1].mem_length = 0x200;
	mmap[1].mem_type = 1;
	mmap[2].entry_size = 0;

	cloudos::allocator alloc(reinterpret_cast<void*>(0x1000), mmap, 40);

	SECTION("exact fit") {
		REQUIRE_ALLOC(0x20, 0x1000);
		REQUIRE_ALLOC(0x1e0, 0x1020);
		REQUIRE_ALLOC(0x1e0, 0x4000);
		REQUIRE_ALLOC(0x20, 0x41e0);
	}

	SECTION("loose fit") {
		REQUIRE_ALLOC(0x20, 0x1000);
		REQUIRE_ALLOC(0x100, 0x1020);
		REQUIRE_ALLOC(0x100, 0x4000);
		REQUIRE_ALLOC(0x20, 0x4100);
	}

	SECTION("out of memory with bytes left") {
		REQUIRE_ALLOC(0x200, 0x1000);
		REQUIRE_ALLOC(0x180, 0x4000);
		REQUIRE_ALLOC(0x200, 0x0);
	}

	SECTION("out of memory without bytes left") {
		REQUIRE_ALLOC(0x200, 0x1000);
		REQUIRE_ALLOC(0x200, 0x4000);
		REQUIRE_ALLOC(0x200, 0x0);
	}

	SECTION("allocation that will never fit") {
		REQUIRE_ALLOC(0x300, 0x0);
	}
}

TEST_CASE("allocations that have to skip a block") {
	cloudos::memory_map_entry mmap[4];
	mmap[0].entry_size = 20;
	mmap[0].mem_base.addr = reinterpret_cast<void*>(0x1000);
	mmap[0].mem_length = 0x200;
	mmap[0].mem_type = 1;
	mmap[1].entry_size = 20;
	mmap[1].mem_base.addr = reinterpret_cast<void*>(0x2000);
	mmap[1].mem_length = 0x10;
	mmap[1].mem_type = 1;
	mmap[2].entry_size = 20;
	mmap[2].mem_base.addr = reinterpret_cast<void*>(0x4000);
	mmap[2].mem_length = 0x200;
	mmap[2].mem_type = 1;
	mmap[3].entry_size = 0;

	cloudos::allocator alloc(reinterpret_cast<void*>(0x1000), mmap, 60);
	REQUIRE_ALLOC(0x20, 0x1000);
	REQUIRE_ALLOC(0x200, 0x4000);
}

TEST_CASE("allocations that have to skip a reserved block") {
	cloudos::memory_map_entry mmap[4];
	mmap[0].entry_size = 20;
	mmap[0].mem_base.addr = reinterpret_cast<void*>(0x1000);
	mmap[0].mem_length = 0x200;
	mmap[0].mem_type = 1;
	mmap[1].entry_size = 20;
	mmap[1].mem_base.addr = reinterpret_cast<void*>(0x2000);
	mmap[1].mem_length = 0x200;
	mmap[1].mem_type = 2;
	mmap[2].entry_size = 20;
	mmap[2].mem_base.addr = reinterpret_cast<void*>(0x4000);
	mmap[2].mem_length = 0x200;
	mmap[2].mem_type = 1;
	mmap[3].entry_size = 0;

	cloudos::allocator alloc(reinterpret_cast<void*>(0x1000), mmap, 60);
	REQUIRE_ALLOC(0x20, 0x1000);
	REQUIRE_ALLOC(0x200, 0x4000);
}
