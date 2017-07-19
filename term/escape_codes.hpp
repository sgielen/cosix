#pragma once

#include <stdint.h>
#include <stddef.h>

namespace cloudos {

/**
 * Retrieves the next token from the input. A token can be a single escape or
 * control sequence, a control character, or a set of UTF-8 characters. These
 * can be checked by calling is_control_character and is_escape_code on the
 * resulting tokens. If both functions return false, the token is a normal
 * set of UTF-8 characters.
 *
 * This function assumes input is in valid UTF-8 format. UTF-8 multi-byte
 * characters are inserted together with 'ASCII' non-control bytes in the same
 * token.
 *
 * While escape codes are not interpreted by this function, the length of an
 * escape code requires knowing the format. Therefore, if the format is
 * unknown, the escape code can not be correctly read. In this case, the
 * function will return only the preamble of the escape code separately, and it
 * may happen that the rest of the escape code is still in the input string.
 * This ensures that concatenating the tokens again will not lead to incorrect
 * behaviour in receiving terminals -- but if interpretation inside the Cosix
 * kernel is important (e.g. for the VGA terminal) then this function needs to
 * know about the escape code format to return it correctly.
 *
 * Initially, *inputsz is set to the size of the input and *tokensz is set to
 * the size of the next_token buffer. Here, *tokensz must be at least 32 to
 * ensure a full escape code can fit. If a full escape code can be returned, or
 * the input string starts with normal characters, this function returns true,
 * removes the characters from the input buffer, sets *inputsz to the new size
 * of the input, sets next_token to the escape code or the character sequence
 * and sets *tokensz to that size. If input is empty, contains only an
 * incomplete escape code, or contains an escape code that does not fit in
 * *next_token, this function returns false.
 */
bool next_token(char *input, size_t *inputsz, char *next_token, size_t *tokensz);

bool is_control_character(char const *token, size_t sz);
bool is_escape_sequence(char const *token, size_t sz);

/**
 * Returns the length of the UTF-8 sequence at the beginning of token. Returns
 * 1 for a normal ASCII character (including control characters). Returns 0 if
 * the token does not start with a UTF-8 sequence. Returns -1 if it starts with
 * a UTF-8 sequence that is invalid, returns -2 if the token ends before the
 * sequence can be completed. In the last case, it is possible that the
 * function will return a positive value if more data becomes available.
 */
int unicode_character_length(char const *token, size_t sz);

/**
 * Returns the length of an escape sequence at the beginning of token. Because
 * escape sequences are of variable length, it is necessary to know all types
 * of escape sequences in order to return the correct value. For this reason,
 * if an escape sequence is not recognized, this function always returns a
 * positive value. It returns 0 if the token does not start with an escape
 * sequence, and -1 if token starts with an escape sequence that is known
 * to be incomplete.
 *
 * This function is greedy: in case of ambiguity, it will return the longest
 * escape sequence out of all possibilities.
 */
int escape_sequence_length(char const *token, size_t sz);

/**
 * Special escape codes used by Cosix:
 * - CSI 18 t
 *   Make terminal emulator write back the size of the terminal in characters
 *   as CSI 8 ; <height> ; <width> t
 * - CSI ? 8000 h
 *   Written by the terminal whenever the output size changes.
 * - CSI ? 8001 h
 *   Turn off echoing of input. In many operating systems this is a task of the
 *   TTY layer; in Cosix, echoing is done by the terminal emulator.
 * - CSI ? 8002 h
 *   Turn on echoing of input.
 * - CSI ? 8003 h
 *   Turn off LF -> CRLF conversion.
 * - CSI ? 8004 h
 *   Turn on LF -> CRLF conversion.
 * Many more escape codes can be found here:
 * http://invisible-island.net/xterm/ctlseqs/ctlseqs.html
 */
bool is_escape_code_get_terminal_size(const char *token, size_t sz);
bool is_escape_code_terminal_size(const char *token, size_t sz);
bool is_escape_code_terminal_size_changed(const char *token, size_t sz);
bool is_escape_code_echoing_off(const char *token, size_t sz);
bool is_escape_code_echoing_on(const char *token, size_t sz);
bool is_escape_code_crlf_off(const char *token, size_t sz);
bool is_escape_code_crlf_on(const char *token, size_t sz);

}
