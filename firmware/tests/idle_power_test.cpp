// Exercise the real loop with a fake clock, radio, UART, and FreeRTOS task.
#define CONFIG_PM_ENABLE 1
#define CONFIG_PM_PROFILING 1
#define CONFIG_FREERTOS_USE_TICKLESS_IDLE 1
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main sleepFixtureMain
#include "sleep_test.cpp"
#undef main
#pragma GCC diagnostic pop

void quietAt(uint32_t now) {
  testMillis = now;
  connected = disconnectRequested = pairingOpen = receiving = busy = false;
  menuDown = menuLatched = false;
  menuButton.begin(now - 100, false);
  testPinLevels[panel::kMenuPin] = testPinLevels[panel::kBackPin] = HIGH;
  screenIsLabel = true;
  qrVisibleRequested = qrVisibleOnPanel = false;
  findActive = findOnPanel = findTimerArmed = false;
  awakeWindow.armed = fastAdvertisingWindow.armed = false;
  qrRetryPending = advertisingRetryPending = false;
  advertisingPhase = AdvertisingPhase::Active;
  advertisingFast = false;
  usbRecentlyActive = false;
  testWakeFailOperation.clear();
  Serial.input.clear();
  testPendingTaskNotifications = 0;
}

void expectWait(uint32_t expected) {
  testNotifyTakeCalls = 0;
  loop();
  assert(testNotifyTakeCalls == 1 && testLastNotifyTimeout == expected);
  assert(testControllerStatus == ESP_BT_CONTROLLER_STATUS_ENABLED);
  assert(testBluedroidStatus == ESP_BLUEDROID_STATUS_ENABLED);
}

int main() {
  testMillis = 1000;
  installFixture();
  connected = false;
  testControllerStatus = ESP_BT_CONTROLLER_STATUS_IDLE;
  setup();
  assert(inventory_counter_power::automaticPmState().auto_light_sleep);
  assert(inventory_counter_power::lightSleepWakeState().ready);
  assert(Serial.receiveCallback == wakeLoop && !Serial.receiveOnlyOnTimeout);
  assert(powerLoopTaskHandle == xTaskGetCurrentTaskHandle());
  assert(testAdvertisingIntervals.back() == power_policy::kFastAdvertisingUnits);

  quietAt(10000);
  const uint32_t firstIteration = loopIterations;
  expectWait(power_policy::kIdleWaitMs);
  assert(loopIterations == firstIteration + 1 && maxLoopWaitMs == power_policy::kIdleWaitMs);
  assert(!inventory_counter_power::lightSleepWakeState().armed);

  // An incoming byte during the task wait invokes the registered UART callback.
  quietAt(11000);
  testTaskNotifyCalls = testPendingTaskNotifications = 0;
  testNotifyTakeHook = []() { Serial.inject("CWUSB {\"op\":\"usb_power\"}\n"); };
  expectWait(power_policy::kIdleWaitMs);
  assert(testTaskNotifyCalls == 1 && testLastTaskNotified == powerLoopTaskHandle);
  assert(testLastNotifyTakeResult == 1 && Serial.available() > 0);
  Serial.output.clear();
  expectWait(power_policy::kPollMs);
  assert(!Serial.available());
  assert(Serial.output.find("\"loop_iterations\":") != std::string::npos);
  assert(Serial.output.find("\"max_loop_wait_ms\":1000") != std::string::npos);
  assert(Serial.output.find("\"advertising_interval_ms\":1000") != std::string::npos);
  assert(Serial.output.find("\"ble_always_available\":true") != std::string::npos);
  testMillis += 1000;
  expectWait(power_policy::kIdleWaitMs);

  quietAt(20000);
  menuButton.begin(millis(), false);  // a released button still needs its debounce interval
  expectWait(power_policy::kPollMs);
  testMillis += kMenuDebounceMs;
  expectWait(power_policy::kIdleWaitMs);

  quietAt(30000);
  connected = true; connectionTouched = connectionStarted = millis();
  expectWait(power_policy::kPollMs);
  quietAt(31000);
  receiving = true; transferTouched = millis();
  expectWait(power_policy::kPollMs);
  quietAt(32000);
  busy = true;
  expectWait(power_policy::kPollMs);

  // The loop must wake at the exact panel and advertising deadlines.
  quietAt(40000);
  awakeWindow.begin(millis() - kAwakeWindowMs + 37);
  qrVisibleRequested = qrVisibleOnPanel = true;
  expectWait(37);
  quietAt(41000);
  findActive = findOnPanel = findTimerArmed = true;
  findStarted = millis() - kFindDurationMs + 29;
  expectWait(29);
  quietAt(42000);
  screenIsLabel = false;
  qrRetryPending = true; qrRetryAt = millis() - 1000 + 17;
  expectWait(17);
  quietAt(43000);
  advertisingPhase = AdvertisingPhase::Stopped;
  advertisingRetryPending = true; advertisingRetryAt = millis() - 1000 + 13;
  expectWait(13);

  quietAt(20);  // QR deadline crosses millis() wraparound.
  awakeWindow.begin(millis() - kAwakeWindowMs + 41);
  qrVisibleRequested = qrVisibleOnPanel = true;
  expectWait(41);

  quietAt(50000);
  testWakeFailOperation = "wake:2";
  expectWait(power_policy::kPollMs);
  assert(!inventory_counter_power::lightSleepWakeState().ready);
  assert(inventory_counter_power::automaticPmState().awake_held);
  std::cout << "Idle power and UART wake tests passed\n";
}
