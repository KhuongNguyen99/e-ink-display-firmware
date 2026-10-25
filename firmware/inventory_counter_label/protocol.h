#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace inventory_counter {
constexpr size_t kFrameBytes = 4000;
constexpr unsigned kWidth = 250, kHeight = 122;
constexpr uint32_t kMaxRevision = 2147483647;

inline bool decimal(const char *text, uint32_t maximum, uint32_t &value) {
  if (!text || !*text) return false;
  uint32_t result = 0;
  for (; *text; ++text) {
    if (*text < '0' || *text > '9') return false;
    const uint32_t digit = *text - '0';
    if (digit > maximum || result > (maximum - digit) / 10) return false;
    result = result * 10 + digit;
  }
  value = result;
  return true;
}

inline bool hex(const char *text, size_t length) {
  if (!text || strlen(text) != length) return false;
  for (size_t i = 0; i < length; ++i)
    if (!((text[i] >= '0' && text[i] <= '9') ||
          (text[i] >= 'a' && text[i] <= 'f'))) return false;
  return true;
}

inline bool uuid(const char *text) {
  if (!text || strlen(text) != 36) return false;
  for (size_t i = 0; i < 36; ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (text[i] != '-') return false;
    } else if (!((text[i] >= '0' && text[i] <= '9') ||
                 (text[i] >= 'a' && text[i] <= 'f'))) return false;
  }
  return true;
}

inline bool constantEqual(const char *a, const char *b) {
  const size_t length = strlen(a);
  if (length != strlen(b)) return false;
  volatile uint8_t difference = 0;
  for (size_t i = 0; i < length; ++i) difference |= uint8_t(a[i] ^ b[i]);
  return difference == 0;
}

// Incremental writes never touch the destination on overflow.
inline bool append(uint8_t *destination, size_t capacity, size_t &used,
                   const uint8_t *chunk, size_t length) {
  if (used > capacity || length > capacity - used) return false;
  memcpy(destination + used, chunk, length);
  used += length;
  return true;
}

inline void blackPixel(uint8_t *frame, unsigned x, unsigned y) {
  if (x < kWidth && y < kHeight)
    frame[16 * (249 - x) + y / 8] |= uint8_t(0x80 >> (y % 8));
}

inline bool paddingValid(const uint8_t *frame) {
  for (size_t column = 0; column < 250; ++column)
    if (frame[16 * column + 15] & 0x3f) return false;
  return true;
}

enum class RevisionDecision { Apply, Duplicate, Conflict };
inline RevisionDecision revisionDecision(bool hasFrame, uint32_t current,
                                         const char *currentHash, uint32_t next,
                                         const char *nextHash) {
  if (!next || next > kMaxRevision) return RevisionDecision::Conflict;
  if (!hasFrame || next > current) return RevisionDecision::Apply;
  if (next == current && constantEqual(currentHash, nextHash)) return RevisionDecision::Duplicate;
  return RevisionDecision::Conflict;
}
}  // namespace inventory_counter
