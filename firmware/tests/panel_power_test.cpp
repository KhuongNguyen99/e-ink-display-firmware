#include "../inventory_counter_label/panel.h"
#include <assert.h>
#include <limits.h>
#include <iostream>
#include <vector>

#ifndef INVENTORY_COUNTER_SSD1680
#error "This test exercises the SSD1680 panel profile"
#endif

SerialMock Serial;
enum class Kind { Command, Data, Reset };
struct Event { Kind kind; uint8_t value; uint32_t at; };
std::vector<Event> events;
int pins[16] = {};
uint32_t elapsed = 0, busyUntil = 0;
bool sleeping = false, resetLowSeen = false;
unsigned bits = 0, resetCount = 0, sleepCount = 0;
uint8_t nextByte = 0, lastCommand = 0;
enum class RefreshMode { Success, NeverAssert, NeverFinish };
RefreshMode refreshMode = RefreshMode::Success;

void pinMode(uint8_t, int) {}
uint32_t millis() { return elapsed; }
void delay(uint32_t count) { elapsed += count; }
int digitalRead(uint8_t pin) {
  assert(pin == 9);
  return sleeping || elapsed < busyUntil ? HIGH : LOW;
}
void digitalWrite(uint8_t pin, int value) {
  const int prior = pins[pin];
  pins[pin] = value;
  if (pin == 10) {
    if (value == LOW) resetLowSeen = true;
    if (value == HIGH && prior == LOW && resetLowSeen) {
      resetLowSeen = false;
      sleeping = false;
      busyUntil = elapsed + 2;
      ++resetCount;
      events.push_back({Kind::Reset, 0, elapsed});
    }
  }
  if (pin == 14 && value == LOW) { bits = 0; nextByte = 0; }
  if (pin == 12 && value == HIGH && pins[14] == LOW) {
    nextByte = uint8_t((nextByte << 1) | pins[11]);
    if (++bits != 8) return;
    const bool isCommand = pins[13] == LOW;
    assert(!sleeping);  // Commands after deep sleep require a hardware reset.
    events.push_back({isCommand ? Kind::Command : Kind::Data, nextByte, elapsed});
    if (!isCommand) {
      if (lastCommand == 0x10) {
        assert(nextByte == 0x01);
        sleeping = true;
        ++sleepCount;
      }
      return;
    }
    lastCommand = nextByte;
    if (nextByte == 0x20) {
      busyUntil = refreshMode == RefreshMode::Success ? elapsed + 500 :
                  refreshMode == RefreshMode::NeverFinish ? UINT32_MAX : elapsed;
    } else if (nextByte == 0x10) {
      assert(elapsed >= busyUntil);  // Sleep must follow completed refresh.
    }
  }
}

size_t commandAfter(size_t start, uint8_t value) {
  for (size_t i = start; i < events.size(); ++i)
    if (events[i].kind == Kind::Command && events[i].value == value) return i;
  assert(false && "missing command");
  return events.size();
}
void checkSuccessfulUpdate(size_t start, const uint8_t *frame) {
  const size_t ram = commandAfter(start, 0x24);
  for (size_t i = 0; i < inventory_counter::kFrameBytes; ++i) {
    const Event &sent = events.at(ram + 1 + i);
    assert(sent.kind == Kind::Data && sent.value == uint8_t(~frame[i]));
  }
  const size_t control = commandAfter(ram + 1 + inventory_counter::kFrameBytes, 0x22);
  assert(events.at(control + 1).kind == Kind::Data && events.at(control + 1).value == 0xf7);
  const size_t activate = commandAfter(control + 2, 0x20);
  const size_t sleep = commandAfter(activate + 1, 0x10);
  assert(events.at(sleep + 1).kind == Kind::Data && events.at(sleep + 1).value == 0x01);
  assert(events[activate].at + 500 <= events[sleep].at);
}
void checkFailedUpdate(size_t start, uint8_t expectedControl) {
  const size_t control = commandAfter(start, 0x22);
  assert(events.at(control + 1).kind == Kind::Data &&
         events.at(control + 1).value == expectedControl);
  commandAfter(control + 2, 0x20);
  for (size_t i = control + 2; i < events.size(); ++i)
    assert(events[i].kind != Kind::Command || events[i].value != 0x10);
}

int main() {
  uint8_t first[inventory_counter::kFrameBytes] = {};
  uint8_t second[inventory_counter::kFrameBytes] = {};
  inventory_counter::blackPixel(first, 0, 0);
  inventory_counter::blackPixel(second, 249, 121);

  size_t start = events.size();
  assert(panel::render(first));
  checkSuccessfulUpdate(start, first);
  assert(sleeping && digitalRead(9) == HIGH);
  assert(resetCount == 1 && sleepCount == 1 && pins[7] == HIGH);

  start = events.size();
  assert(panel::render(second));
  assert(events.at(start).kind == Kind::Reset);
  assert(commandAfter(start, 0x12) < commandAfter(start, 0x24));
  checkSuccessfulUpdate(start, second);
  assert(sleeping && resetCount == 2 && sleepCount == 2 && pins[7] == HIGH);

  refreshMode = RefreshMode::NeverAssert;
  start = events.size();
  const uint32_t began = elapsed;
  assert(!panel::render(first));
  assert(elapsed - began >= 1000 && elapsed - began < 2000);
  checkFailedUpdate(start, 0xf7);
  assert(!sleeping && resetCount == 3 && sleepCount == 2);

  refreshMode = RefreshMode::NeverFinish;
  start = events.size();
  const uint32_t beganAgain = elapsed;
  assert(!panel::render(first));
  assert(elapsed - beganAgain >= 15000 && elapsed - beganAgain < 16000);
  checkFailedUpdate(start, 0xf7);
  assert(!sleeping && resetCount == 4 && sleepCount == 2);

  refreshMode = RefreshMode::Success;
  start = events.size();
  assert(panel::render(second));
  checkSuccessfulUpdate(start, second);
  assert(resetCount == 5 && sleepCount == 3 && sleeping);
  std::cout << "SSD1680 refresh, deep sleep, reset, SPI frame, and timeout tests passed\n";
}
