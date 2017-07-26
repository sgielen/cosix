#include <oslibc/string.h>
#include <catch.hpp>

TEST_CASE("strlen") {
	REQUIRE(strlen("") == 0);
	REQUIRE(strlen("a") == 1);
	REQUIRE(strlen("\xff\xfe\xf0\x0f \x03\x02\x01") == 8);
}

TEST_CASE("memset") {
	uint8_t buf[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
	uint8_t res[] = {0, 1, 9, 9, 9, 9, 9, 7, 8};
	REQUIRE(memset(buf + 2, 9, sizeof(buf) - 4) == &buf[2]);
	for(size_t i = 0; i < sizeof(buf); ++i) {
		REQUIRE(buf[i] == res[i]);
	}
}

TEST_CASE("memcpy") {
	uint8_t buf[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
	uint8_t buf2[] = {4, 3, 2, 1};
	uint8_t res[] = {0, 1, 2, 4, 3, 2, 1, 7, 8};
	REQUIRE(memcpy(buf + 3, buf2, 4) == &buf[3]);
	for(size_t i = 0; i < sizeof(buf); ++i) {
		REQUIRE(buf[i] == res[i]);
	}
}

TEST_CASE("strcmp") {
	CHECK(strcmp("", "") == 0);
	CHECK(strcmp("a", "b") == -1);
	CHECK(strcmp("b", "a") == 1);
	CHECK(strcmp("a", "") == 1);
	CHECK(strcmp("", "a") == -1);
	CHECK(strcmp("ab", "aa") == 1);
	CHECK(strcmp("", "abc") == -1);
	CHECK(strcmp("abc", "") == 1);
}

TEST_CASE("memcmp") {
	CHECK(memcmp("", "", 0) == 0);
	CHECK(memcmp("", "", 1) == 0);
	CHECK(memcmp("a", "b", 0) == 0);
	CHECK(memcmp("a", "b", 1) == -1);
	CHECK(memcmp("a", "b", 2) == -1);
	CHECK(memcmp("b", "a", 0) == 0);
	CHECK(memcmp("b", "a", 1) == 1);
	CHECK(memcmp("b", "a", 2) == 1);
	CHECK(memcmp("a", "", 0) == 0);
	CHECK(memcmp("a", "", 1) == 1);
	CHECK(memcmp("", "a", 0) == 0);
	CHECK(memcmp("", "a", 1) == -1);
	CHECK(memcmp("ab", "aa", 0) == 0);
	CHECK(memcmp("ab", "aa", 1) == 0);
	CHECK(memcmp("ab", "aa", 2) == 1);
	CHECK(memcmp("ab", "aa", 3) == 1);
	CHECK(memcmp("", "abc", 0) == 0);
	CHECK(memcmp("", "abc", 1) == -1);
	CHECK(memcmp("abc", "", 0) == 0);
	CHECK(memcmp("abc", "", 1) == 1);
}
