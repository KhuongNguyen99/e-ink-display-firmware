#pragma once
#include <Arduino.h>

constexpr esp_err_t ESP_ERR_NO_MEM = -4;
enum esp_pm_lock_type_t { ESP_PM_CPU_FREQ_MAX, ESP_PM_NO_LIGHT_SLEEP };
struct TestPmLock { explicit TestPmLock(esp_pm_lock_type_t value) : type(value) {} esp_pm_lock_type_t type; bool held = false; };
using esp_pm_lock_handle_t = TestPmLock *;
struct esp_pm_config_esp32s3_t {
  int max_freq_mhz;
  int min_freq_mhz;
  bool light_sleep_enable;
};

static esp_err_t testPmConfigResult = ESP_OK, testPmReadbackResult = ESP_OK;
static esp_err_t testPmCpuCreateResult = ESP_OK, testPmAwakeCreateResult = ESP_OK;
static esp_err_t testPmCpuAcquireResult = ESP_OK, testPmAwakeAcquireResult = ESP_OK;
static esp_err_t testPmCpuReleaseResult = ESP_OK, testPmAwakeReleaseResult = ESP_OK;
static esp_pm_config_esp32s3_t testPmConfig = {};
static bool testPmOverrideReadback = false;
static esp_pm_config_esp32s3_t testPmReadback = {};
static unsigned testPmConfigCalls = 0, testPmReadbackCalls = 0;
static unsigned testPmCreateCalls = 0, testPmDeleteCalls = 0;
static unsigned testPmCpuAcquireCalls = 0, testPmCpuReleaseCalls = 0;
static unsigned testPmAwakeAcquireCalls = 0, testPmAwakeReleaseCalls = 0;
static unsigned testPmDumpCalls = 0;

inline esp_err_t esp_pm_configure(const void *config) {
  ++testPmConfigCalls;
  if (testPmConfigResult == ESP_OK) testPmConfig = *static_cast<const esp_pm_config_esp32s3_t *>(config);
  return testPmConfigResult;
}
inline esp_err_t esp_pm_get_configuration(void *config) {
  ++testPmReadbackCalls;
  if (testPmReadbackResult == ESP_OK) {
    *static_cast<esp_pm_config_esp32s3_t *>(config) = testPmOverrideReadback ? testPmReadback : testPmConfig;
  }
  return testPmReadbackResult;
}
inline esp_err_t esp_pm_lock_create(esp_pm_lock_type_t type, int, const char *, esp_pm_lock_handle_t *handle) {
  ++testPmCreateCalls;
  const auto result = type == ESP_PM_CPU_FREQ_MAX ? testPmCpuCreateResult : testPmAwakeCreateResult;
  if (result == ESP_OK) *handle = new TestPmLock(type);
  return result;
}
inline esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t handle) {
  if (handle->type == ESP_PM_CPU_FREQ_MAX) {
    ++testPmCpuAcquireCalls;
    if (testPmCpuAcquireResult != ESP_OK) return testPmCpuAcquireResult;
  } else {
    ++testPmAwakeAcquireCalls;
    if (testPmAwakeAcquireResult != ESP_OK) return testPmAwakeAcquireResult;
  }
  handle->held = true;
  return ESP_OK;
}
inline esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t handle) {
  if (handle->type == ESP_PM_CPU_FREQ_MAX) {
    ++testPmCpuReleaseCalls;
    if (testPmCpuReleaseResult != ESP_OK) return testPmCpuReleaseResult;
  } else {
    ++testPmAwakeReleaseCalls;
    if (testPmAwakeReleaseResult != ESP_OK) return testPmAwakeReleaseResult;
  }
  handle->held = false;
  return ESP_OK;
}
inline esp_err_t esp_pm_lock_delete(esp_pm_lock_handle_t handle) {
  if (!handle || handle->held) return ESP_ERR_INVALID_STATE;
  ++testPmDeleteCalls;
  delete handle;
  return ESP_OK;
}
inline esp_err_t esp_pm_dump_locks(FILE *stream) {
  ++testPmDumpCalls;
  if (stream) fputs("PM profile\n", stream);
  return ESP_OK;
}
