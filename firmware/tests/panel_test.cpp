#include "../inventory_counter_label/panel.h"
#include <assert.h>
#include <vector>
#include <iostream>

struct Transfer { bool command; uint8_t value; };
std::vector<Transfer> transfers;
TestSerial Serial;
int pins[16] = {};
uint32_t elapsed = 0, busyUntil = 0;
bool stuckBusy = false, disconnected = false;
bool waitingForFirstRefresh = true;
unsigned refreshes = 0;
unsigned bits = 0;
uint8_t nextByte = 0, lastCommand = 0;
#ifdef INVENTORY_COUNTER_SSD1680
constexpr int busyLevel = HIGH;
#else
constexpr int busyLevel = LOW;
#endif
void pinMode(uint8_t, int) {}
uint32_t millis() { return elapsed; }
void delay(uint32_t count) { elapsed += count; }
int digitalRead(uint8_t pin) {
  assert(pin == 9);
#ifdef INVENTORY_COUNTER_JD79661
  // A permitted JD startup state: BUSY stays LOW until the initialization/clear command.
  if (waitingForFirstRefresh) return LOW;
#endif
  return stuckBusy || (!disconnected && elapsed < busyUntil) ? busyLevel : 1 - busyLevel;
}
void digitalWrite(uint8_t pin, int value) {
  pins[pin] = value;
  if (pin == 10 && value == LOW) waitingForFirstRefresh = true;
  if (pin == 14 && value == LOW) { bits = 0; nextByte = 0; }
  if (pin == 12 && value == HIGH && pins[14] == LOW) {
    nextByte = uint8_t((nextByte << 1) | pins[11]);
    if (++bits == 8) {
      const bool command = pins[13] == LOW;
      transfers.push_back({command, nextByte});
      if (command) lastCommand = nextByte;
#ifdef INVENTORY_COUNTER_SSD1680
      if (command && nextByte == 0x20) busyUntil = elapsed + 500;
#else
      if (!command && lastCommand == 0x17 && nextByte == 0xa5) {
        waitingForFirstRefresh = false; busyUntil = elapsed + 500; ++refreshes;
      }
#endif
    }
  }
}
int main() {
  uint8_t frame[inventory_counter::kFrameBytes] = {};
  inventory_counter::blackPixel(frame, 0, 0);
  stuckBusy = true;
  uint32_t start = elapsed;
  assert(!panel::render(frame));
  assert(elapsed - start < 16000);
  stuckBusy = false; transfers.clear();
  assert(panel::render(frame));
  assert(pins[7] == HIGH);
#ifdef INVENTORY_COUNTER_SSD1680
  const uint8_t dataRegister = 0x24;
#else
  const uint8_t dataRegister = 0x13;
  assert(refreshes == 3);  // One failed clear, then a complete clear followed by the actual frame.
  unsigned planes = 0;
  for (size_t i = 0; i < transfers.size(); ++i) {
    if (!transfers[i].command || (transfers[i].value != 0x10 && transfers[i].value != 0x13)) continue;
    if (planes++ >= 2) break;
    for (size_t offset = 0; offset < inventory_counter::kFrameBytes; ++offset)
      assert(!transfers.at(i + 1 + offset).command && transfers.at(i + 1 + offset).value == 0xff);
  }
  assert(planes == 3);
  std::vector<uint8_t> lutRegisters;
  for (const Transfer &transfer : transfers)
    if (transfer.command && transfer.value >= 0x20 && transfer.value <= 0x24) lutRegisters.push_back(transfer.value);
  assert(lutRegisters == std::vector<uint8_t>({0x20,0x21,0x24,0x22,0x23, 0x20,0x21,0x24,0x23,0x22}));
#endif
  bool found = false;
  for (size_t i = 0; i < transfers.size(); ++i) {
    if (!transfers[i].command || transfers[i].value != dataRegister) continue;
#ifdef INVENTORY_COUNTER_JD79661
    if (transfers.at(i + 1 + 3984).value == 0xff) continue;  // Initial clearing plane, not the label.
#endif
    found = true;
    for (size_t offset = 0; offset < inventory_counter::kFrameBytes; ++offset) {
      const Transfer &sent = transfers.at(i + 1 + offset);
      assert(!sent.command);
#ifdef INVENTORY_COUNTER_SSD1680
      assert(sent.value == uint8_t(~frame[offset]));
#else
      assert(sent.value == frame[offset]);
#endif
    }
    break;
  }
  assert(found);
  disconnected = true; start = elapsed;
  assert(!panel::render(frame));
  assert(elapsed - start < 2000);
  std::cout << panel::kProfile << " SPI packing, BUSY success, stuck and disconnected tests passed\n";
}
