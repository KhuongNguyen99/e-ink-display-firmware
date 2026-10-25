#pragma once
#include <Arduino.h>
#include <cstdio>

#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE && \
    defined(CONFIG_FREERTOS_USE_TICKLESS_IDLE) && CONFIG_FREERTOS_USE_TICKLESS_IDLE
#define INVENTORY_COUNTER_AUTOMATIC_PM_COMPILED 1
#include <esp_pm.h>
#else
#define INVENTORY_COUNTER_AUTOMATIC_PM_COMPILED 0
#endif

namespace inventory_counter_power {

struct AutomaticPmState {
  esp_err_t config_result = ESP_ERR_NOT_SUPPORTED;
  esp_err_t readback_result = ESP_ERR_INVALID_STATE;
  esp_err_t lock_result = ESP_ERR_INVALID_STATE;
  esp_err_t awake_lock_result = ESP_ERR_INVALID_STATE;
  esp_err_t fallback_result = ESP_ERR_INVALID_STATE;
  esp_err_t cpu_lock_result = ESP_ERR_INVALID_STATE;
  bool pm_configured = false;
  bool auto_light_sleep = false;
  bool awake_held = false;
#if INVENTORY_COUNTER_AUTOMATIC_PM_COMPILED
  esp_pm_lock_handle_t cpu_lock = nullptr;
  esp_pm_lock_handle_t awake_lock = nullptr;
#endif
};

inline AutomaticPmState &automaticPmState() {
  static AutomaticPmState state;
  return state;
}

inline bool beginAutomaticLightSleep() {
  auto &state = automaticPmState();
  if (state.pm_configured) return state.auto_light_sleep;
#if INVENTORY_COUNTER_AUTOMATIC_PM_COMPILED
  state.lock_result = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "inventory_counter_cpu", &state.cpu_lock);
  if (state.lock_result != ESP_OK) return false;
  state.lock_result = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "inventory_counter_work", &state.awake_lock);
  if (state.lock_result != ESP_OK) {
    esp_pm_lock_delete(state.cpu_lock); state.cpu_lock = nullptr;
    return false;
  }
  state.awake_lock_result = esp_pm_lock_acquire(state.awake_lock);
  if (state.awake_lock_result != ESP_OK) {
    esp_pm_lock_delete(state.awake_lock); state.awake_lock = nullptr;
    esp_pm_lock_delete(state.cpu_lock); state.cpu_lock = nullptr;
    return false;
  }
  state.awake_held = true;
  esp_pm_config_esp32s3_t config = {240, 80, true};
  state.config_result = esp_pm_configure(&config);
  if (state.config_result != ESP_OK) {
    esp_pm_lock_release(state.awake_lock); state.awake_held = false;
    esp_pm_lock_delete(state.awake_lock); state.awake_lock = nullptr;
    esp_pm_lock_delete(state.cpu_lock); state.cpu_lock = nullptr;
    return false;
  }
  state.pm_configured = true;
  esp_pm_config_esp32s3_t readback = {};
  state.readback_result = esp_pm_get_configuration(&readback);
  state.auto_light_sleep = state.readback_result == ESP_OK &&
                           readback.max_freq_mhz == 240 && readback.min_freq_mhz == 80 &&
                           readback.light_sleep_enable;
  // Retain NO_LIGHT_SLEEP if the requested configuration cannot be verified.
  return state.auto_light_sleep;
#else
  return false;
#endif
}

inline bool holdAwakeForWork() {
  auto &state = automaticPmState();
#if INVENTORY_COUNTER_AUTOMATIC_PM_COMPILED
  if (!state.auto_light_sleep || state.awake_held) return true;
  state.awake_lock_result = esp_pm_lock_acquire(state.awake_lock);
  if (state.awake_lock_result == ESP_OK) {
    state.awake_held = true;
    return true;
  }
  esp_pm_config_esp32s3_t fallback = {240, 80, false};
  state.fallback_result = esp_pm_configure(&fallback);
  if (state.fallback_result == ESP_OK) {
    state.auto_light_sleep = false;
    return true;
  }
#endif
  return !state.auto_light_sleep;
}

inline bool allowLightSleepWhileWaiting() {
  auto &state = automaticPmState();
#if INVENTORY_COUNTER_AUTOMATIC_PM_COMPILED
  if (!state.auto_light_sleep || !state.awake_held) return false;
  state.awake_lock_result = esp_pm_lock_release(state.awake_lock);
  if (state.awake_lock_result == ESP_OK) state.awake_held = false;
#endif
  return !state.awake_held && state.auto_light_sleep;
}

class ScopedFastCpu {
 public:
  ScopedFastCpu() : previous_mhz_(getCpuFrequencyMhz()) {
#if INVENTORY_COUNTER_AUTOMATIC_PM_COMPILED
    auto &state = automaticPmState();
    if (state.pm_configured) {
      state.cpu_lock_result = esp_pm_lock_acquire(state.cpu_lock);
      locked_ = state.cpu_lock_result == ESP_OK;
      return;
    }
#endif
    if (previous_mhz_ != 240) boosted_ = setCpuFrequencyMhz(240);
  }

  ~ScopedFastCpu() {
#if INVENTORY_COUNTER_AUTOMATIC_PM_COMPILED
    if (locked_) {
      automaticPmState().cpu_lock_result = esp_pm_lock_release(automaticPmState().cpu_lock);
      return;
    }
#endif
    if (boosted_) setCpuFrequencyMhz(previous_mhz_);
  }

  ScopedFastCpu(const ScopedFastCpu &) = delete;
  ScopedFastCpu &operator=(const ScopedFastCpu &) = delete;

 private:
  uint32_t previous_mhz_;
  bool boosted_ = false;
#if INVENTORY_COUNTER_AUTOMATIC_PM_COMPILED
  bool locked_ = false;
#endif
};

inline esp_err_t dumpPmLocks(FILE *stream = stdout) {
#if INVENTORY_COUNTER_AUTOMATIC_PM_COMPILED && defined(CONFIG_PM_PROFILING) && CONFIG_PM_PROFILING
  return automaticPmState().pm_configured ? esp_pm_dump_locks(stream) : ESP_ERR_INVALID_STATE;
#else
  (void)stream;
  return ESP_ERR_NOT_SUPPORTED;
#endif
}

}  // namespace inventory_counter_power
