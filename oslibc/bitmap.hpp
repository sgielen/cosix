#pragma once

#include <stdint.h>
#include <stddef.h>

namespace cloudos {

/**
 * The Bitmap struct can be used to efficiently store one-bit information on a
 * large number of objects. For example, it can be used to efficiently store
 * which parking spots in a garage are taken, or which pages are used in
 * memory.
 */
struct Bitmap {
  typedef size_t offset_t;

  /* The Bitmap does not take ownership of the given buffer. It should be
   * zero-filled.
   */
  void reset(size_t nbits, uint8_t *buffer);

  bool get(offset_t);
  void set(offset_t);
  void unset(offset_t);

  inline bool get_free(offset_t &off) {
    return get_contiguous_free(1, off);
  }

  bool get_contiguous_free(size_t num, offset_t &off);

private:
  size_t nbits;
  uint8_t *buffer;
};

}
