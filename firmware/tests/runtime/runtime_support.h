#pragma once
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <map>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using String = std::string;
static uint32_t testMillis = 1;
inline uint32_t millis() { return testMillis; }
inline void delay(uint32_t ms) { testMillis += ms; }
static uint32_t testCpuMHz = 240;
inline uint32_t getCpuFrequencyMhz() { return testCpuMHz; }
inline bool setCpuFrequencyMhz(uint32_t mhz) { testCpuMHz = mhz; return true; }
using TaskHandle_t = void *;
using TickType_t = uint32_t;
using BaseType_t = int;
constexpr int pdFALSE = 0;
constexpr int pdTRUE = 1;
static unsigned testIsrNotifyCalls = 0, testIsrYieldCalls = 0;
inline void vTaskNotifyGiveFromISR(TaskHandle_t, BaseType_t *woken) { ++testIsrNotifyCalls; *woken = pdTRUE; }
inline void testYieldFromIsr() { ++testIsrYieldCalls; }
#define portYIELD_FROM_ISR() testYieldFromIsr()
inline TickType_t pdMS_TO_TICKS(uint32_t ms) { return ms; }
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return reinterpret_cast<void *>(1); }
inline void xTaskNotifyGive(TaskHandle_t) {}
inline uint32_t ulTaskNotifyTake(int, TickType_t) { return 0; }
constexpr int HIGH = 1, LOW = 0, OUTPUT = 1, INPUT_PULLUP = 2;
#define IRAM_ATTR
using portMUX_TYPE = int;
constexpr int portMUX_INITIALIZER_UNLOCKED = 0, FALLING = 3, CHANGE = 4;
inline void portENTER_CRITICAL_ISR(portMUX_TYPE *) {}
inline void portEXIT_CRITICAL_ISR(portMUX_TYPE *) {}
inline void portENTER_CRITICAL(portMUX_TYPE *) {}
inline void portEXIT_CRITICAL(portMUX_TYPE *) {}
static void (*testPinInterrupt)() = nullptr;
static int testInterruptPin = -1, testInterruptMode = -1;
inline int digitalPinToInterrupt(uint8_t pin) { return pin; }
inline void attachInterrupt(int pin, void (*callback)(), int mode) {
  testInterruptPin = pin; testPinInterrupt = callback; testInterruptMode = mode;
}
inline void pinMode(uint8_t, int) {}
static std::map<uint8_t, int> testPinLevels;
inline void digitalWrite(uint8_t pin, int level) { testPinLevels[pin] = level; }
inline int digitalRead(uint8_t pin) { return testPinLevels.count(pin) ? testPinLevels[pin] : HIGH; }
inline size_t strlcpy(char *out, const char *in, size_t size) {
  const size_t length = strlen(in);
  if (size) { const size_t copied = std::min(size - 1, length); memcpy(out, in, copied); out[copied] = 0; }
  return length;
}
struct TestSerial {
  std::string output;
  void begin(int) {}
  int available() { return 0; }
  int read() { return -1; }
  void print(const std::string &s) { output += s; }
  void println(const std::string &s) { output += s + "\n"; }
  void printf(const char *format, ...) {
    char text[512]; va_list args; va_start(args, format); vsnprintf(text, sizeof(text), format, args); va_end(args); output += text;
  }
};
static TestSerial Serial;
struct TestESP { uint64_t getEfuseMac() { return 0x18cedc8fcba4; } };
static TestESP ESP __attribute__((unused));
using SemaphoreHandle_t = void *;
constexpr int portMAX_DELAY = -1;
inline void *xSemaphoreCreateMutex() { return nullptr; }
inline void xSemaphoreTake(void *, int) {}
inline void xSemaphoreGive(void *) {}
inline uint32_t uxTaskGetStackHighWaterMark(void *) { return 1024; }
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_STATE = -2,
              ESP_ERR_NOT_SUPPORTED = -3, ESP_BT_STATUS_SUCCESS = 0;
constexpr int ESP_BT_CONTROLLER_STATUS_IDLE = 0, ESP_BT_CONTROLLER_STATUS_INITED = 1,
              ESP_BT_CONTROLLER_STATUS_ENABLED = 2;
static bool testRadioEntropyReady = true;
static int testControllerStatus = ESP_BT_CONTROLLER_STATUS_ENABLED;
inline int esp_bt_controller_get_status() { return testRadioEntropyReady ? testControllerStatus : ESP_BT_CONTROLLER_STATUS_IDLE; }
static void (*testRandomOverride)(void *, size_t) = nullptr;
inline void esp_fill_random(void *buffer, size_t size) {
  if (testRandomOverride) { testRandomOverride(buffer, size); return; }
  static uint8_t sequence = 0;
  auto *bytes = static_cast<uint8_t *>(buffer);
  for (size_t i = 0; i < size; ++i) bytes[i] = ++sequence;
}
class Preferences {
 public:
  std::map<std::string, std::vector<uint8_t>> values;
  bool failWrites = false;
  std::string failKey;
  std::string failRemoveKey;
  uint32_t writeAdvanceMs = 0;
  size_t capacityBytes = std::numeric_limits<size_t>::max();
  std::vector<std::string> removedKeys;
  bool begin(const char *, bool) { return true; }
  size_t getBytesLength(const char *key) { return values[key].size(); }
  size_t getBytes(const char *key, void *value, size_t size) {
    size = std::min(size, values[key].size()); memcpy(value, values[key].data(), size); return size;
  }
  size_t putBytes(const char *key, const void *value, size_t size) {
    testMillis += writeAdvanceMs;
    if (failWrites || failKey == key) return 0;
    size_t used = 0;
    for (const auto &entry : values) if (entry.first != key) used += entry.second.size();
    if (size > capacityBytes - std::min(used, capacityBytes)) return 0;
    const auto *bytes = static_cast<const uint8_t *>(value); values[key] = std::vector<uint8_t>(bytes, bytes + size); return size;
  }
  bool remove(const char *key) {
    if (failWrites || failRemoveKey == key || !values.count(key)) return false;
    values.erase(key); removedKeys.push_back(key); return true;
  }
  uint8_t getUChar(const char *key, uint8_t fallback) { return values[key].empty() ? fallback : values[key][0]; }
  size_t putUChar(const char *key, uint8_t value) { return putBytes(key, &value, 1); }
};

using esp_bd_addr_t = uint8_t[6];
using esp_gatt_if_t = uint8_t;
constexpr esp_gatt_if_t ESP_GATT_IF_NONE = 0xff;
constexpr int ESP_BT_SLEEP_MODE_1 = 1, ESP_BT_SLEEP_CLOCK_MAIN_XTAL = 2,
              ESP_BT_MODE_BLE = 1;
struct esp_bt_controller_config_t { int sleep_mode = 0, sleep_clock = 0, txpwr_dft = 0; };
inline esp_bt_controller_config_t BT_CONTROLLER_INIT_CONFIG_DEFAULT() { return {}; }
static esp_bt_controller_config_t testControllerConfig;
static esp_err_t testControllerInitResult = ESP_OK, testControllerEnableResult = ESP_OK,
                 testControllerSleepResult = ESP_OK;
static unsigned testControllerInitCalls = 0, testControllerEnableCalls = 0,
                testControllerSleepCalls = 0, testControllerDeinitCalls = 0;
inline esp_err_t esp_bt_controller_init(const esp_bt_controller_config_t *config) {
  ++testControllerInitCalls; testControllerConfig = *config;
  if (testControllerInitResult == ESP_OK) testControllerStatus = ESP_BT_CONTROLLER_STATUS_INITED;
  return testControllerInitResult;
}
inline esp_err_t esp_bt_controller_enable(int mode) {
  ++testControllerEnableCalls;
  if (mode != ESP_BT_MODE_BLE) return ESP_FAIL;
  if (testControllerEnableResult == ESP_OK) testControllerStatus = ESP_BT_CONTROLLER_STATUS_ENABLED;
  return testControllerEnableResult;
}
inline esp_err_t esp_bt_controller_deinit() {
  ++testControllerDeinitCalls; testControllerStatus = ESP_BT_CONTROLLER_STATUS_IDLE; return ESP_OK;
}
inline esp_err_t esp_bt_sleep_enable() { ++testControllerSleepCalls; return testControllerSleepResult; }
static std::vector<std::string> testSleepEvents;
static int testBluedroidStatus = 2;
constexpr int ESP_BLUEDROID_STATUS_UNINITIALIZED = 0, ESP_BLUEDROID_STATUS_INITIALIZED = 1,
              ESP_BLUEDROID_STATUS_ENABLED = 2;
inline int esp_bluedroid_get_status() { return testBluedroidStatus; }
inline esp_err_t esp_bluedroid_disable() {
  testSleepEvents.push_back("host_disable"); testBluedroidStatus = 1; return ESP_OK;
}
inline esp_err_t esp_bluedroid_deinit() {
  testSleepEvents.push_back("host_deinit"); testBluedroidStatus = 0; return ESP_OK;
}
inline esp_err_t esp_bt_controller_disable() {
  testSleepEvents.push_back("controller_disable"); testControllerStatus = ESP_BT_CONTROLLER_STATUS_INITED; return ESP_OK;
}
using gpio_num_t = int;
using gpio_int_type_t = int;
constexpr int GPIO_INTR_DISABLE = 0, GPIO_INTR_ANYEDGE = 3, GPIO_INTR_LOW_LEVEL = 4;
constexpr int UART_NUM_0 = 0;
static std::map<int, bool> testGpioInterruptEnabled, testGpioWakeEnabled;
static std::map<int, int> testGpioInterruptType;
static std::vector<std::string> testWakeOperations;
static std::string testWakeFailOperation;
static void (*testWakeOnMenuEnable)() = nullptr;
static int testUartWakeThreshold = 0;
inline esp_err_t testWakeOperation(const std::string &operation) {
  testWakeOperations.push_back(operation);
  return operation == testWakeFailOperation ? ESP_FAIL : ESP_OK;
}
inline esp_err_t gpio_intr_disable(gpio_num_t pin) {
  const esp_err_t result = testWakeOperation("disable:" + std::to_string(pin));
  if (result == ESP_OK) testGpioInterruptEnabled[pin] = false;
  return result;
}
inline esp_err_t gpio_intr_enable(gpio_num_t pin) {
  const esp_err_t result = testWakeOperation("enable:" + std::to_string(pin));
  if (result == ESP_OK) {
    testGpioInterruptEnabled[pin] = true;
    if (pin == 2 && testWakeOnMenuEnable) {
      auto callback = testWakeOnMenuEnable; testWakeOnMenuEnable = nullptr; callback();
    }
  }
  return result;
}
inline esp_err_t gpio_wakeup_enable(gpio_num_t pin, gpio_int_type_t mode) {
  const esp_err_t result = testWakeOperation("wake:" + std::to_string(pin));
  if (result == ESP_OK) { testGpioWakeEnabled[pin] = true; testGpioInterruptType[pin] = mode; }
  return result;
}
inline esp_err_t gpio_wakeup_disable(gpio_num_t pin) {
  const esp_err_t result = testWakeOperation("unwake:" + std::to_string(pin));
  if (result == ESP_OK) testGpioWakeEnabled[pin] = false;
  return result;
}
inline esp_err_t gpio_set_intr_type(gpio_num_t pin, gpio_int_type_t mode) {
  const esp_err_t result = testWakeOperation("type:" + std::to_string(pin));
  if (result == ESP_OK) testGpioInterruptType[pin] = mode;
  return result;
}
inline int gpio_get_level(gpio_num_t pin) { return digitalRead(pin); }
inline esp_err_t uart_set_wakeup_threshold(int, int threshold) {
  testUartWakeThreshold = threshold; return testWakeOperation("uart_threshold");
}
inline esp_err_t esp_sleep_enable_uart_wakeup(int) { return testWakeOperation("uart_wake"); }
inline esp_err_t esp_sleep_enable_gpio_wakeup() { return testWakeOperation("gpio_wake"); }
constexpr int GPIO_NUM_2 = 2, GPIO_NUM_7 = 7, GPIO_NUM_10 = 10, GPIO_NUM_11 = 11,
              GPIO_NUM_12 = 12, GPIO_NUM_13 = 13, GPIO_NUM_14 = 14, GPIO_NUM_19 = 19;
constexpr int RTC_GPIO_MODE_INPUT_ONLY = 0;
static std::map<int, bool> testHeldPins;
static bool testDeepSleepHold = false, testRtcInitialized = false, testRtcPullup = false, testRtcPulldown = true;
static int testRtcDirection = -1, testWakePin = -1, testWakeLevel = -1;
static int testHoldFailPin = -1;
static unsigned testDeepSleepCalls = 0;
inline esp_err_t gpio_hold_en(gpio_num_t pin) {
  if (pin == testHoldFailPin) return ESP_FAIL;
  testHeldPins[pin] = true; return ESP_OK;
}
inline esp_err_t gpio_hold_dis(gpio_num_t pin) { testHeldPins[pin] = false; return ESP_OK; }
inline void gpio_deep_sleep_hold_en() { testDeepSleepHold = true; }
inline void gpio_deep_sleep_hold_dis() { testDeepSleepHold = false; }
inline esp_err_t rtc_gpio_init(gpio_num_t pin) { testRtcInitialized = pin == 2; return ESP_OK; }
inline esp_err_t rtc_gpio_deinit(gpio_num_t) { testRtcInitialized = false; return ESP_OK; }
inline esp_err_t rtc_gpio_set_direction(gpio_num_t, int mode) { testRtcDirection = mode; return ESP_OK; }
inline esp_err_t rtc_gpio_pullup_en(gpio_num_t) { testRtcPullup = true; return ESP_OK; }
inline esp_err_t rtc_gpio_pulldown_dis(gpio_num_t) { testRtcPulldown = false; return ESP_OK; }
inline uint32_t rtc_gpio_get_level(gpio_num_t pin) { return digitalRead(pin); }
inline esp_err_t esp_sleep_enable_ext0_wakeup(gpio_num_t pin, int level) {
  testWakePin = pin; testWakeLevel = level; return ESP_OK;
}
inline void esp_deep_sleep_start() { ++testDeepSleepCalls; testSleepEvents.push_back("deep_sleep"); }
constexpr int ESP_GATT_PERM_READ = 1, ESP_GATT_PERM_WRITE = 2;
constexpr int BLE_ADDR_TYPE_RANDOM = 1, BLE_ADDR_TYPE_PUBLIC = 0, ADV_TYPE_IND = 0;
constexpr int ADV_CHNL_ALL = 7, ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY = 0;
constexpr int ESP_BLE_ADV_FLAG_GEN_DISC = 2, ESP_BLE_ADV_FLAG_BREDR_NOT_SPT = 4;
enum esp_ble_power_type_t { ESP_BLE_PWR_TYPE_DEFAULT, ESP_BLE_PWR_TYPE_ADV,
                            ESP_BLE_PWR_TYPE_CONN_HDL0 };
constexpr int ESP_PWR_LVL_N0 = 0;
static std::map<int, int> testTxPower;
inline esp_err_t esp_ble_tx_power_set(esp_ble_power_type_t type, int level) {
  testTxPower[int(type)] = level; return ESP_OK;
}
inline int esp_ble_tx_power_get(esp_ble_power_type_t type) {
  const auto found = testTxPower.find(int(type));
  return found == testTxPower.end() ? ESP_FAIL : found->second;
}
struct esp_ble_adv_params_t { int adv_int_min, adv_int_max, adv_type, own_addr_type, peer_addr_type; uint8_t peer_addr[6]; int channel_map, adv_filter_policy; };
struct esp_ble_gatts_cb_param_t {
  struct { uint16_t conn_id = 0; } read;
  struct { uint16_t conn_id = 0; bool is_prep = false; uint16_t offset = 0; } write;
  struct { uint16_t conn_id = 0; } connect, disconnect;
};
enum esp_gatts_cb_event_t { ESP_GATTS_REG_EVT };
enum esp_gap_ble_cb_event_t { ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT, ESP_GAP_BLE_SET_STATIC_RAND_ADDR_EVT,
                              ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT,
                              ESP_GAP_BLE_ADV_START_COMPLETE_EVT, ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT };
struct esp_ble_gap_cb_param_t { struct { int status; } adv_data_raw_cmpl, set_rand_addr_cmpl,
                                scan_rsp_data_raw_cmpl, adv_start_cmpl, adv_stop_cmpl; };
static void (*testGapHandler)(esp_gap_ble_cb_event_t, esp_ble_gap_cb_param_t *) = nullptr;
static std::string testDeviceName;
static std::vector<uint8_t> testScanResponse;
static bool testFailScanQueue = false, testFailScanComplete = false, testFailSetName = false;
static unsigned testFailScanCount = 0, testFailSetNameCount = 0;
static bool testHoldScanComplete = false;
static unsigned testAdvertisingStarts = 0, testAdvertisingStops = 0;
static std::vector<int> testAdvertisingIntervals;
static bool testHoldAdvertisingComplete = false;
static bool testFailAdvertisingStart = false, testFailAdvertisingStop = false;
struct esp_ble_auth_cmpl_t {};
struct esp_ble_bond_dev_t { esp_bd_addr_t bd_addr; };
inline int esp_ble_get_bond_device_num() { return 0; }
inline int esp_ble_get_bond_device_list(int *, esp_ble_bond_dev_t *) { return ESP_OK; }
inline int esp_ble_remove_bond_device(uint8_t *) { return ESP_OK; }
inline int esp_ble_gap_set_rand_addr(uint8_t *) {
  if (testGapHandler) {
    esp_ble_gap_cb_param_t param = {}; param.set_rand_addr_cmpl.status = ESP_BT_STATUS_SUCCESS;
    testGapHandler(ESP_GAP_BLE_SET_STATIC_RAND_ADDR_EVT, &param);
  }
  return ESP_OK;
}
inline int esp_ble_gap_config_scan_rsp_data_raw(uint8_t *value, uint32_t length) {
  if (testFailScanQueue || (testFailScanCount && testFailScanCount--)) return ESP_FAIL;
  if (!testFailScanComplete) testScanResponse.assign(value, value + length);
  if (!testHoldScanComplete && testGapHandler) {
    esp_ble_gap_cb_param_t param = {}; param.scan_rsp_data_raw_cmpl.status = testFailScanComplete ? ESP_FAIL : ESP_BT_STATUS_SUCCESS;
    testGapHandler(ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT, &param);
  }
  return ESP_OK;
}
inline int esp_ble_gap_set_device_name(const char *value) {
  if (testFailSetName || (testFailSetNameCount && testFailSetNameCount--)) return ESP_FAIL;
  testDeviceName = value; return ESP_OK;
}
inline int esp_ble_gap_start_advertising(esp_ble_adv_params_t *params) {
  ++testAdvertisingStarts; testAdvertisingIntervals.push_back(params->adv_int_min);
  if (testFailAdvertisingStart) return ESP_FAIL;
  if (!testHoldAdvertisingComplete && testGapHandler) {
    esp_ble_gap_cb_param_t param = {};
    param.adv_start_cmpl.status = ESP_BT_STATUS_SUCCESS;
    testGapHandler(ESP_GAP_BLE_ADV_START_COMPLETE_EVT, &param);
  }
  return ESP_OK;
}
inline int esp_ble_gap_stop_advertising() {
  ++testAdvertisingStops;
  if (testFailAdvertisingStop) return ESP_FAIL;
  if (!testHoldAdvertisingComplete && testGapHandler) {
    esp_ble_gap_cb_param_t param = {};
    param.adv_stop_cmpl.status = ESP_BT_STATUS_SUCCESS;
    testGapHandler(ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT, &param);
  }
  return ESP_OK;
}
inline int esp_ble_gatts_send_indicate(esp_gatt_if_t, uint16_t, uint16_t, size_t, uint8_t *, bool) { return ESP_OK; }
class BLECharacteristic;
class BLECharacteristicCallbacks {
 public:
  virtual void onRead(BLECharacteristic *, esp_ble_gatts_cb_param_t *) {}
  virtual void onWrite(BLECharacteristic *, esp_ble_gatts_cb_param_t *) {}
  virtual ~BLECharacteristicCallbacks() = default;
};
class BLE2902 {
 public:
  void setAccessPermissions(int) {}
  bool getNotifications() { return false; }
};
class BLECharacteristic {
 public:
  enum { PROPERTY_READ = 1, PROPERTY_WRITE = 2, PROPERTY_NOTIFY = 4 };
  std::string value;
  std::unique_ptr<BLECharacteristicCallbacks> callback;
  std::unique_ptr<BLE2902> descriptor;
  void setValue(const std::string &v) { value = v; }
  std::string getValue() { return value; }
  void setAccessPermissions(int) {}
  void setCallbacks(BLECharacteristicCallbacks *value) { callback.reset(value); }
  void addDescriptor(BLE2902 *value) { descriptor.reset(value); }
  uint16_t getHandle() { return 1; }
};
class BLEService {
 public:
  BLECharacteristic *createCharacteristic(const char *, int) { static BLECharacteristic result; return &result; }
  void start() {}
};
class BLEServer;
class BLEServerCallbacks {
 public:
  virtual void onConnect(BLEServer *, esp_ble_gatts_cb_param_t *) {}
  virtual void onDisconnect(BLEServer *, esp_ble_gatts_cb_param_t *) {}
  virtual ~BLEServerCallbacks() = default;
};
class BLEServer {
 public:
  bool closed = false;
  std::unique_ptr<BLEServerCallbacks> callback;
  void disconnect(uint16_t) { closed = true; }
  void setCallbacks(BLEServerCallbacks *value) { callback.reset(value); }
  BLEService *createService(const char *) { static BLEService result; return &result; }
};
class BLESecurityCallbacks {
 public:
  virtual uint32_t onPassKeyRequest() = 0;
  virtual void onPassKeyNotify(uint32_t) = 0;
  virtual bool onSecurityRequest() = 0;
  virtual bool onConfirmPIN(uint32_t) = 0;
  virtual void onAuthenticationComplete(esp_ble_auth_cmpl_t) = 0;
  virtual ~BLESecurityCallbacks() = default;
};
class BLEUUID { public: explicit BLEUUID(const char *) {} };
class BLEAdvertisementData { public: void setFlags(int) {} void setCompleteServices(BLEUUID) {} };
class BLEAdvertising {
 public:
  void setAdvertisementData(BLEAdvertisementData &) {
    if (testGapHandler) {
      esp_ble_gap_cb_param_t param = {}; param.adv_data_raw_cmpl.status = ESP_BT_STATUS_SUCCESS;
      testGapHandler(ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT, &param);
    }
  }
  void setScanResponse(bool) {}
};
class BLEDevice {
 public:
  static void init(const char *name) { testDeviceName = name; }
  static void setMTU(int) {}
  static void setSecurityCallbacks(BLESecurityCallbacks *value) {
    static std::unique_ptr<BLESecurityCallbacks> owner;
    owner.reset(value);
  }
  static void setCustomGapHandler(void (*handler)(esp_gap_ble_cb_event_t, esp_ble_gap_cb_param_t *)) { testGapHandler = handler; }
  static void setCustomGattsHandler(void (*)(esp_gatts_cb_event_t, esp_gatt_if_t, esp_ble_gatts_cb_param_t *)) {}
  static BLEServer *createServer() { static BLEServer result; return &result; }
  static BLEAdvertising *getAdvertising() { static BLEAdvertising result; return &result; }
};


// Successful light-sleep evidence is distinct from the PM lock mode.
using esp_sleep_wakeup_cause_t = int;
constexpr int ESP_SLEEP_WAKEUP_UNDEFINED = 0, ESP_SLEEP_WAKEUP_TIMER = 4;
constexpr int ESP_RST_POWERON = 1, ESP_RST_DEEPSLEEP = 8;
static int testSleepWakeCause = ESP_SLEEP_WAKEUP_UNDEFINED, testResetReason = ESP_RST_POWERON;
inline esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() { return testSleepWakeCause; }
inline int esp_reset_reason() { return testResetReason; }
