#include "iovec.hpp"
#include <memory/allocation.hpp>
#include <oslibc/utility.hpp>
#include "string.h"

using namespace cloudos;

size_t
cloudos::veccpy(const cloudabi_iovec_t *dst, size_t dstlen, Blk src, int flags)
{
	return veccpy(dst, dstlen, reinterpret_cast<char*>(src.ptr), src.size, flags);
}

size_t
cloudos::veccpy(const cloudabi_iovec_t *dst, size_t dstlen, const char *src, size_t srclen, int flags)
{
	size_t copied = 0;
	for(size_t i = 0; i < dstlen && srclen > copied; ++i) {
		size_t copy = min(dst[i].buf_len, srclen - copied);
		memcpy(dst[i].buf, src + copied, copy);
		copied += copy;
	}

	if(flags == VECCPY_RETURN_TRUNCATED_BYTES) {
		return srclen - copied;
	} else {
		return copied;
	}
}

size_t
cloudos::veccpy(Blk dst, const cloudabi_ciovec_t *src, size_t srclen, int flags)
{
	return veccpy(reinterpret_cast<char*>(dst.ptr), dst.size, src, srclen, flags);
}

size_t
cloudos::veccpy(char *dst, size_t dstlen, const cloudabi_ciovec_t *src, size_t srclen, int flags)
{
	size_t src_size = 0;
	size_t copied = 0;
	size_t i;
	for(i = 0; i < srclen && dstlen > copied; ++i) {
		src_size += src[i].buf_len;
		size_t copy = min(src[i].buf_len, dstlen - copied);
		memcpy(dst + copied, src[i].buf, copy);
		copied += copy;
	}

	if(flags == VECCPY_RETURN_TRUNCATED_BYTES) {
		for(; i < srclen; ++i) {
			src_size += src[i].buf_len;
		}
		return src_size - copied;
	} else {
		return copied;
	}
}
