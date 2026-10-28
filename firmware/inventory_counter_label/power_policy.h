#pragma once

#include <stdint.h>

namespace power_policy {

constexpr uint32_t kFastWindowMs = 5000;
constexpr uint32_t kMenuHoldMs = 5000;
constexpr uint32_t kPairWindowMs = 120000;
constexpr uint32_t kTransferTimeoutMs = 60000;
constexpr uint32_t kIdleDisconnectMs = 30000;
constexpr uint32_t kPollMs = 25;
constexpr uint32_t kIdleWaitMs = 1000;
constexpr uint16_t kFastAdvertisingUnits = 0xa0;   // 100 ms, in 0.625 ms units
constexpr uint16_t kSlowAdvertisingUnits = 0x640;  // 1 second

// Subtraction on uint32_t keeps deadlines valid across millis() wraparound.
inline uint32_t remaining(uint32_t now, uint32_t started, uint32_t duration) {
  const uint32_t elapsed = now - started;
  return elapsed >= duration ? 0 : duration - elapsed;
}

struct FastWindow {
  bool armed = false;
  uint32_t started = 0;

  void begin(uint32_t now) { started = now; armed = true; }
  uint32_t left(uint32_t now) const {
    return armed ? remaining(now, started, kFastWindowMs) : 0;
  }
  bool active(uint32_t now) const { return left(now) != 0; }
};

struct WaitState {
  bool idleSleepReady = false;
  uint32_t refreshLeft = UINT32_MAX;
  uint32_t advertisingRetryLeft = UINT32_MAX;
  bool menuHeld = false;
  bool menuLatched = false;
  uint32_t menuStarted = 0;
  bool pairingOpen = false;
  uint32_t pairingStarted = 0;
  bool receiving = false;
  uint32_t transferTouched = 0;
  bool connected = false;
  bool disconnectRequested = false;
  bool busy = false;
  uint32_t connectionTouched = 0;
  bool connectionAuthorized = false;
  uint32_t connectionStarted = 0;
  uint32_t fastWindowLeft = 0;
};

inline uint32_t loopWaitMs(uint32_t now, const WaitState &s) {
  // A verified GPIO/UART wake path permits longer quiescent waits. Keep
  // active work and every wake-setup failure on the responsive polling path.
  const bool idle = s.idleSleepReady && !s.connected && !s.pairingOpen &&
      !s.receiving && !s.busy && !s.menuHeld;
  uint32_t wait = idle ? kIdleWaitMs : kPollMs;
  auto shorten = [&wait](uint32_t left) {
    if (left < wait) wait = left;
  };
  if (s.menuHeld && !s.menuLatched) shorten(remaining(now, s.menuStarted, kMenuHoldMs));
  if (s.pairingOpen) shorten(remaining(now, s.pairingStarted, kPairWindowMs));
  if (s.receiving) shorten(remaining(now, s.transferTouched, kTransferTimeoutMs));
  if (s.connected && !s.disconnectRequested && !s.busy) {
    shorten(remaining(now, s.connectionTouched, kIdleDisconnectMs));
    if (!s.connectionAuthorized)
      shorten(remaining(now, s.connectionStarted, kIdleDisconnectMs));
  }
  if (s.fastWindowLeft) shorten(s.fastWindowLeft);
  shorten(s.refreshLeft);
  shorten(s.advertisingRetryLeft);
  return wait ? wait : 1;
}

}  // namespace power_policy
