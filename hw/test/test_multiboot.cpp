#include <hw/multiboot.hpp>
#include <stdlib.h>
#include <catch.hpp>

TEST_CASE( "hw/multiboot" ) {
	using cloudos::multiboot_info;

	uint32_t buf[255];
	memset(&buf[0], 0x00, sizeof(buf));

	SECTION( "magic checking" ) {
		multiboot_info mi1(buf, 0x00000000);
		REQUIRE(mi1.is_valid() == false);

		multiboot_info mi2(buf, 0x1BADB002);
		REQUIRE(mi2.is_valid() == false);

		multiboot_info mi3(buf, 0x2BADB002);
		REQUIRE(mi3.is_valid() == true);
	}

	SECTION( "no flags" ) {
		buf[0] = 0;
		multiboot_info mi(buf);
		REQUIRE(mi.is_valid());
		uint32_t l, u;
		REQUIRE(mi.mem_amount(&l, &u) == false);
	}

	SECTION( "mem_info" ) {
		buf[0] = 1;
		buf[1] = 200;
		buf[2] = 400;
		multiboot_info mi(buf);
		REQUIRE(mi.is_valid());
		uint32_t l, u;
		REQUIRE(mi.mem_amount(&l, &u) == true);
		REQUIRE(l == 200);
		REQUIRE(u == 400);
	}
}
