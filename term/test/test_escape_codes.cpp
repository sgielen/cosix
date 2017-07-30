#include <catch.hpp>
#include <oslibc/string.h>
#include <term/escape_codes.hpp>

TEST_CASE( "term/escape_codes/is_control_character" ) {
	using cloudos::is_control_character;

	// Empty string
	CHECK(is_control_character("\x00", 0) == false);

	// Control characters
	CHECK(is_control_character("\x00", 1) == true);
	CHECK(is_control_character("\n", 1) == true);
	CHECK(is_control_character("\x1b", 1) == true);
	CHECK(is_control_character("\x7f", 1) == true);

	// Not control characters
	CHECK(is_control_character(" ", 1) == false);
	CHECK(is_control_character("#", 1) == false);
	CHECK(is_control_character("~", 1) == false);
	CHECK(is_control_character("\x80", 1) == false);
	CHECK(is_control_character("\xff", 1) == false);

	// Control characters have a size of 1
	CHECK(is_control_character("\n\n", 2) == false);
}

TEST_CASE( "term/escape_codes/is_escape_sequence" ) {
	using cloudos::is_escape_sequence;

	// Empty string
	CHECK(is_escape_sequence("\x00", 0) == false);

	// Normal text
	CHECK(is_escape_sequence("a", 1) == false);
	CHECK(is_escape_sequence("f\x1bg", 3) == false);

	// Control characters
	CHECK(is_escape_sequence("\n", 1) == false);

	// Starts with ^[
	CHECK(is_escape_sequence("\x1b", 1) == true);
	CHECK(is_escape_sequence("\x1b[A", 3) == true);
}

TEST_CASE( "term/escape_codes/unicode_character_length" ) {
	using cloudos::unicode_character_length;

	// Not actually a unicode character
	CHECK(unicode_character_length("a", 0) == 0);
	CHECK(unicode_character_length("\0", 1) == 0);

	// Valid 1-byte character
	CHECK(unicode_character_length("abc", 3) == 1);
	CHECK(unicode_character_length("\n", 1) == 1);
	CHECK(unicode_character_length("\x1b\x1b\x1b", 3) == 1);

	// Invalid characters
	CHECK(unicode_character_length("\x80", 1) == -1); // 1000_0000
	CHECK(unicode_character_length("\xbf", 1) == -1); // 1011_1111
	CHECK(unicode_character_length("\xff", 1) == -1); // 1111_1111
	CHECK(unicode_character_length("\xf8", 1) == -1); // 1111_1000 (would indicate 5-byte character)
	CHECK(unicode_character_length("\xf8g", 2) == -1);

	// Valid 2-byte characters
	CHECK(unicode_character_length("\xc0\x80", 2) == 2); // 1100_0000 1000_0000
	CHECK(unicode_character_length("\xdf\xbf", 2) == 2); // 1101_1111 1011_1111
	CHECK(unicode_character_length("\xdf\xbfg", 3) == 2);

	// Invalid 2-byte characters
	CHECK(unicode_character_length("\xdf\xbf", 1) == -2); // incomplete
	CHECK(unicode_character_length("\xc0\x00", 2) == -1); // 1100_0000 0000_0000
	CHECK(unicode_character_length("\xdf\xff", 2) == -1); // 1101_1111 1111_1111
	CHECK(unicode_character_length("\xdf\x7f", 2) == -1); // 1101_1111 0111_1111
	CHECK(unicode_character_length("\xdf\xc0", 2) == -1); // 1101_1111 1100_0000
	CHECK(unicode_character_length("\xdf\xc0g", 3) == -1);

	// Valid 3-byte characters
	// 1110_0000 1000_0000 1000_0000
	CHECK(unicode_character_length("\xe0\x80\x80", 3) == 3);
	// 1110_1111 1011_1111 1011_1111
	CHECK(unicode_character_length("\xef\xbf\xbf", 3) == 3);
	CHECK(unicode_character_length("\xef\xbf\xbfg", 4) == 3);

	// Invalid 3-byte characters
	CHECK(unicode_character_length("\xe0\x80\x80", 1) == -2); // incomplete
	CHECK(unicode_character_length("\xe0\x80\x80", 2) == -2); // incomplete
	CHECK(unicode_character_length("\xe0\x00\x80", 3) == -1);
	CHECK(unicode_character_length("\xe0\x80\x00", 3) == -1);
	CHECK(unicode_character_length("\xe0\x80\xff", 3) == -1);
	CHECK(unicode_character_length("\xe0\xff\x80", 3) == -1);

	// Valid 4-byte characters
	// 1111_0000 1000_0000 1000_0000 1000_0000
	CHECK(unicode_character_length("\xf0\x80\x80\x80", 4) == 4);
	// 1111_0111 1011_1111 1011_1111 1011_1111
	CHECK(unicode_character_length("\xf7\xbf\xbf\xbf", 4) == 4);
	CHECK(unicode_character_length("\xf7\xbf\xbf\xbfg", 5) == 4);

	// Invalid 4-byte characters
	CHECK(unicode_character_length("\xf7\xbf\xbf\xbf", 1) == -2);
	CHECK(unicode_character_length("\xf7\xbf\xbf\xbf", 2) == -2);
	CHECK(unicode_character_length("\xf7\xbf\xbf\xbf", 3) == -2);
	CHECK(unicode_character_length("\xf7\xbf\xbf\x00", 4) == -1);
	CHECK(unicode_character_length("\xf7\xbf\x00\xbf", 4) == -1);
	CHECK(unicode_character_length("\xf7\x00\xbf\xbf", 4) == -1);
	CHECK(unicode_character_length("\xf7\xbf\xbf\xff", 4) == -1);
	CHECK(unicode_character_length("\xf7\xbf\xff\xbf", 4) == -1);
	CHECK(unicode_character_length("\xf7\xff\xbf\xbf", 4) == -1);
}

TEST_CASE( "term/escape_codes/escape_sequence_length" ) {
	using cloudos::escape_sequence_length;

	// No escape sequence
	CHECK(escape_sequence_length("", 0) == 0);
	CHECK(escape_sequence_length("a", 1) == 0);
	CHECK(escape_sequence_length("\n", 1) == 0);

	// Incomplete escape sequence
	CHECK(escape_sequence_length("\x1b", 1) == -1);
	CHECK(escape_sequence_length("\x1bOP", 1) == -1);

	// Short escape sequence
	CHECK(escape_sequence_length("\x1bOP", 3) == 3);
	CHECK(escape_sequence_length("\x1b[H", 3) == 3);
	CHECK(escape_sequence_length("\x1bOPh", 4) == 3);
	CHECK(escape_sequence_length("\x1b[Ha", 4) == 3);

	// Incomplete escape sequence
	CHECK(escape_sequence_length("\x1b[", 2) == -1);
	CHECK(escape_sequence_length("\x1b[1A", 2) == -1);

	// Escape sequence with one parameter
	CHECK(escape_sequence_length("\x1b[1A", 4) == 4);
	CHECK(escape_sequence_length("\x1b[1A~", 5) == 4);
	CHECK(escape_sequence_length("\x1b[5~", 4) == 4);
	CHECK(escape_sequence_length("\x1b[5~~", 5) == 4);
	CHECK(escape_sequence_length("\x1b[11~", 5) == 5);
	CHECK(escape_sequence_length("\x1b[11~a", 6) == 5);
	CHECK(escape_sequence_length("\x1b[?8003h", 8) == 8);
	CHECK(escape_sequence_length("\x1b[?8003h~", 9) == 8);

	// Incomplete escape sequence
	CHECK(escape_sequence_length("\x1b[11", 4) == -1);
	CHECK(escape_sequence_length("\x1b[11~", 4) == -1);
	CHECK(escape_sequence_length("\x1b[?8003h", 3) == -1);
	CHECK(escape_sequence_length("\x1b[?8003h", 4) == -1);

	// Escape sequence with two parameters
	CHECK(escape_sequence_length("\x1b[7;2C", 6) == 6);
	CHECK(escape_sequence_length("\x1b[7;2C~", 7) == 6);
	CHECK(escape_sequence_length("\x1b[11;11$", 8) == 8);
	CHECK(escape_sequence_length("\x1b[11;11$$", 9) == 8);

	// Incomplete escape sequence
	CHECK(escape_sequence_length("\x1b[11;", 5) == -1);
	CHECK(escape_sequence_length("\x1b[11;1~", 5) == -1);
	CHECK(escape_sequence_length("\x1b[11;1~", 6) == -1);

	// Escape sequence with three parameters
	CHECK(escape_sequence_length("\x1b[8;80;24t", 10) == 10);
	CHECK(escape_sequence_length("\x1b[8;80;24t~", 11) == 10);

	// Incomplete escape sequence
	CHECK(escape_sequence_length("\x1b[8;80;24t", 9) == -1);
	CHECK(escape_sequence_length("\x1b[8;80;24t", 8) == -1);
	CHECK(escape_sequence_length("\x1b[8;80;24t", 7) == -1);

	// Other escape codes
	CHECK(escape_sequence_length("\x1bOn", 3) == 3);
	CHECK(escape_sequence_length("\x1b[1;9P", 6) == 6);

	// Unknown types of escape codes: don't return zero so we
	// always continue reading
	CHECK(escape_sequence_length("\x1bG5~", 4) > 0);
	CHECK(escape_sequence_length("\x1b[#500h", 7) > 0);
}

TEST_CASE( "term/escape_codes/next_token" ) {
	std::string input;
	std::string expected_remaining_input;
	std::vector<std::string> expected_tokens;

	SECTION("empty input") {
	}

	SECTION("just characters") {
		input = "abc";
		expected_tokens.push_back("abc");
	}

	SECTION("control character") {
		input = "abc\ndef";
		expected_tokens.push_back("abc");
		expected_tokens.push_back("\n");
		expected_tokens.push_back("def");
	}

	SECTION("control characters") {
		input = "\n\nabcdef\r\b";
		expected_tokens.push_back("\n");
		expected_tokens.push_back("\n");
		expected_tokens.push_back("abcdef");
		expected_tokens.push_back("\r");
		expected_tokens.push_back("\b");
	}

	SECTION("Larger than a single token") {
		input = "1234567890123456789012345678901234567890";
		expected_tokens.push_back("12345678901234567890123456789012");
		expected_tokens.push_back("34567890");
	}

	SECTION("UTF-8 characters") {
		input = "I own \xe2\x82\xac 50\n";
		expected_tokens.push_back("I own \xe2\x82\xac 50");
		expected_tokens.push_back("\n");
	}

	SECTION("UTF-8 right on the edge of a token") {
		input = "1234567890123456789012345678901\xe2\x82\xac hello";
		expected_tokens.push_back("1234567890123456789012345678901");
		expected_tokens.push_back("\xe2\x82\xac hello");
	}

	SECTION("Incomplete UTF-8 characters") {
		input = "I own \xe2\x82";
		expected_tokens.push_back("I own ");
		expected_remaining_input = "\xe2\x82";
	}

	SECTION("Invalid characters") {
		// Invalid UTF-8 characters should be passed one-by-one, so
		// that even invalid input always progresses through the
		// tokenizer
		input = "\x85\x85\x85 foobar";
		expected_tokens.push_back("\x85");
		expected_tokens.push_back("\x85");
		expected_tokens.push_back("\x85");
		expected_tokens.push_back(" foobar");
	}

	SECTION("Short escape sequences") {
		input = "Left key is \x1bOD or \x1b[D.\nF3 is \x1bOR.";
		expected_tokens.push_back("Left key is ");
		expected_tokens.push_back("\x1bOD");
		expected_tokens.push_back(" or ");
		expected_tokens.push_back("\x1b[D");
		expected_tokens.push_back(".");
		expected_tokens.push_back("\n");
		expected_tokens.push_back("F3 is ");
		expected_tokens.push_back("\x1bOR");
		expected_tokens.push_back(".");
	}

	SECTION("Interrupted short escape sequences") {
		input = "Left key is \x1bO";
		expected_tokens.push_back("Left key is ");
		expected_remaining_input = "\x1bO";
	}

	SECTION("Escape sequence right on the edge of a token") {
		input = "1234567890123456789012345678901\x1bOD hello";
		expected_tokens.push_back("1234567890123456789012345678901");
		expected_tokens.push_back("\x1bOD");
		expected_tokens.push_back(" hello");
	}

	CAPTURE(input);

	size_t inputsz = input.size();
	char c_input[inputsz];
	memcpy(c_input, input.c_str(), inputsz);

	char token[32];

	std::vector<std::string> tokens;
	size_t tokensz = sizeof(token);
	size_t sum_tokens = 0;
	while(cloudos::next_token(c_input, &inputsz, token, &tokensz)) {
		tokens.push_back(std::string(token, tokensz));
		sum_tokens += tokensz;
		CHECK(sum_tokens + inputsz == input.size());
		tokensz = sizeof(token);
	}

	CHECK(std::string(c_input, inputsz) == expected_remaining_input);
	REQUIRE(tokens.size() == expected_tokens.size());
	for(size_t i = 0; i < tokens.size(); ++i) {
		CHECK(tokens[i] == expected_tokens[i]);
	}
}

TEST_CASE( "term/escape_codes/next_token/bad" ) {
	std::string input;

	SECTION("UTF-8 token does not fit") {
		input = "\xe2\x82\xac";
	}

	SECTION("Escape sequence does not fit") {
		input = "\x1bOD";
	}

	size_t inputsz = input.size();
	char c_input[inputsz];
	memcpy(c_input, input.c_str(), inputsz);

	char token;
	size_t tokensz = 1;

	CHECK(cloudos::next_token(c_input, &inputsz, &token, &tokensz) == false);
	CHECK(inputsz == input.size());
	CHECK(std::string(c_input, input.size()) == input);
	CHECK(tokensz == 1);
}
