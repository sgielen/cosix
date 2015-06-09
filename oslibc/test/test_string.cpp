#include <oslibc/string.h>
#include <string.h>
#include <catch.hpp>

TEST_CASE() {
	REQUIRE(strlen("") == 0);
	REQUIRE(strlen("a") == 1);
	REQUIRE(strlen("\xff\xfe\xf0\x0f \x03\x02\x01") == 8);
}
