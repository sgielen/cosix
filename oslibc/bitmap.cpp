#include "oslibc/bitmap.hpp"
#include "oslibc/assert.hpp"
#include "global.hpp"

using namespace cloudos;

void Bitmap::reset(size_t n, uint8_t *b) {
	nbits = n;
	buffer = b;
}

bool Bitmap::get(Bitmap::offset_t off) {
	assert(off < nbits);
	uint8_t b = buffer[off / 8];
	uint8_t o = off % 8;
	return b & (1 << o);
}

void Bitmap::set(Bitmap::offset_t off) {
	assert(off < nbits);
	uint8_t &b = buffer[off / 8];
	uint8_t o = off % 8;
	b |= (1 << o);
}

void Bitmap::unset(Bitmap::offset_t off) {
	assert(off < nbits);
	uint8_t &b = buffer[off / 8];
	uint8_t o = off % 8;
	b &= ~(1 << o);
}

bool Bitmap::get_contiguous_free(size_t num, offset_t &off) {
	// TODO: make a more efficient algorithm
	size_t count = 0;
	for(off = 0; off < nbits; ++off) {
		if(off % 8 == 0 && buffer[off / 8] == 0xff) {
			// this whole byte is full, skip it
			count = 0;
			off += 7;
		} else if(!get(off)) {
			if(++count == num) {
				for(count = off - num + 1; count <= off; ++count) {
					set(count);
				}
				off -= num - 1;
				return true;
			}
		} else {
			count = 0;
		}
	}
	return false;
}
