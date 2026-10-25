#include "../inventory_counter_label/power_policy.h"

#include <cassert>
#include <cstdint>

int main() {
  using namespace power_policy;
  static_assert(kFastAdvertisingUnits * 625 == 20000, "fast interval must be 20 ms");
  static_assert(kSlowAdvertisingUnits * 625 == 500000, "idle interval must be 500 ms");

  FastWindow fast;
  assert(!fast.active(0));
  fast.begin(0xfffffff0u);
  assert(fast.active(0xfffffff0u));
  assert(fast.left(0x10u) == kFastWindowMs - 32u);
  assert(fast.active(0xfffffff0u + kFastWindowMs - 1u));
  assert(!fast.active(0xfffffff0u + kFastWindowMs));
  fast.begin(0xfffffff0u + kFastWindowMs);
  assert(fast.active(0xfffffff0u + kFastWindowMs));

  WaitState state;
  assert(loopWaitMs(100, state) == kPollMs);
  state.menuHeld = true;
  state.menuStarted = 0xfffffff0u;
  assert(loopWaitMs(0xfffffff0u + kMenuHoldMs - 3u, state) == 3);
  assert(loopWaitMs(0xfffffff0u + kMenuHoldMs, state) == 1);
  state.menuLatched = true;
  assert(loopWaitMs(0xfffffff0u + kMenuHoldMs, state) == kPollMs);

  state = WaitState{};
  state.pairingOpen = true;
  state.pairingStarted = 0xfffffff0u;
  assert(loopWaitMs(0xfffffff0u + kPairWindowMs - 7u, state) == 7);
  state = WaitState{};
  state.receiving = true;
  state.transferTouched = 0xfffffff0u;
  assert(loopWaitMs(0xfffffff0u + kTransferTimeoutMs - 2u, state) == 2);
  state = WaitState{};
  state.connected = true;
  state.connectionTouched = 100;
  state.connectionStarted = 110;
  state.connectionAuthorized = false;
  assert(loopWaitMs(100 + kIdleDisconnectMs - 4, state) == 4);
  state.connectionAuthorized = true;
  state.connectionTouched = 200;
  assert(loopWaitMs(200 + kIdleDisconnectMs - 9, state) == 9);
  state.disconnectRequested = true;
  assert(loopWaitMs(200 + kIdleDisconnectMs - 9, state) == kPollMs);
  state = WaitState{};
  state.fastWindowLeft = 6;
  assert(loopWaitMs(100, state) == 6);
}
