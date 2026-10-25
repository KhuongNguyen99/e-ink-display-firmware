#pragma once
#include <Arduino.h>
#include <esp_bt.h>
#include <cJSON.h>
#include "automatic_pm.h"

namespace countwell_power {
struct ControllerPowerState {
  esp_err_t init_result = ESP_ERR_INVALID_STATE;
  esp_err_t enable_result = ESP_ERR_INVALID_STATE;
  esp_err_t sleep_result = ESP_ERR_INVALID_STATE;
  bool enabled = false;
};

inline ControllerPowerState &controllerPowerState() {
  static ControllerPowerState state;
  return state;
}
}

inline bool beginPowerSavingBleController() {
  auto &state = countwell_power::controllerPowerState();
  if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) return state.enabled;
  esp_bt_controller_config_t config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  // These are documented runtime configuration fields; keep the pinned SDK intact.
  config.sleep_mode = ESP_BT_SLEEP_MODE_1;
  config.sleep_clock = ESP_BT_SLEEP_CLOCK_MAIN_XTAL;
  config.txpwr_dft = ESP_PWR_LVL_N0;
  state.init_result = esp_bt_controller_init(&config);
  if (state.init_result != ESP_OK) return false;
  state.enable_result = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (state.enable_result != ESP_OK) {
    esp_bt_controller_deinit();
    return false;
  }
  state.sleep_result = esp_bt_sleep_enable();
  state.enabled = state.sleep_result == ESP_OK;
  return state.enabled;
}

inline void addPowerRuntimeDiagnostics(cJSON *output) {
  const auto &state = countwell_power::controllerPowerState();
  const auto &pm = countwell_power::automaticPmState();
  cJSON_AddNumberToObject(output, "cpu_mhz", getCpuFrequencyMhz());
  cJSON_AddBoolToObject(output, "modem_sleep", state.enabled);
  cJSON_AddStringToObject(output, "sleep_clock", state.enabled ? "main_xtal" : "unavailable");
  cJSON_AddNumberToObject(output, "ble_init_result", state.init_result);
  cJSON_AddNumberToObject(output, "ble_enable_result", state.enable_result);
  cJSON_AddNumberToObject(output, "ble_sleep_result", state.sleep_result);
  cJSON_AddBoolToObject(output, "auto_pm_available", COUNTWELL_AUTOMATIC_PM_COMPILED);
  cJSON_AddBoolToObject(output, "auto_light_sleep", pm.auto_light_sleep);
  cJSON_AddBoolToObject(output, "auto_pm_configured", pm.pm_configured);
  cJSON_AddBoolToObject(output, "auto_pm_awake_held", pm.awake_held);
  cJSON_AddNumberToObject(output, "auto_pm_config_result", pm.config_result);
  cJSON_AddNumberToObject(output, "auto_pm_readback_result", pm.readback_result);
  cJSON_AddNumberToObject(output, "auto_pm_lock_result", pm.lock_result);
  cJSON_AddNumberToObject(output, "auto_pm_awake_lock_result", pm.awake_lock_result);
  cJSON_AddNumberToObject(output, "auto_pm_fallback_result", pm.fallback_result);
  cJSON_AddNumberToObject(output, "auto_pm_cpu_lock_result", pm.cpu_lock_result);
}
