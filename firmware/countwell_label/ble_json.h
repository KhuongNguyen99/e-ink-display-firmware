#pragma once
#include <stdint.h>
#include <cJSON.h>

namespace countwell {
// cJSON 1.7.15 prints numbers through sscanf/strtod, which exhausts the
// Bluetooth callback stack. Internal unsigned integers need only decimal
// digits; cJSON copies this token and emits it as a JSON number, not a string.
inline cJSON *addJsonUint32(cJSON *json, const char *name, uint32_t value) {
  char digits[11];
  char *start = digits + sizeof(digits);
  *--start = 0;
  do {
    *--start = char('0' + value % 10);
    value /= 10;
  } while (value);
  return cJSON_AddRawToObject(json, name, start);
}
}  // namespace countwell
