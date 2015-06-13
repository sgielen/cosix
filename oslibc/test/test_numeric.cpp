#include <oslibc/numeric.h>
#include <string>
#include <catch.hpp>

TEST_CASE() {
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
