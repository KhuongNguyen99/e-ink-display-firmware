#include "../inventory_counter_label/power_policy.h"

#include <cassert>
#include <cstdint>

int main() {
  using namespace power_policy;
  static_assert(kFastWindowMs == 5000 && kPollMs == 25 && kIdleWaitMs == 1000,
      "the fast window, active poll, and idle wait are deliberate power budgets");
  static_assert(kFastAdvertisingUnits == 0xa0 && kFastAdvertisingUnits * 625 == 100000,
      "fast advertising interval must be 100 ms");
  static_assert(kSlowAdvertisingUnits == 0x640 && kSlowAdvertisingUnits * 625 == 1000000,
      "idle advertising interval must be 1000 ms");

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

  state = WaitState{};
  state.idleSleepReady = true;
  assert(loopWaitMs(100, state) == kIdleWaitMs);
  state.connected = true; assert(loopWaitMs(100, state) == kPollMs); state.connected = false;
  state.pairingOpen = true; assert(loopWaitMs(100, state) == kPollMs); state.pairingOpen = false;
  state.receiving = true; assert(loopWaitMs(100, state) == kPollMs); state.receiving = false;
  state.busy = true; assert(loopWaitMs(100, state) == kPollMs); state.busy = false;
  state.menuHeld = true; state.menuLatched = true;
  assert(loopWaitMs(100, state) == kPollMs); state.menuHeld = false;

  state.fastWindowLeft = 997;
  assert(loopWaitMs(100, state) == 997);
  state.fastWindowLeft = 0;
  state.refreshLeft = 733;
  assert(loopWaitMs(100, state) == 733);
  state.advertisingRetryLeft = 411;
  assert(loopWaitMs(100, state) == 411);
  state.refreshLeft = 50;
  assert(loopWaitMs(100, state) == 50);
  state.refreshLeft = 0;
  assert(loopWaitMs(100, state) == 1);

  state = WaitState{}; state.idleSleepReady = true;
  state.refreshLeft = remaining(8, UINT32_MAX - 10u, 1000);
  assert(state.refreshLeft == 981);
  assert(loopWaitMs(8, state) == 981);
  state.refreshLeft = UINT32_MAX;
  state.advertisingRetryLeft = remaining(8, UINT32_MAX - 10u, 1000);
  assert(loopWaitMs(8, state) == 981);
}
