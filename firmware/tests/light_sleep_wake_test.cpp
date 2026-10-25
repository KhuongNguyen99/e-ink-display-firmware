#include "../countwell_label/light_sleep_wake.h"
#include <cassert>
#include <iostream>

using namespace countwell_power;

static void resetWake() {
  lightSleepWakeState() = LightSleepWakeState{};
  testPinLevels.clear(); testGpioInterruptEnabled.clear(); testGpioWakeEnabled.clear();
  testGpioInterruptType.clear(); testWakeOperations.clear(); testWakeFailOperation.clear();
  testWakeOnMenuEnable = nullptr; testIsrNotifyCalls = testIsrYieldCalls = 0;
  testGpioInterruptType[2] = GPIO_INTR_ANYEDGE; testGpioInterruptEnabled[2] = true;
}

static void beginWake() { assert(beginLightSleepWake(2, 1, xTaskGetCurrentTaskHandle())); }

int main() {
  resetWake(); beginWake();
  assert(testUartWakeThreshold == 3);
  assert(armLightSleepWake());
  assert(lightSleepWakeState().armed);
  assert(testGpioInterruptType[2] == GPIO_INTR_LOW_LEVEL);
  assert(testGpioWakeEnabled[2] && testGpioWakeEnabled[1]);
  assert(!testGpioInterruptEnabled[1]);
  digitalWrite(2, LOW);
  menuLightSleepInterrupt();
  assert(!testGpioInterruptEnabled[2]); // Held button cannot retrigger level IRQ.
  assert(testIsrNotifyCalls == 1 && testIsrYieldCalls == 1);
  digitalWrite(2, HIGH); // Release before the loop resumes is sampled by caller.
  assert(restoreLightSleepWake());
  assert(!lightSleepWakeState().armed && !testGpioWakeEnabled[2] && !testGpioWakeEnabled[1]);
  assert(testGpioInterruptType[2] == GPIO_INTR_ANYEDGE && testGpioInterruptEnabled[2]);
  menuLightSleepInterrupt(); // Normal CHANGE notification must not mask the IRQ.
  assert(testGpioInterruptEnabled[2] && testIsrNotifyCalls == 2);

  for (int pin : {1, 2}) {
    resetWake(); beginWake(); digitalWrite(pin, LOW);
    assert(!armLightSleepWake());
    assert(!lightSleepWakeState().armed && lightSleepWakeState().ready);
    assert(testGpioInterruptType[2] == GPIO_INTR_ANYEDGE);
  }

  resetWake(); beginWake();
  testWakeOnMenuEnable = [] { digitalWrite(2, LOW); menuLightSleepInterrupt(); };
  assert(!armLightSleepWake()); // Press during arming keeps the caller awake.
  assert(testIsrNotifyCalls == 1 && !lightSleepWakeState().armed);
  assert(testGpioInterruptType[2] == GPIO_INTR_ANYEDGE && testGpioInterruptEnabled[2]);

  for (const char *operation : {"uart_threshold", "uart_wake", "gpio_wake"}) {
    resetWake(); testWakeFailOperation = operation;
    assert(!beginLightSleepWake(2, 1, xTaskGetCurrentTaskHandle()));
    assert(!armLightSleepWake() && !lightSleepWakeState().ready);
  }
  for (const char *operation : {"disable:2", "disable:1", "wake:2", "wake:1", "enable:2"}) {
    resetWake(); beginWake(); testWakeFailOperation = operation;
    assert(!armLightSleepWake());
    assert(!lightSleepWakeState().ready && !lightSleepWakeState().armed);
    assert(!testGpioWakeEnabled[2] && !testGpioWakeEnabled[1]);
    assert(testGpioInterruptType[2] == GPIO_INTR_ANYEDGE);
    testWakeFailOperation.clear();
    assert(!armLightSleepWake()); // A failed arm never silently reenables sleep.
  }
  for (const char *operation : {"disable:2", "unwake:2", "unwake:1", "type:2", "enable:2"}) {
    resetWake(); beginWake(); assert(armLightSleepWake()); testWakeFailOperation = operation;
    assert(!restoreLightSleepWake());
    assert(!lightSleepWakeState().ready && !lightSleepWakeState().armed);
    testWakeFailOperation.clear(); assert(!armLightSleepWake());
  }
  resetWake(); beginWake(); assert(armLightSleepWake());
  testWakeFailOperation = "disable:2"; menuLightSleepInterrupt();
  assert(!lightSleepWakeState().ready);
  testWakeFailOperation.clear(); assert(!restoreLightSleepWake());
  std::cout << "light-sleep wake tests passed\n";
}
