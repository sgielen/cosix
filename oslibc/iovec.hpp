#pragma once

#include <cloudabi_types.h>
#include <stdint.h>
#include <stddef.h>

namespace cloudos {

struct Blk;

#define VECCPY_RETURN_TRUNCATED_BYTES 1

/** veccpy: copy from/to an iovec.
 * If flags is 0, returns amount of bytes copied. If this is lower than the size of the source,
 * the source did not fit in the destination. Cannot be higher than the size of the source.
 * If flags is VECCPY_RETURN_TRUNCATED_BYTES, returns amount of bytes remaining in the source
 * after it was copied into the destination, i.e. 0 if the source fit fully in the destination.
 */

size_t
veccpy(const cloudabi_iovec_t *dst, size_t dstlen, Blk src, int flags);

size_t
veccpy(const cloudabi_iovec_t *dst, size_t dstlen, const char *src, size_t srclen, int flags);

size_t
veccpy(Blk dst, const cloudabi_ciovec_t *src, size_t srclen, int flags);

size_t
veccpy(char *dst, size_t dstlen, const cloudabi_ciovec_t *src, size_t srclen, int flags);

}
