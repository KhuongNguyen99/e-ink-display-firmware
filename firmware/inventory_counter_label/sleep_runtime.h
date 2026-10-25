#pragma once
#include <Arduino.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>

namespace inventory_counter {
// GPIO7 drives the Q9 panel-ground switch; VCI/VDDIO remain at 3V3. Retain HIGH
// and idle panel signals so opening ground cannot backpower through live IO.
constexpr gpio_num_t kPanelSleepPins[] = {GPIO_NUM_7, GPIO_NUM_10, GPIO_NUM_11,
                                        GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_14};
inline void setSleepPinLevels() {
  pinMode(7, OUTPUT); digitalWrite(7, HIGH);
  pinMode(14, OUTPUT); digitalWrite(14, HIGH);
  for (auto pin : kPanelSleepPins) { pinMode(pin, OUTPUT); digitalWrite(pin, pin == GPIO_NUM_11 ? LOW : HIGH); }
  pinMode(19, OUTPUT); digitalWrite(19, LOW);
}
inline void releaseSleepPins() {
  // Configure the post-reset outputs before releasing retained pad levels.
  setSleepPinLevels();
  gpio_deep_sleep_hold_dis();
  for (auto pin : kPanelSleepPins) gpio_hold_dis(pin);
  gpio_hold_dis(GPIO_NUM_19);
  rtc_gpio_deinit(GPIO_NUM_2);
}
inline bool stopSleepRadio() {
  if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED && esp_bluedroid_disable() != ESP_OK) return false;
  if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_INITIALIZED && esp_bluedroid_deinit() != ESP_OK) return false;
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED && esp_bt_controller_disable() != ESP_OK) return false;
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED && esp_bt_controller_deinit() != ESP_OK) return false;
  return true;
}
inline bool enterButtonSleep() {
  if (digitalRead(2) == LOW) return false;
  if (rtc_gpio_init(GPIO_NUM_2) != ESP_OK ||
      rtc_gpio_set_direction(GPIO_NUM_2, RTC_GPIO_MODE_INPUT_ONLY) != ESP_OK ||
      rtc_gpio_pullup_en(GPIO_NUM_2) != ESP_OK || rtc_gpio_pulldown_dis(GPIO_NUM_2) != ESP_OK ||
      esp_sleep_enable_ext0_wakeup(GPIO_NUM_2, 0) != ESP_OK) {
    rtc_gpio_deinit(GPIO_NUM_2); return false;
  }
  // A press racing RTC configuration must not create an immediate wake loop.
  if (rtc_gpio_get_level(GPIO_NUM_2) == 0) { rtc_gpio_deinit(GPIO_NUM_2); return false; }
  setSleepPinLevels();
  for (auto pin : kPanelSleepPins) if (gpio_hold_en(pin) != ESP_OK) { releaseSleepPins(); return false; }
  if (gpio_hold_en(GPIO_NUM_19) != ESP_OK) { releaseSleepPins(); return false; }
  gpio_deep_sleep_hold_en();
  esp_deep_sleep_start();
  return true;  // Only reached by the host-test deep-sleep stub.
}
}  // namespace inventory_counter
