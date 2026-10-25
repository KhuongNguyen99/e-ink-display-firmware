#pragma once
#include "protocol.h"

namespace countwell {
constexpr size_t kControlBytes = 1024;
constexpr size_t kBlePacketBytes = 20;

// A malformed logical command is discarded through its newline so the next
// command cannot accidentally complete a previous partial JSON document.
struct ControlMessage {
  char bytes[kControlBytes + 1] = {};
  size_t used = 0;
  bool discarding = false;
  enum class Result { More, Complete, Invalid };

  void reset() { used = 0; discarding = false; bytes[0] = 0; }
  Result push(const uint8_t *packet, size_t length) {
    if (!length || length > kBlePacketBytes) { discarding = true; return Result::Invalid; }
    Result result = Result::More;
    for (size_t i = 0; i < length; ++i) {
      const uint8_t ch = packet[i];
      if (ch == '\n') {
        const bool invalid = discarding || !used || i + 1 != length;
        bytes[used] = 0;
        discarding = false;
        result = invalid ? Result::Invalid : Result::Complete;
        if (invalid) used = 0;
        return result;
      }
      if (discarding) continue;
      if (!ch || used == kControlBytes) { discarding = true; result = Result::Invalid; continue; }
      bytes[used++] = char(ch);
    }
    return result;
  }
};

inline bool appendBleFrame(uint8_t *destination, size_t &used,
                           const uint8_t *packet, size_t length) {
  if (length < 3 || length > kBlePacketBytes) return false;
  const size_t offset = size_t(packet[0]) | (size_t(packet[1]) << 8);
  if (offset != used) return false;
  return append(destination, kFrameBytes, used, packet + 2, length - 2);
}
}  // namespace countwell
