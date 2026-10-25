#ifndef COUNTWELL_TEST_NO_PM
#define CONFIG_PM_ENABLE 1
#define CONFIG_PM_PROFILING 1
#define CONFIG_FREERTOS_USE_TICKLESS_IDLE 1
#endif
#include "../countwell_label/power_runtime.h"
#include <assert.h>
#include <iostream>

void resetController() {
  testControllerStatus = ESP_BT_CONTROLLER_STATUS_IDLE;
  testRadioEntropyReady = true;
  testControllerInitResult = testControllerEnableResult = testControllerSleepResult = ESP_OK;
  testControllerInitCalls = testControllerEnableCalls = testControllerSleepCalls = testControllerDeinitCalls = 0;
  countwell_power::controllerPowerState() = countwell_power::ControllerPowerState{};
}

#ifndef COUNTWELL_TEST_NO_PM
void resetPm() {
  auto &state = countwell_power::automaticPmState();
  assert(!state.pm_configured);
  assert(!state.cpu_lock && !state.awake_lock);
  state = countwell_power::AutomaticPmState{};
  testPmConfigResult = testPmReadbackResult = ESP_OK;
  testPmCpuCreateResult = testPmAwakeCreateResult = ESP_OK;
  testPmCpuAcquireResult = testPmAwakeAcquireResult = ESP_OK;
  testPmCpuReleaseResult = testPmAwakeReleaseResult = ESP_OK;
  testPmConfigCalls = testPmReadbackCalls = testPmCreateCalls = testPmDeleteCalls = 0;
  testPmCpuAcquireCalls = testPmCpuReleaseCalls = 0;
  testPmAwakeAcquireCalls = testPmAwakeReleaseCalls = testPmDumpCalls = 0;
  testPmOverrideReadback = false;
  testCpuMHz = 80;
}

void clearConfiguredPm() {
  auto &state = countwell_power::automaticPmState();
  if (state.awake_held) assert(esp_pm_lock_release(state.awake_lock) == ESP_OK);
  assert(esp_pm_lock_delete(state.awake_lock) == ESP_OK);
  assert(esp_pm_lock_delete(state.cpu_lock) == ESP_OK);
  state = countwell_power::AutomaticPmState{};
}

int main() {
  resetController();
  assert(beginPowerSavingBleController());
  assert(testControllerConfig.sleep_mode == ESP_BT_SLEEP_MODE_1);
  assert(testControllerConfig.sleep_clock == ESP_BT_SLEEP_CLOCK_MAIN_XTAL);
  assert(testControllerConfig.txpwr_dft == ESP_PWR_LVL_N0);
  assert(testControllerStatus == ESP_BT_CONTROLLER_STATUS_ENABLED);
  assert(testControllerInitCalls == 1 && testControllerEnableCalls == 1 && testControllerSleepCalls == 1);
  assert(beginPowerSavingBleController());
  assert(testControllerInitCalls == 1 && testControllerSleepCalls == 1);
  cJSON *diagnostics = cJSON_CreateObject();
  addPowerRuntimeDiagnostics(diagnostics);
  assert(cJSON_IsTrue(cJSON_GetObjectItem(diagnostics, "modem_sleep")));
  cJSON_Delete(diagnostics);

  resetController(); testControllerInitResult = ESP_FAIL;
  assert(!beginPowerSavingBleController());
  assert(testControllerEnableCalls == 0 && testControllerSleepCalls == 0);
  resetController(); testControllerEnableResult = ESP_FAIL;
  assert(!beginPowerSavingBleController());
  assert(testControllerDeinitCalls == 1 && testControllerSleepCalls == 0);
  assert(testControllerStatus == ESP_BT_CONTROLLER_STATUS_IDLE);
  resetController(); testControllerSleepResult = ESP_FAIL;
  assert(!beginPowerSavingBleController());
  diagnostics = cJSON_CreateObject(); addPowerRuntimeDiagnostics(diagnostics);
  assert(cJSON_IsFalse(cJSON_GetObjectItem(diagnostics, "modem_sleep")));
  cJSON_Delete(diagnostics);

  resetController(); testControllerStatus = ESP_BT_CONTROLLER_STATUS_ENABLED;
  assert(!beginPowerSavingBleController());
  assert(testControllerInitCalls == 0);

  resetPm(); testPmCpuCreateResult = ESP_ERR_NO_MEM;
  assert(!countwell_power::beginAutomaticLightSleep());
  assert(testPmCreateCalls == 1 && testPmDeleteCalls == 0 && testPmConfigCalls == 0);
  assert(!countwell_power::automaticPmState().auto_light_sleep);
  resetPm(); testPmAwakeCreateResult = ESP_ERR_NO_MEM;
  assert(!countwell_power::beginAutomaticLightSleep());
  assert(testPmCreateCalls == 2 && testPmDeleteCalls == 1 && testPmConfigCalls == 0);
  resetPm(); testPmAwakeAcquireResult = ESP_FAIL;
  assert(!countwell_power::beginAutomaticLightSleep());
  assert(testPmDeleteCalls == 2 && testPmConfigCalls == 0);
  resetPm(); testPmConfigResult = ESP_ERR_NOT_SUPPORTED;
  assert(!countwell_power::beginAutomaticLightSleep());
  assert(testPmDeleteCalls == 2 && testPmAwakeReleaseCalls == 1);
  assert(countwell_power::automaticPmState().config_result == ESP_ERR_NOT_SUPPORTED);
  {
    countwell_power::ScopedFastCpu fast;
    assert(testCpuMHz == 240 && testPmCpuAcquireCalls == 0);
  }
  assert(testCpuMHz == 80);

  resetPm(); testPmOverrideReadback = true; testPmReadback = {240, 80, false};
  assert(!countwell_power::beginAutomaticLightSleep());
  assert(countwell_power::automaticPmState().pm_configured);
  assert(countwell_power::automaticPmState().awake_held);
  assert(countwell_power::holdAwakeForWork());
  assert(!countwell_power::allowLightSleepWhileWaiting());
  diagnostics = cJSON_CreateObject(); addPowerRuntimeDiagnostics(diagnostics);
  assert(cJSON_IsFalse(cJSON_GetObjectItem(diagnostics, "auto_light_sleep")));
  cJSON_Delete(diagnostics);
  clearConfiguredPm();

  resetPm();
  assert(countwell_power::beginAutomaticLightSleep());
  assert(countwell_power::allowLightSleepWhileWaiting());
  testPmAwakeAcquireResult = ESP_FAIL;
  assert(countwell_power::holdAwakeForWork());
  assert(testPmConfigCalls == 2 && !testPmConfig.light_sleep_enable);
  assert(!countwell_power::automaticPmState().auto_light_sleep);
  assert(!countwell_power::automaticPmState().awake_held);
  assert(countwell_power::automaticPmState().awake_lock_result == ESP_FAIL);
  assert(countwell_power::automaticPmState().fallback_result == ESP_OK);
  assert(countwell_power::holdAwakeForWork() && testPmConfigCalls == 2);
  diagnostics = cJSON_CreateObject(); addPowerRuntimeDiagnostics(diagnostics);
  assert(cJSON_IsFalse(cJSON_GetObjectItem(diagnostics, "auto_light_sleep")));
  assert(cJSON_GetObjectItem(diagnostics, "auto_pm_fallback_result")->valueint == ESP_OK);
  cJSON_Delete(diagnostics);
  clearConfiguredPm();

  resetPm();
  assert(countwell_power::beginAutomaticLightSleep());
  assert(countwell_power::allowLightSleepWhileWaiting());
  testPmAwakeAcquireResult = ESP_FAIL;
  testPmConfigResult = ESP_FAIL;
  assert(!countwell_power::holdAwakeForWork());
  assert(countwell_power::automaticPmState().auto_light_sleep);
  assert(!countwell_power::automaticPmState().awake_held);
  assert(countwell_power::automaticPmState().awake_lock_result == ESP_FAIL);
  assert(countwell_power::automaticPmState().fallback_result == ESP_FAIL);
  assert(countwell_power::automaticPmState().config_result == ESP_OK);
  clearConfiguredPm();

  resetPm();
  assert(countwell_power::beginAutomaticLightSleep());
  auto &pm = countwell_power::automaticPmState();
  assert(pm.pm_configured && pm.auto_light_sleep && pm.awake_held);
  assert(testPmConfig.max_freq_mhz == 240 && testPmConfig.min_freq_mhz == 80);
  assert(testPmConfig.light_sleep_enable && testPmReadbackCalls == 1);
  assert(countwell_power::beginAutomaticLightSleep() && testPmConfigCalls == 1);
  assert(countwell_power::holdAwakeForWork() && testPmAwakeAcquireCalls == 1);
  {
    countwell_power::ScopedFastCpu fast;
    assert(testPmCpuAcquireCalls == 1 && testCpuMHz == 80);
  }
  assert(testPmCpuReleaseCalls == 1 && pm.cpu_lock_result == ESP_OK);
  testPmCpuAcquireResult = ESP_FAIL;
  {
    countwell_power::ScopedFastCpu fast;
    assert(testPmCpuAcquireCalls == 2 && testCpuMHz == 80);
  }
  assert(testPmCpuReleaseCalls == 1 && pm.cpu_lock_result == ESP_FAIL);
  testPmCpuAcquireResult = ESP_OK;
  testPmAwakeReleaseResult = ESP_FAIL;
  assert(!countwell_power::allowLightSleepWhileWaiting() && pm.awake_held);
  assert(pm.awake_lock_result == ESP_FAIL);
  testPmAwakeReleaseResult = ESP_OK;
  assert(countwell_power::allowLightSleepWhileWaiting());
  assert(!pm.awake_held && testPmAwakeReleaseCalls == 2);
  assert(!countwell_power::allowLightSleepWhileWaiting());
  assert(countwell_power::holdAwakeForWork() && testPmAwakeAcquireCalls == 2);
  diagnostics = cJSON_CreateObject(); addPowerRuntimeDiagnostics(diagnostics);
  assert(cJSON_IsTrue(cJSON_GetObjectItem(diagnostics, "auto_pm_available")));
  assert(cJSON_IsTrue(cJSON_GetObjectItem(diagnostics, "auto_light_sleep")));
  assert(cJSON_IsTrue(cJSON_GetObjectItem(diagnostics, "auto_pm_awake_held")));
  assert(cJSON_GetObjectItem(diagnostics, "auto_pm_readback_result")->valueint == ESP_OK);
  cJSON_Delete(diagnostics);
  assert(countwell_power::dumpPmLocks(nullptr) == ESP_OK && testPmDumpCalls == 1);
  clearConfiguredPm();
  std::cout << "BLE controller power lifecycle tests passed\n";
}
#else
int main() {
  assert(!countwell_power::beginAutomaticLightSleep());
  assert(countwell_power::holdAwakeForWork());
  assert(!countwell_power::allowLightSleepWhileWaiting());
  assert(!countwell_power::automaticPmState().auto_light_sleep);
  assert(countwell_power::dumpPmLocks(nullptr) == ESP_ERR_NOT_SUPPORTED);
  testCpuMHz = 80;
  {
    countwell_power::ScopedFastCpu fast;
    assert(testCpuMHz == 240);
  }
  assert(testCpuMHz == 80);
  cJSON *diagnostics = cJSON_CreateObject();
  addPowerRuntimeDiagnostics(diagnostics);
  assert(cJSON_IsFalse(cJSON_GetObjectItem(diagnostics, "auto_pm_available")));
  assert(cJSON_IsFalse(cJSON_GetObjectItem(diagnostics, "auto_light_sleep")));
  cJSON_Delete(diagnostics);
  std::cout << "Stock-build PM fallback tests passed\n";
}
#endif
