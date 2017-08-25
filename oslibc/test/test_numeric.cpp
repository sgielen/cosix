#include <oslibc/numeric.h>
#include <string>
#include <catch.hpp>

TEST_CASE("itoa") {
	char buf[65];

	/* positive values */
	REQUIRE(itoa_s(0, buf, sizeof(buf), 2) == std::string("0"));
	REQUIRE(i64toa_s(0, buf, sizeof(buf), 8) == std::string("0"));
	REQUIRE(uitoa_s(0, buf, sizeof(buf), 10) == std::string("0"));
	REQUIRE(ui64toa_s(0, buf, sizeof(buf), 16) == std::string("0"));
	REQUIRE(itoa_s(0, buf, sizeof(buf), 36) == std::string("0"));

	REQUIRE(itoa_s(1, buf, sizeof(buf), 2) == std::string("1"));
	REQUIRE(i64toa_s(1, buf, sizeof(buf), 8) == std::string("1"));
	REQUIRE(uitoa_s(1, buf, sizeof(buf), 10) == std::string("1"));
	REQUIRE(ui64toa_s(1, buf, sizeof(buf), 16) == std::string("1"));
	REQUIRE(itoa_s(1, buf, sizeof(buf), 36) == std::string("1"));

	REQUIRE(itoa_s(2, buf, sizeof(buf), 2) == std::string("10"));
	REQUIRE(i64toa_s(2, buf, sizeof(buf), 8) == std::string("2"));
	REQUIRE(uitoa_s(2, buf, sizeof(buf), 10) == std::string("2"));
	REQUIRE(ui64toa_s(2, buf, sizeof(buf), 16) == std::string("2"));
	REQUIRE(itoa_s(2, buf, sizeof(buf), 36) == std::string("2"));

	REQUIRE(itoa_s(10, buf, sizeof(buf), 2) == std::string("1010"));
	REQUIRE(i64toa_s(10, buf, sizeof(buf), 8) == std::string("12"));
	REQUIRE(uitoa_s(10, buf, sizeof(buf), 10) == std::string("10"));
	REQUIRE(ui64toa_s(10, buf, sizeof(buf), 16) == std::string("a"));
	REQUIRE(itoa_s(10, buf, sizeof(buf), 36) == std::string("a"));

	REQUIRE(itoa_s(35, buf, sizeof(buf), 2) == std::string("100011"));
	REQUIRE(i64toa_s(35, buf, sizeof(buf), 8) == std::string("43"));
	REQUIRE(uitoa_s(35, buf, sizeof(buf), 10) == std::string("35"));
	REQUIRE(ui64toa_s(35, buf, sizeof(buf), 16) == std::string("23"));
	REQUIRE(itoa_s(35, buf, sizeof(buf), 36) == std::string("z"));

	REQUIRE(itoa_s(452, buf, sizeof(buf), 2) == std::string("111000100"));
	REQUIRE(i64toa_s(452, buf, sizeof(buf), 8) == std::string("704"));
	REQUIRE(uitoa_s(452, buf, sizeof(buf), 10) == std::string("452"));
	REQUIRE(ui64toa_s(452, buf, sizeof(buf), 16) == std::string("1c4"));
	REQUIRE(itoa_s(452, buf, sizeof(buf), 32) == std::string("e4"));

	/* negative values */
	REQUIRE(itoa_s(-1, buf, sizeof(buf), 2) == std::string("-1"));
	REQUIRE(i64toa_s(-1, buf, sizeof(buf), 8) == std::string("-1"));
	REQUIRE(itoa_s(-1, buf, sizeof(buf), 10) == std::string("-1"));
	REQUIRE(i64toa_s(-1, buf, sizeof(buf), 16) == std::string("-1"));
	REQUIRE(itoa_s(-1, buf, sizeof(buf), 32) == std::string("-1"));

	REQUIRE(itoa_s(-2, buf, sizeof(buf), 2) == std::string("-10"));
	REQUIRE(i64toa_s(-2, buf, sizeof(buf), 8) == std::string("-2"));
	REQUIRE(itoa_s(-2, buf, sizeof(buf), 10) == std::string("-2"));
	REQUIRE(i64toa_s(-2, buf, sizeof(buf), 16) == std::string("-2"));
	REQUIRE(itoa_s(-2, buf, sizeof(buf), 32) == std::string("-2"));

	REQUIRE(itoa_s(-452, buf, sizeof(buf), 2) == std::string("-111000100"));
	REQUIRE(i64toa_s(-452, buf, sizeof(buf), 8) == std::string("-704"));
	REQUIRE(itoa_s(-452, buf, sizeof(buf), 10) == std::string("-452"));
	REQUIRE(i64toa_s(-452, buf, sizeof(buf), 16) == std::string("-1c4"));
	REQUIRE(itoa_s(-452, buf, sizeof(buf), 32) == std::string("-e4"));

	/* values that don't fit */
	REQUIRE(itoa_s(0, buf, 0, 2) == nullptr);
	REQUIRE(i64toa_s(0, buf, 0, 10) == nullptr);
	REQUIRE(uitoa_s(0, buf, 0, 32) == nullptr);

	REQUIRE(itoa_s(0, buf, 1, 2) == nullptr);
	REQUIRE(i64toa_s(0, buf, 1, 10) == nullptr);
	REQUIRE(uitoa_s(0, buf, 1, 32) == nullptr);

	REQUIRE(i64toa_s(1, buf, 2, 2) == std::string("1"));
	REQUIRE(ui64toa_s(1, buf, 2, 2) == std::string("1"));

	REQUIRE(i64toa_s(-1, buf, 2, 2) == nullptr);
	REQUIRE(i64toa_s(-1, buf, 3, 2) == std::string("-1"));

	REQUIRE(i64toa_s(2, buf, 2, 2) == nullptr);
	REQUIRE(i64toa_s(2, buf, 2, 3) == std::string("2"));

	/* wrong input */
	REQUIRE(i64toa_s(1, nullptr, 2, 2) == nullptr);
	REQUIRE(i64toa_s(1, buf, 2, 0) == nullptr);
	REQUIRE(i64toa_s(1, buf, 2, 37) == nullptr);
}

TEST_CASE("atoi") {
	int32_t res32 = -1;
	int64_t res64 = -1;

	/* empty or incomplete string */
	REQUIRE(!atoi_s("", &res32, 10));
	REQUIRE(!atoi64_s("", &res64, 10));
	REQUIRE(!atoi_s("-", &res32, 16));
	REQUIRE(!atoi64_s("-", &res64, 16));

	/* positive values */
	REQUIRE(atoi_s("0", &res32, 2));
	REQUIRE(res32 == 0);
	REQUIRE(atoi64_s("0", &res64, 8));
	REQUIRE(res64 == 0);
	REQUIRE(atoi_s("0", &res32, 10));
	REQUIRE(res32 == 0);
	REQUIRE(atoi64_s("0", &res64, 16));
	REQUIRE(res64 == 0);
	REQUIRE(atoi_s("0", &res32, 32));
	REQUIRE(res32 == 0);

	REQUIRE(atoi_s("1", &res32, 2));
	REQUIRE(res32 == 1);
	REQUIRE(atoi64_s("1", &res64, 8));
	REQUIRE(res64 == 1);
	REQUIRE(atoi_s("1", &res32, 10));
	REQUIRE(res32 == 1);
	REQUIRE(atoi64_s("1", &res64, 16));
	REQUIRE(res64 == 1);
	REQUIRE(atoi_s("1", &res32, 32));
	REQUIRE(res32 == 1);

	REQUIRE(atoi_s("10", &res32, 2));
	REQUIRE(res32 == 2);
	REQUIRE(atoi64_s("2", &res64, 8));
	REQUIRE(res64 == 2);
	REQUIRE(atoi_s("2", &res32, 10));
	REQUIRE(res32 == 2);
	REQUIRE(atoi64_s("2", &res64, 16));
	REQUIRE(res64 == 2);
	REQUIRE(atoi_s("2", &res32, 32));
	REQUIRE(res32 == 2);

	REQUIRE(atoi_s("1010", &res32, 2));
	REQUIRE(res32 == 10);
	REQUIRE(atoi64_s("12", &res64, 8));
	REQUIRE(res64 == 10);
	REQUIRE(atoi_s("10", &res32, 10));
	REQUIRE(res32 == 10);
	REQUIRE(atoi64_s("a", &res64, 16));
	REQUIRE(res64 == 10);
	REQUIRE(atoi_s("A", &res32, 32));
	REQUIRE(res32 == 10);

	REQUIRE(atoi_s("100011", &res32, 2));
	REQUIRE(res32 == 35);
	REQUIRE(atoi64_s("43", &res64, 8));
	REQUIRE(res64 == 35);
	REQUIRE(atoi_s("35", &res32, 10));
	REQUIRE(res32 == 35);
	REQUIRE(atoi64_s("23", &res64, 16));
	REQUIRE(res64 == 35);
	REQUIRE(atoi_s("z", &res32, 36));
	REQUIRE(res32 == 35);

	REQUIRE(atoi_s("111000100", &res32, 2));
	REQUIRE(res32 == 452);
	REQUIRE(atoi64_s("704", &res64, 8));
	REQUIRE(res64 == 452);
	REQUIRE(atoi_s("452", &res32, 10));
	REQUIRE(res32 == 452);
	REQUIRE(atoi64_s("1c4", &res64, 16));
	REQUIRE(res64 == 452);
	REQUIRE(atoi_s("E4", &res32, 32));
	REQUIRE(res32 == 452);

	/* negative values */
	REQUIRE(atoi_s("-1", &res32, 2));
	REQUIRE(res32 == -1);
	REQUIRE(atoi64_s("-1", &res64, 8));
	REQUIRE(res64 == -1);
	REQUIRE(atoi_s("-1", &res32, 10));
	REQUIRE(res32 == -1);
	REQUIRE(atoi64_s("-1", &res64, 16));
	REQUIRE(res64 == -1);
	REQUIRE(atoi_s("-1", &res32, 32));
	REQUIRE(res32 == -1);

	REQUIRE(atoi_s("-10", &res32, 2));
	REQUIRE(res32 == -2);
	REQUIRE(atoi64_s("-2", &res64, 8));
	REQUIRE(res64 == -2);
	REQUIRE(atoi_s("-2", &res32, 10));
	REQUIRE(res32 == -2);
	REQUIRE(atoi64_s("-2", &res64, 16));
	REQUIRE(res64 == -2);
	REQUIRE(atoi_s("-2", &res32, 32));
	REQUIRE(res32 == -2);

	REQUIRE(atoi_s("-111000100", &res32, 2));
	REQUIRE(res32 == -452);
	REQUIRE(atoi64_s("-704", &res64, 8));
	REQUIRE(res64 == -452);
	REQUIRE(atoi_s("-452", &res32, 10));
	REQUIRE(res32 == -452);
	REQUIRE(atoi64_s("-1C4", &res64, 16));
	REQUIRE(res64 == -452);
	REQUIRE(atoi_s("-e4", &res32, 32));
	REQUIRE(res32 == -452);

	/* values that don't fit */
	REQUIRE(atoi_s("7fffffff", &res32, 16));
	REQUIRE(res32 == 0x7fffffff);
	REQUIRE(!atoi_s("80000000", &res32, 16));
	REQUIRE(atoi64_s("7fffffffffffffff", &res64, 16));
	REQUIRE(res64 == 0x7fffffffffffffff);
	REQUIRE(!atoi64_s("8000000000000000", &res64, 16));
	REQUIRE(atoi_s("-7fffffff", &res32, 16));
	REQUIRE(res32 == -0x7fffffff);
	REQUIRE(!atoi_s("-80000000", &res32, 16));
	REQUIRE(atoi64_s("-7fffffffffffffff", &res64, 16));
	REQUIRE(res64 == -0x7fffffffffffffff);
	REQUIRE(!atoi64_s("-8000000000000000", &res64, 16));

	/* values that don't exist in base */
	REQUIRE(!atoi_s("a", &res32, 10));
	REQUIRE(!atoi64_s("5a", &res64, 10));
	REQUIRE(!atoi_s("!", &res32, 16));
	REQUIRE(!atoi64_s("5!", &res64, 16));
	REQUIRE(!atoi_s("g", &res32, 16));
	REQUIRE(!atoi64_s("5g", &res64, 16));

	/* incorrect format */
	REQUIRE(!atoi_s("--1", &res32, 10));
	REQUIRE(!atoi64_s("1 2", &res64, 10));
	REQUIRE(!atoi64_s("1.2", &res64, 10));
}
