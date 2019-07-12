#include <oslibc/uuid.hpp>
#include <catch.hpp>
#include <string.h>

TEST_CASE("generate uuid") {
	// initialize random generator
	cloudos::rng rng;
	rng.seed(time(nullptr));
	cloudos::global_state_->random = &rng;

	// We generate v4 variant 1 random UUIDs
	uint8_t uuid1[16];
	cloudos::generate_random_uuid(uuid1, sizeof(uuid1));
	{
		uint8_t uuid_version = (uuid1[6] & 0xf0) >> 4;
		REQUIRE(uuid_version == 4);
		uint8_t uuid_variant = (uuid1[8] & 0xc0) >> 6;
		REQUIRE(uuid_variant == 2 /* variant 1 == 0b10 == 2 */);
	}

	{
		uint8_t uuid2[16];
		cloudos::generate_random_uuid(uuid2, sizeof(uuid2));
		uint8_t uuid_version = (uuid2[6] & 0xf0) >> 4;
		REQUIRE(uuid_version == 4);
		uint8_t uuid_variant = (uuid2[8] & 0xc0) >> 6;
		REQUIRE(uuid_variant == 2 /* variant 1 == 0b10 == 2 */);
		// UUIDs must be different
		REQUIRE(memcmp(uuid1, uuid2, sizeof(uuid1)) != 0);
	}

	cloudos::global_state_->random = nullptr;
}

TEST_CASE("print uuid") {
	uint8_t uuid[16];
	char buf[37];

	memset(uuid, 0, sizeof(uuid));
	cloudos::uuid_to_string(uuid, sizeof(uuid), buf, sizeof(buf));
	REQUIRE(std::string(buf) == "00000000-0000-0000-0000-000000000000");

	memcpy(uuid, "\x00\x11\x22\x33\x44\x55\x66\x77\x88\x99\xaa\xbb\xcc\xdd\xee\xff", sizeof(uuid));
	cloudos::uuid_to_string(uuid, sizeof(uuid), buf, sizeof(buf));
	REQUIRE(std::string(buf) == "00112233-4455-6677-8899-aabbccddeeff");

	memcpy(uuid, "\x12\x34\x56\x78\x9a\xbc\xde\xf0\x12\x34\x56\x78\x9a\xbc\xde\xf0", sizeof(uuid));
	cloudos::uuid_to_string(uuid, sizeof(uuid), buf, sizeof(buf));
	REQUIRE(std::string(buf) == "12345678-9abc-def0-1234-56789abcdef0");
}

TEST_CASE("parse uuid") {
	uint8_t buf[16];

	// not a valid uuid
	std::string uuid = "";
	REQUIRE(cloudos::string_to_uuid(uuid.c_str(), uuid.size(), buf, sizeof(buf)) == false);

	uuid = "00000000-0000-0000-0000-00000000000"; // 1 byte too short
	REQUIRE(cloudos::string_to_uuid(uuid.c_str(), uuid.size(), buf, sizeof(buf)) == false);

	uuid = "00000000-0000-0000-00000-00000000000"; // dash in the wrong place
	REQUIRE(cloudos::string_to_uuid(uuid.c_str(), uuid.size(), buf, sizeof(buf)) == false);

	uuid = "00000000-0000-0000-0000-0000000000000"; // 1 byte too long
	REQUIRE(cloudos::string_to_uuid(uuid.c_str(), uuid.size(), buf, sizeof(buf)) == false);

	// correct uuid
	uuid = "00000000-0000-0000-0000-000000000000";
	REQUIRE(cloudos::string_to_uuid(uuid.c_str(), uuid.size(), buf, sizeof(buf)) == true);
	REQUIRE(memcmp(buf, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 16) == 0);

	// uuid with characters
	uuid = "01234567-89ab-cdef-0123-456789abcdef";
	REQUIRE(cloudos::string_to_uuid(uuid.c_str(), uuid.size(), buf, sizeof(buf)) == true);
	REQUIRE(memcmp(buf, "\x01\x23\x45\x67\x89\xab\xcd\xef\x01\x23\x45\x67\x89\xab\xcd\xef", 16) == 0);

	// uuid with uppercase characters (must not be generated but should be accepted)
	uuid = "01234567-89ab-CDeF-0123-456789AbcDef";
	REQUIRE(cloudos::string_to_uuid(uuid.c_str(), uuid.size(), buf, sizeof(buf)) == true);
	REQUIRE(memcmp(buf, "\x01\x23\x45\x67\x89\xab\xcd\xef\x01\x23\x45\x67\x89\xab\xcd\xef", 16) == 0);
}
