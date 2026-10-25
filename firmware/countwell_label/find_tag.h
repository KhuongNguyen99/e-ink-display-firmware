#pragma once
#include "qr_geometry.h"
#include <stdio.h>

namespace countwell {
constexpr uint32_t kFindDurationMs = 30000;
constexpr uint32_t kFindRestoreRetryMs = 1000;
constexpr unsigned kFindQrHalo = 12;

// The source is always the saved normal frame. Neither its hash nor padding
// changes. Preserve QR pixels, make a clipped 12-pixel white quiet zone around
// the bitmap, and flip all other visible pixels for a readable Find signal.
inline void invertOutsideQr(const uint8_t *normal, uint8_t *temporary, const QrBounds &qr) {
  if (temporary != normal) memcpy(temporary, normal, kFrameBytes);
  for (unsigned x = 0; x < kWidth; ++x) {
    for (unsigned y = 0; y < kHeight; ++y) {
      if (x >= qr.x && x < unsigned(qr.x) + qr.size &&
          y >= qr.y && y < unsigned(qr.y) + qr.size) continue;
      const size_t offset = 16 * (kWidth - 1 - x) + y / 8;
      const uint8_t mask = uint8_t(0x80 >> (y % 8));
      const bool inHalo = x + kFindQrHalo >= qr.x && x < unsigned(qr.x) + qr.size + kFindQrHalo &&
          y + kFindQrHalo >= qr.y && y < unsigned(qr.y) + qr.size + kFindQrHalo;
      if (inHalo) temporary[offset] &= uint8_t(~mask);
      else temporary[offset] ^= mask;
    }
  }
}

inline bool findTranscript(char *output, size_t capacity, const char *device,
                           const char *generation, const char *box, const char *profile,
                           uint32_t revision, const char *digest, const char *challenge,
                           const QrBounds &qr) {
  const int length = snprintf(output, capacity,
      "CW2|find|%s|%s|%s|%s|%lu|%s|%s|30000|%u|%u|%u",
      device, generation, box, profile, static_cast<unsigned long>(revision),
      digest, challenge, unsigned(qr.x), unsigned(qr.y), unsigned(qr.size));
  return length > 0 && size_t(length) < capacity;
}
}  // namespace countwell
