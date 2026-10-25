#pragma once
#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_sleep.h>

namespace countwell_power {

struct LightSleepWakeState {
  gpio_num_t menu_pin = static_cast<gpio_num_t>(-1);
  gpio_num_t back_pin = static_cast<gpio_num_t>(-1);
  TaskHandle_t loop_task = nullptr;
  bool initialized = false;
  volatile bool ready = false;
  volatile bool armed = false;
  esp_err_t setup_result = ESP_ERR_INVALID_STATE;
  esp_err_t uart_result = ESP_ERR_INVALID_STATE;
  esp_err_t gpio_result = ESP_ERR_INVALID_STATE;
  esp_err_t arm_result = ESP_ERR_INVALID_STATE;
  esp_err_t restore_result = ESP_ERR_INVALID_STATE;
  volatile esp_err_t isr_mask_result = ESP_OK;
};

inline LightSleepWakeState &lightSleepWakeState() {
  static LightSleepWakeState state;
  return state;
}

inline void keepWakeError(esp_err_t result, esp_err_t &first) {
  if (first == ESP_OK && result != ESP_OK) first = result;
}

// The caller holds NO_LIGHT_SLEEP throughout setup, arming, and restoration.
inline bool beginLightSleepWake(gpio_num_t menu, gpio_num_t back, TaskHandle_t task) {
  auto &state = lightSleepWakeState();
  if (state.armed || menu == back || !task) return false;
  state = LightSleepWakeState{};
  state.menu_pin = menu; state.back_pin = back; state.loop_task = task;
  state.initialized = true;
  // S3 Arduino UART0 is already clocked from the retained main crystal. The
  // sender must send a wake preamble: UART drops characters that trigger wake.
  state.uart_result = uart_set_wakeup_threshold(UART_NUM_0, 3);
  if (state.uart_result == ESP_OK) state.uart_result = esp_sleep_enable_uart_wakeup(UART_NUM_0);
  state.gpio_result = esp_sleep_enable_gpio_wakeup();
  state.setup_result = state.uart_result != ESP_OK ? state.uart_result : state.gpio_result;
  state.ready = state.setup_result == ESP_OK;
  return state.ready;
}

inline bool restoreLightSleepWake() {
  auto &state = lightSleepWakeState();
  if (!state.initialized) return false;
  esp_err_t result = ESP_OK;
  keepWakeError(gpio_intr_disable(state.menu_pin), result);
  state.armed = false;
  keepWakeError(gpio_wakeup_disable(state.menu_pin), result);
  keepWakeError(gpio_wakeup_disable(state.back_pin), result);
  // gpio_wakeup_enable overwrites int_type; disabling wake does not restore it.
  keepWakeError(gpio_set_intr_type(state.menu_pin, GPIO_INTR_ANYEDGE), result);
  keepWakeError(gpio_intr_enable(state.menu_pin), result);
  state.restore_result = result;
  if (result != ESP_OK || state.isr_mask_result != ESP_OK) state.ready = false;
  return state.ready;
}

inline bool armLightSleepWake() {
  auto &state = lightSleepWakeState();
  if (!state.ready || state.armed || !gpio_get_level(state.menu_pin) || !gpio_get_level(state.back_pin)) return false;
  esp_err_t result = ESP_OK;
  keepWakeError(gpio_intr_disable(state.menu_pin), result);
  keepWakeError(gpio_intr_disable(state.back_pin), result);
  // Set this before enabling a level interrupt: an already arriving press must
  // be handled as one-shot rather than retriggering while the button is held.
  state.armed = true;
  keepWakeError(gpio_wakeup_enable(state.menu_pin, GPIO_INTR_LOW_LEVEL), result);
  keepWakeError(gpio_wakeup_enable(state.back_pin, GPIO_INTR_LOW_LEVEL), result);
  keepWakeError(gpio_intr_enable(state.menu_pin), result);
  state.arm_result = result;
  if (result != ESP_OK) {
    state.ready = false;
    restoreLightSleepWake();
    return false;
  }
  // A press racing arming is serviced while awake; do not release the PM lock.
  if (!gpio_get_level(state.menu_pin) || !gpio_get_level(state.back_pin) || !state.ready) {
    restoreLightSleepWake();
    return false;
  }
  return true;
}

// Call after the Menu ISR has sampled the edge and exited its critical section.
inline void IRAM_ATTR menuLightSleepInterrupt() {
  auto &state = lightSleepWakeState();
  if (state.armed) {
    state.isr_mask_result = gpio_intr_disable(state.menu_pin);
    if (state.isr_mask_result != ESP_OK) state.ready = false;
  }
  if (state.loop_task) {
    BaseType_t higher_priority_woken = pdFALSE;
    vTaskNotifyGiveFromISR(state.loop_task, &higher_priority_woken);
    if (higher_priority_woken) portYIELD_FROM_ISR();
  }
}

}  // namespace countwell_power
