#pragma once
#include "protocol.h"
#include "qr_geometry.h"
#include <stdio.h>

namespace countwell {
constexpr size_t kTranscriptBytes = 384;

inline bool decodeBleKey(const char *text, uint8_t *key) {
  if (!hex(text, 64)) return false;
  for (size_t i = 0; i < 32; ++i) {
    const char a = text[i * 2], b = text[i * 2 + 1];
    key[i] = uint8_t(((a <= '9' ? a - '0' : a - 'a' + 10) << 4) |
                     (b <= '9' ? b - '0' : b - 'a' + 10));
  }
  return true;
}
inline bool pairedTranscript(char *output, size_t capacity, const char *device,
                             const char *generation, const char *box) {
  const int length = snprintf(output, capacity, "CW2|paired|%s|%s|%s", device, generation, box);
  return length > 0 && size_t(length) < capacity;
}
inline bool beginTranscript(char *output, size_t capacity, const char *device,
                            const char *generation, const char *box, const char *profile,
                            uint32_t revision, const char *digest, const char *challenge,
                            const char *nameHash = nullptr, const QrBounds *qr = nullptr) {
  const int length = nameHash
      ? snprintf(output, capacity, "CW2|begin|%s|%s|%s|%s|%lu|%s|%s|%s", device,
                 generation, box, profile, static_cast<unsigned long>(revision), digest, challenge, nameHash)
      : snprintf(output, capacity, "CW2|begin|%s|%s|%s|%s|%lu|%s|%s", device,
                 generation, box, profile, static_cast<unsigned long>(revision), digest, challenge);
  if (length <= 0 || size_t(length) >= capacity) return false;
  if (!qr) return true;
  const int extra = snprintf(output + length, capacity - size_t(length), "|%u|%u|%u",
                             unsigned(qr->x), unsigned(qr->y), unsigned(qr->size));
  return extra > 0 && size_t(extra) < capacity - size_t(length);
}
inline bool committedTranscript(char *output, size_t capacity, const char *device,
                                const char *generation, const char *box, const char *profile,
                                uint32_t revision, const char *digest,
                                const char *nameHash = nullptr) {
  const int length = nameHash
      ? snprintf(output, capacity, "CW2|committed|%s|%s|%s|%s|%lu|%s|%s", device,
                 generation, box, profile, static_cast<unsigned long>(revision), digest, nameHash)
      : snprintf(output, capacity, "CW2|committed|%s|%s|%s|%s|%lu|%s", device,
                 generation, box, profile, static_cast<unsigned long>(revision), digest);
  return length > 0 && size_t(length) < capacity;
}
}  // namespace countwell
