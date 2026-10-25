#pragma once
#include "ble_protocol.h"

namespace inventory_counter {
// All commands have scalar fields. Reject nesting before cJSON's recursive
// parser sees radio/USB input; a bounded byte count alone does not bound stack.
inline bool flatCommandJson(const char *text) {
  if (!text) return false;
  const auto whitespace = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; };
  while (whitespace(*text)) ++text;
  if (*text++ != '{') return false;
  bool quoted = false, escaped = false, closed = false;
  for (; *text; ++text) {
    const char ch = *text;
    if (quoted) {
      if (uint8_t(ch) < 0x20) return false;
      if (escaped) escaped = false;
      else if (ch == '\\') escaped = true;
      else if (ch == '"') quoted = false;
    } else if (closed) {
      if (!whitespace(ch)) return false;
    } else if (ch == '"') quoted = true;
    else if (ch == '{' || ch == '[' || ch == ']') return false;
    else if (ch == '}') closed = true;
  }
  return closed && !quoted;
}

inline bool validStaticAddress(const uint8_t *address) {
  if ((address[0] & 0xc0) != 0xc0) return false;
  bool allZero = (address[0] & 0x3f) == 0;
  bool allOne = (address[0] & 0x3f) == 0x3f;
  for (size_t i = 1; i < 6; ++i) {
    allZero = allZero && address[i] == 0;
    allOne = allOne && address[i] == 0xff;
  }
  return !allZero && !allOne;
}

// A USB command is a bounded line with a mandatory transport-specific prefix.
// Overflow/NUL discard the entire line, including any apparently valid suffix.
struct UsbCommand {
  char bytes[kControlBytes + 7] = {};
  size_t used = 0;
  bool discarding = false;
  enum class Result { More, Complete, Invalid };
  void reset() { memset(bytes, 0, sizeof(bytes)); used = 0; discarding = false; }
  Result push(uint8_t ch) {
    if (ch == '\r') return Result::More;
    if (ch == '\n') {
      bytes[used] = 0;
      return !discarding && used > 6 && !memcmp(bytes, "CWUSB ", 6) ? Result::Complete : Result::Invalid;
    }
    if (!ch || used == sizeof(bytes) - 1) discarding = true;
    if (!discarding) bytes[used++] = char(ch);
    return Result::More;
  }
};
}  // namespace inventory_counter
