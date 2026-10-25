#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace countwell {
constexpr char kDefaultBleName[] = "E-ink display";
constexpr size_t kMaxBleNameBytes = 248;
constexpr size_t kScanNameBytes = 29;

// Reject overlong encodings, surrogates, noncharacters outside Unicode's
// scalar range, controls, and malformed continuation bytes.
inline bool utf8Prefix(const char *value, size_t length, size_t &boundary) {
  boundary = 0;
  if (!value) return false;
  for (size_t i = 0; i < length;) {
    const uint8_t a = uint8_t(value[i]);
    size_t width = 1;
    if (a < 0x20 || a == 0x7f) return false;
    if (a >= 0xc2 && a <= 0xdf) width = 2;
    else if (a >= 0xe0 && a <= 0xef) width = 3;
    else if (a >= 0xf0 && a <= 0xf4) width = 4;
    else if (a >= 0x80) return false;
    if (i + width > length) return false;
    for (size_t j = 1; j < width; ++j)
      if ((uint8_t(value[i + j]) & 0xc0) != 0x80) return false;
    if (width == 3 && ((a == 0xe0 && uint8_t(value[i + 1]) < 0xa0) ||
                       (a == 0xed && uint8_t(value[i + 1]) >= 0xa0))) return false;
    if (width == 4 && ((a == 0xf0 && uint8_t(value[i + 1]) < 0x90) ||
                       (a == 0xf4 && uint8_t(value[i + 1]) >= 0x90))) return false;
    i += width;
    if (i <= kScanNameBytes) boundary = i;
  }
  return true;
}
inline bool validBleName(const char *value) {
  if (!value) return false;
  const size_t length = strnlen(value, kMaxBleNameBytes + 1);
  size_t boundary = 0;
  return length > 6 && length <= kMaxBleNameBytes &&
         !memcmp(value, "E-ink ", 6) && utf8Prefix(value, length, boundary);
}
inline size_t scanResponseName(const char *value, uint8_t *data) {
  const size_t length = strlen(value);
  size_t boundary = 0;
  if (!utf8Prefix(value, length, boundary)) return 0;
  const size_t used = length <= kScanNameBytes ? length : boundary;
  data[0] = uint8_t(used + 1);
  data[1] = length == used ? 0x09 : 0x08;
  memcpy(data + 2, value, used);
  return used + 2;
}
}  // namespace countwell
