#include "sdkconfig.h"
#include "esp_idf_version.h"
#include "esp_arduino_version.h"
#include "esp_bt.h"

#if ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(4, 4, 5)
#error This build requires the pinned ESP-IDF 4.4.5 SDK
#endif
#if ESP_ARDUINO_VERSION != ESP_ARDUINO_VERSION_VAL(2, 0, 10)
#error This build requires the pinned Arduino 2.0.10 component
#endif
#if !CONFIG_PM_ENABLE || !CONFIG_FREERTOS_USE_TICKLESS_IDLE || !CONFIG_BT_CTRL_MODEM_SLEEP || !CONFIG_BT_CTRL_MAIN_XTAL_PU_DURING_LIGHT_SLEEP
#error Rebuild the SDK with Bluetooth modem sleep and automatic light sleep enabled
#endif
#if !CONFIG_BT_BLE_42_FEATURES_SUPPORTED
#error Arduino 2.0.10 requires the legacy BLE advertising APIs
#endif
#if CONFIG_PM_SLP_DISABLE_GPIO
#error Inventory Counter requires its board GPIO configuration to remain active during light sleep
#endif
#if CONFIG_ESP_SLEEP_GPIO_RESET_WORKAROUND
#error The GPIO reset workaround must not silently enable GPIO isolation
#endif
#if CONFIG_ARDUINO_ISR_IRAM
#error The Menu ISR uses flash-resident helpers and must pause during flash operations
#endif
static_assert(CONFIG_BT_CTRL_SLEEP_MODE_EFF == ESP_BT_SLEEP_MODE_1, "Expected modem sleep mode 1");
static_assert(CONFIG_BT_CTRL_SLEEP_CLOCK_EFF == ESP_BT_SLEEP_CLOCK_MAIN_XTAL, "Expected main crystal clock");
