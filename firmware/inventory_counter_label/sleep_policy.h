#pragma once
#include "qr_geometry.h"
#include "power_policy.h"

namespace inventory_counter {
constexpr uint32_t kAwakeWindowMs = 120000;
constexpr uint32_t kWakeReleaseMs = 50;

struct AwakeWindow {
  bool armed = false;
  uint32_t started = 0;
  void begin(uint32_t now) { started = now; armed = true; }
  uint32_t left(uint32_t now) const {
    return armed ? power_policy::remaining(now, started, kAwakeWindowMs) : 0;
  }
  bool expired(uint32_t now) const { return armed && !left(now); }
};

struct WakeRelease {
  bool released = false;
  uint32_t started = 0;
  bool ready(uint32_t now, bool held) {
    if (held) { released = false; return false; }
    if (!released) { released = true; started = now; }
    return now - started >= kWakeReleaseMs;
  }
};

// Only a temporary buffer is changed. The canonical image/hash stay intact.
inline void hideQr(const uint8_t *normal, uint8_t *hidden, const QrBounds &qr) {
  memcpy(hidden, normal, kFrameBytes);
  for (unsigned x = qr.x; x < unsigned(qr.x) + qr.size; ++x)
    for (unsigned y = qr.y; y < unsigned(qr.y) + qr.size; ++y)
      hidden[16 * (kWidth - 1 - x) + y / 8] &= uint8_t(~(0x80 >> (y % 8)));
}
}  // namespace inventory_counter
