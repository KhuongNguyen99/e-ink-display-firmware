#pragma once
#include <Arduino.h>
#include "power_policy.h"

namespace inventory_counter {
constexpr uint32_t kMenuDebounceMs = 50;

// The ISR and awake checks share this state under menuEdgeMux. Events latch so
// a complete press during a panel refresh or crypto operation cannot be lost.
struct MenuButton {
  bool rawHeld = false, held = false, ignoreRelease = false;
  bool longReported = false, shortPress = false, longPress = false;
  uint32_t rawAt = 0, pressedAt = 0;

  void begin(uint32_t now, bool down) {
    rawHeld = held = ignoreRelease = down;
    longReported = shortPress = longPress = false;
    rawAt = pressedAt = now;
  }

  void sample(uint32_t now, bool down) {
    if (rawHeld != held && uint32_t(now - rawAt) >= kMenuDebounceMs) {
      held = rawHeld;
      if (held) { pressedAt = rawAt; longReported = false; }
      else {
        if (!longReported && uint32_t(rawAt - pressedAt) >= power_policy::kMenuHoldMs) {
          longPress = longReported = true;
        }
        // Pending short presses carry parity: two taps cancel before a refresh.
        if (!ignoreRelease && !longReported) shortPress = !shortPress;
        ignoreRelease = false;
      }
    }
    // A pending release uses its edge time, never the later debounce time.
    if (held && !longReported &&
        uint32_t((rawHeld ? now : rawAt) - pressedAt) >= power_policy::kMenuHoldMs) {
      longPress = longReported = true;
    }
    if (down != rawHeld) { rawHeld = down; rawAt = now; }
  }
};
}  // namespace inventory_counter
