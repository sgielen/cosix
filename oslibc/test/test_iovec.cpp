#include <oslibc/iovec.hpp>
#include <memory/allocation.hpp>
#include <catch.hpp>

using namespace cloudos;

TEST_CASE("veccpy to iovec") {
	char buf[14];
	memset(buf, 'z', sizeof(buf));

	cloudabi_iovec_t dst;
	dst.buf = buf;
	dst.buf_len = 12; // Lower than sizeof(buf) on purpose

	// Copy from const char*
	REQUIRE(veccpy(&dst, 1, "Hello world", 5, 0) == 5);
	REQUIRE(memcmp(buf, "Hellozzzzzzzzz", sizeof(buf)) == 0);

	memset(buf, 'y', sizeof(buf));
	REQUIRE(veccpy(&dst, 1, "Hello world", 5, VECCPY_RETURN_TRUNCATED_BYTES) == 0);
	REQUIRE(memcmp(buf, "Helloyyyyyyyyy", sizeof(buf)) == 0);

	// Copy from Blk
	Blk source;
	char teststr[] = "Test string";
	source.ptr = teststr;
	source.size = 4;

	memset(buf, 'x', sizeof(buf));
	REQUIRE(veccpy(&dst, 1, source, 0) == 4);
	REQUIRE(memcmp(buf, "Testxxxxxxxxxx", sizeof(buf)) == 0);

	memset(buf, 'w', sizeof(buf));
	REQUIRE(veccpy(&dst, 1, source, VECCPY_RETURN_TRUNCATED_BYTES) == 0);
	REQUIRE(memcmp(buf, "Testwwwwwwwwww", sizeof(buf)) == 0);

	// Copy truncated
	memset(buf, 'v', sizeof(buf));
	REQUIRE(veccpy(&dst, 1, "Hello world, test", 18, 0) == 12);
	REQUIRE(memcmp(&buf, "Hello world,vv", sizeof(buf)) == 0);

	memset(buf, 'u', sizeof(buf));
	REQUIRE(veccpy(&dst, 1, "Hello world, test", 18, VECCPY_RETURN_TRUNCATED_BYTES) == 6);
	REQUIRE(memcmp(&buf, "Hello world,uu", sizeof(buf)) == 0);

	// Copy over two iovecs
	cloudabi_iovec_t dsts[2];
	dsts[0].buf = buf;
	dsts[0].buf_len = 4;
	char buf2[4];
	dsts[1].buf = buf2;
	dsts[1].buf_len = 4;
	memset(buf, 't', sizeof(buf));
	memset(buf2, 's', sizeof(buf2));
	REQUIRE(veccpy(dsts, 2, "Foo bar baz quux", 16, 0) == 8);
	REQUIRE(memcmp(buf, "Foo tttttttttt", sizeof(buf)) == 0);
	REQUIRE(memcmp(buf2, "bar ", sizeof(buf2)) == 0);

	// Copy truncated
	memset(buf, 'r', sizeof(buf));
	memset(buf2, 'q', sizeof(buf2));
	REQUIRE(veccpy(dsts, 2, "Baz quux bar", 12, VECCPY_RETURN_TRUNCATED_BYTES) == 4);
	REQUIRE(memcmp(buf, "Baz rrrrrrrrrr", sizeof(buf)) == 0);
	REQUIRE(memcmp(buf2, "quux", sizeof(buf2)) == 0);
}

TEST_CASE("veccpy from iovec") {
	char buf[14];

	cloudabi_ciovec_t src;
	src.buf = buf;
	src.buf_len = 5;

	// Copy to char*
	memcpy(buf, "Hello", 5);
	char dst[12];
	memset(dst, 'a', sizeof(dst));
	REQUIRE(veccpy(dst, sizeof(dst), &src, 1, 0) == 5);
	REQUIRE(memcmp(dst, "Helloaaaaaaa", sizeof(dst)) == 0);

	memset(dst, 'b', sizeof(dst));
	REQUIRE(veccpy(dst, sizeof(dst), &src, 1, VECCPY_RETURN_TRUNCATED_BYTES) == 0);
	REQUIRE(memcmp(dst, "Hellobbbbbbb", sizeof(dst)) == 0);

	// Copy truncated
	memset(dst, 'c', sizeof(dst));
	REQUIRE(veccpy(dst, 4, &src, 1, 0) == 4);
	REQUIRE(memcmp(dst, "Hellcccccccc", sizeof(dst)) == 0);

	memset(dst, 'd', sizeof(dst));
	REQUIRE(veccpy(dst, 4, &src, 1, VECCPY_RETURN_TRUNCATED_BYTES) == 1);
	REQUIRE(memcmp(dst, "Helldddddddd", sizeof(dst)) == 0);

	// Copy to Blk
	Blk b;
	b.ptr = dst;
	b.size = sizeof(dst);

	memset(dst, 'e', sizeof(dst));
	REQUIRE(veccpy(b, &src, 1, 0) == 5);
	REQUIRE(memcmp(dst, "Helloeeeeeee", sizeof(dst)) == 0);

	memset(dst, 'f', sizeof(dst));
	REQUIRE(veccpy(b, &src, 1, VECCPY_RETURN_TRUNCATED_BYTES) == 0);
	REQUIRE(memcmp(dst, "Hellofffffff", sizeof(dst)) == 0);

	// Copy truncated
	memset(dst, 'g', sizeof(dst));
	b.size = 4;
	REQUIRE(veccpy(b, &src, 1, 0) == 4);
	REQUIRE(memcmp(dst, "Hellgggggggg", sizeof(dst)) == 0);

	memset(dst, 'h', sizeof(dst));
	REQUIRE(veccpy(b, &src, 1, VECCPY_RETURN_TRUNCATED_BYTES) == 1);
	REQUIRE(memcmp(dst, "Hellhhhhhhhh", sizeof(dst)) == 0);

	// Copy from two iovecs
	cloudabi_ciovec_t srcs[2];
	srcs[0].buf = buf;
	srcs[0].buf_len = 4;
	char buf2[4];
	srcs[1].buf = buf2;
	srcs[1].buf_len = 4;
	memcpy(buf, "Bar ", 4);
	memcpy(buf2, "quux", 4);

	memset(dst, 'i', sizeof(dst));
	REQUIRE(veccpy(dst, sizeof(dst), srcs, 2, 0) == 8);
	REQUIRE(memcmp(dst, "Bar quuxiiii", sizeof(dst)) == 0);

	memset(dst, 'j', sizeof(dst));
	REQUIRE(veccpy(dst, sizeof(dst), srcs, 2, VECCPY_RETURN_TRUNCATED_BYTES) == 0);
	REQUIRE(memcmp(dst, "Bar quuxjjjj", sizeof(dst)) == 0);

	// Copy truncated
	memset(dst, 'k', sizeof(dst));
	REQUIRE(veccpy(dst, 6, srcs, 2, VECCPY_RETURN_TRUNCATED_BYTES) == 2);
	REQUIRE(memcmp(dst, "Bar qukkkkkk", sizeof(dst)) == 0);
}
