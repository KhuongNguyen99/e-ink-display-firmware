#pragma once
#include <Arduino.h>
#include "protocol.h"

#if defined(COUNTWELL_SSD1680) == defined(COUNTWELL_JD79661)
#error "Select exactly one panel: COUNTWELL_SSD1680 or COUNTWELL_JD79661"
#endif

namespace panel {
#ifdef COUNTWELL_SSD1680
constexpr char kProfile[] = "ssd1680";
#else
constexpr char kProfile[] = "jd79661";
#endif
constexpr uint8_t kMenuPin = 2, kBackPin = 1;
bool render(const uint8_t *frame);
void text(uint8_t *frame, unsigned x, unsigned y, const char *value, unsigned scale = 1);
}
