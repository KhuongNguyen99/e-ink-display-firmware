#include "panel.h"
#include <initializer_list>

namespace panel {
// Register sequences and full-refresh LUT values: Elecrow reference, see NOTICE.md.
constexpr uint8_t kPower = 7, kClock = 12, kMosi = 11, kReset = 10;
constexpr uint8_t kDc = 13, kCs = 14, kBusy = 9;
static bool initialized = false;
#ifdef COUNTWELL_JD79661
static bool alternateLut = false;
#endif
#ifdef COUNTWELL_SSD1680
constexpr int kBusyLevel = HIGH;
#else
constexpr int kBusyLevel = LOW;
#endif

static void byte(uint8_t value) {
  digitalWrite(kCs, LOW);
  for (unsigned bit = 0; bit < 8; ++bit) {
    digitalWrite(kClock, LOW);
    digitalWrite(kMosi, (value & 0x80) ? HIGH : LOW);
    digitalWrite(kClock, HIGH);
    value <<= 1;
  }
  digitalWrite(kCs, HIGH);
}
static void command(uint8_t value) { digitalWrite(kDc, LOW); byte(value); digitalWrite(kDc, HIGH); }
static void data(uint8_t value) { byte(value); }
static void reg(uint8_t commandByte, std::initializer_list<uint8_t> values) {
  command(commandByte);
  for (uint8_t value : values) data(value);
}
static void trace(const char *phase) {
  Serial.printf("Display phase=%s BUSY=%d at=%lu\n", phase, digitalRead(kBusy), static_cast<unsigned long>(millis()));
}
static bool ready(const char *phase) {
  trace(phase);
  const uint32_t start = millis();
  delay(10);
  while (digitalRead(kBusy) == kBusyLevel) {
    if (millis() - start >= 15000) { trace("timeout-waiting-idle"); initialized = false; return false; }
    delay(1);
  }
  return true;
}
static bool refreshComplete(const char *phase) {
  trace(phase);
  const uint32_t start = millis();
  while (digitalRead(kBusy) != kBusyLevel) {
    if (millis() - start >= 1000) { trace("timeout-waiting-busy-assertion"); initialized = false; return false; }
    delay(1);
  }
  if (!ready("refresh-waiting-idle")) return false;
  trace("refresh-complete");
  return true;
}
#ifdef COUNTWELL_JD79661
static void loadFullRefreshLut() {
  const uint8_t waveform[] = {0x00, 0x60, 0x20, 0x10, 0x90};
  for (uint8_t index : {0, 1, 4, 2, 3}) {
    uint8_t destination = 0x20 + index;
    if (alternateLut && (index == 2 || index == 3)) destination = 0x25 - index;
    command(destination);
    const uint8_t prefix[] = {0x01, waveform[index], 0x14, 0x14, 0x01, 0x00, 0x00, 0x01};
    for (uint8_t value : prefix) data(value);
    for (unsigned i = 8; i < 56; ++i) data(0);
  }
  alternateLut = !alternateLut;
}
#endif
static bool init() {
  pinMode(kPower, OUTPUT); digitalWrite(kPower, HIGH);
  for (uint8_t pin : {kClock, kMosi, kReset, kDc, kCs}) pinMode(pin, OUTPUT);
  pinMode(kBusy, INPUT);
  digitalWrite(kCs, HIGH);
  delay(100); digitalWrite(kReset, HIGH); delay(10); digitalWrite(kReset, LOW);
#ifdef COUNTWELL_SSD1680
  delay(10); digitalWrite(kReset, HIGH); delay(10);
  if (!ready("ssd-hardware-reset")) return false;
  command(0x12);
  if (!ready("ssd-software-reset")) return false;
  reg(0x01, {0xf9, 0x00, 0x00}); reg(0x11, {0x03});
  reg(0x44, {0x00, 0x0f}); reg(0x45, {0x00, 0x00, 0xf9, 0x00});
  reg(0x3c, {0x01}); reg(0x18, {0x80}); reg(0x4e, {0x00}); reg(0x4f, {0x00, 0x00});
  initialized = ready("ssd-initialized");
#else
  delay(100); digitalWrite(kReset, HIGH); delay(100);
  // The vendor initializes JD registers and clears both RAM planes before its first BUSY wait.
  trace("jd-init-registers");
  reg(0x00, {0xf7, 0x8a}); reg(0x01, {0x03, 0x00, 0x3f, 0x3f, 0x03});
  reg(0x03, {0x00}); reg(0x06, {0x27, 0x27, 0x2f}); reg(0x30, {0x0d});
  reg(0x60, {0x22}); reg(0x82, {0x07}); reg(0xe3, {0x88}); reg(0x41, {0x00});
  reg(0x61, {0x80, 0x00, 0xfa}); reg(0x65, {0x00, 0x00, 0x00}); reg(0x50, {0xb7});
  command(0x10);
  for (size_t i = 0; i < countwell::kFrameBytes; ++i) data(0xff);
  command(0x13);
  for (size_t i = 0; i < countwell::kFrameBytes; ++i) data(0xff);
  alternateLut = false;
  loadFullRefreshLut();
  reg(0x17, {0xa5});
  initialized = refreshComplete("jd-initial-clear");
#endif
  return initialized;
}

bool render(const uint8_t *frame) {
  if (!initialized && !init()) return false;
#ifdef COUNTWELL_SSD1680
  if (!ready("ssd-before-frame")) return false;
  reg(0x4e, {0x00}); reg(0x4f, {0x00, 0x00}); reg(0x3c, {0x01}); command(0x24);
  for (size_t i = 0; i < countwell::kFrameBytes; ++i) data(uint8_t(~frame[i]));
  // SSD1680 update sequence F7 finishes by disabling analog power and the oscillator.
  reg(0x22, {0xf7}); command(0x20);
#else
  reg(0x50, {0xd7}); command(0x13);
  for (size_t i = 0; i < countwell::kFrameBytes; ++i) data(frame[i]);
  loadFullRefreshLut();
  reg(0x17, {0xa5});
#endif
  if (!refreshComplete("frame-refresh")) return false;
#ifdef COUNTWELL_SSD1680
  // Deep sleep mode 1 retains RAM and the visible e-paper image. BUSY stays high
  // in this state, so the next render must hardware-reset before waiting on BUSY.
  reg(0x10, {0x01});
  initialized = false;
#endif
  return true;
}

// Small original 5x7 font for setup messages; label typography is rendered by Countwell.
static const uint8_t glyphs[][5] = {
  {0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},{0x62,0x51,0x49,0x49,0x46},
  {0x22,0x41,0x49,0x49,0x36},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
  {0x3e,0x49,0x49,0x49,0x32},{1,0x71,9,5,3},{0x36,0x49,0x49,0x49,0x36},{0x26,0x49,0x49,0x49,0x3e},
  {0x7e,9,9,9,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
  {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,9,9,9,1},
  {0x3e,0x41,0x49,0x49,0x3a},{0x7f,8,8,8,0x7f},{0,0x41,0x7f,0x41,0},
  {0x20,0x40,0x41,0x3f,1},{0x7f,8,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
  {0x7f,2,0x0c,2,0x7f},{0x7f,4,8,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
  {0x7f,9,9,9,6},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,9,0x19,0x29,0x46},
  {0x26,0x49,0x49,0x49,0x32},{1,1,0x7f,1,1},{0x3f,0x40,0x40,0x40,0x3f},
  {0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,8,0x14,0x63},
  {7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43},
  {0,0x60,0x60,0,0},{8,8,8,8,8},{0,0x36,0x36,0,0},{0x20,0x10,8,4,2}
};
void text(uint8_t *frame, unsigned x, unsigned y, const char *value, unsigned scale) {
  for (; *value && x + 5 * scale <= countwell::kWidth; ++value, x += 6 * scale) {
    char ch = *value;
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    int index = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'A' && ch <= 'Z' ? ch - 'A' + 10 :
                ch == '.' ? 36 : ch == '-' ? 37 : ch == ':' ? 38 : ch == '/' ? 39 : -1;
    if (index < 0) continue;
    for (unsigned col = 0; col < 5; ++col)
      for (unsigned row = 0; row < 7; ++row)
        if (glyphs[index][col] & (1 << row))
          for (unsigned dx = 0; dx < scale; ++dx)
            for (unsigned dy = 0; dy < scale; ++dy)
              countwell::blackPixel(frame, x + col * scale + dx, y + row * scale + dy);
  }
}
}
