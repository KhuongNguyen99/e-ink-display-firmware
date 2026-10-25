#pragma once
#include "protocol.h"

namespace countwell {
struct QrBounds {
  uint16_t x, y, size;
};

inline bool validQrBounds(uint32_t x, uint32_t y, uint32_t size) {
  return size > 0 && x < kWidth && y < kHeight &&
      size <= kWidth - x && size <= kHeight - y;
}

inline bool sameQrBounds(const QrBounds &a, const QrBounds &b) {
  return a.x == b.x && a.y == b.y && a.size == b.size;
}

}  // namespace countwell
