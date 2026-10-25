#include <Arduino.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include <esp_system.h>
#include <esp_bt.h>
#include <mbedtls/sha256.h>
#include <mbedtls/md.h>
#include <cJSON.h>
#include "panel.h"
#include "ble_protocol.h"
#include "ble_auth.h"
#include "qr_geometry.h"
#include "find_tag.h"
#include "ble_json.h"
#include "ble_connection.h"
#include "ble_name.h"
#include "provisioning_crypto.h"
#include "provisioning_authority.h"
#include "power_policy.h"
#include "power_runtime.h"
#include "light_sleep_wake.h"
#include "sleep_policy.h"
#include "sleep_runtime.h"
#include "menu_button.h"

using namespace inventory_counter;
constexpr char kVersion[] = "3.7.0";
constexpr uint32_t kRecordMagic = 0x43574232;
constexpr uint32_t kNameMagic = 0x43574e31;
constexpr uint32_t kQrMagic = 0x43575131;
constexpr uint32_t kPairWindowMs = power_policy::kPairWindowMs;
constexpr uint32_t kTransferTimeoutMs = power_policy::kTransferTimeoutMs;
constexpr uint32_t kIdleDisconnectMs = power_policy::kIdleDisconnectMs;
constexpr char kService[] = "8f7c0001-4a9b-4f3d-9c21-6b8e2d5a1000";
constexpr char kIdentity[] = "8f7c0002-4a9b-4f3d-9c21-6b8e2d5a1000";
constexpr char kControl[] = "8f7c0003-4a9b-4f3d-9c21-6b8e2d5a1000";
constexpr char kData[] = "8f7c0004-4a9b-4f3d-9c21-6b8e2d5a1000";
constexpr char kStatus[] = "8f7c0005-4a9b-4f3d-9c21-6b8e2d5a1000";
constexpr char kSetup[] = "8f7c0006-4a9b-4f3d-9c21-6b8e2d5a1000";
int provisioningRandom(void *, unsigned char *out, size_t size) {
  if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED) return -1;
  esp_fill_random(out, size); return 0;
}
ProvisionCrypto provisionCrypto(provisioningRandom);
struct SetupPublic { char nonce[33], publicKey[131], signature[129]; } setupPublic = {};
struct Pairing { uint32_t magic; char box[37], generation[17]; uint8_t key[32]; };
struct SavedFrame {
  uint32_t magic, revision;
  char generation[17], profile[16], box[37], sha256[65];
  uint8_t pixels[kFrameBytes];
};
// Separate sidecar retains the exact legacy SavedFrame layout. A sidecar is
// trusted only for the active, validated frame with the same full binding.
struct SavedName {
  uint32_t magic, revision;
  char generation[17], profile[16], box[37], frameHash[65];
  uint8_t namedReceipt;
  char value[kMaxBleNameBytes + 1];
};
struct SavedQr {
  uint32_t magic, revision;
  char generation[17], profile[16], box[37], frameHash[65];
  QrBounds bounds;
};
Preferences preferences;
Pairing pairing = {};
SavedFrame lastFrame = {}, candidate = {};
SavedName lastName = {}, candidateName = {};
SavedQr lastQr = {}, candidateQr = {};
AwakeWindow awakeWindow;
bool qrVisibleRequested = true, qrVisibleOnPanel = false;
uint32_t qrRetryAt = 0;
bool qrRetryPending = false;
bool findActive = false, findOnPanel = false, findTimerArmed = false;
uint32_t findStarted = 0;
uint32_t findRemaining() {
  return findActive && findTimerArmed ? power_policy::remaining(millis(), findStarted, kFindDurationMs) : 0;
}
void cancelFind() { findActive = findTimerArmed = false; }
portMUX_TYPE menuEdgeMux = portMUX_INITIALIZER_UNLOCKED;
MenuButton menuButton;
char appliedBleName[kMaxBleNameBytes + 1] = "E-ink display";
ControlMessage control;
UsbCommand usbCommand;
char pendingControl[kControlBytes + 1], deviceId[32], challenge[33], errorText[128] = {};
uint8_t screen[kFrameBytes];
size_t receivedBytes = 0;
uint8_t activeSlot = 0;
uint16_t connectionId = 0;
esp_bd_addr_t advertisingAddress = {};
esp_ble_adv_params_t advertisingParameters = {};
bool storageReady = false, hasFrame = false, connected = false;
bool disconnectRequested = false;
bool connectionAuthorized = false;
bool pairingOpen = false, menuDown = false, menuLatched = false, commandPending = false;
bool receiving = false, busy = false, renderingCandidate = false, screenIsLabel = false;
bool notifyPending = false, uiRendering = false;
bool showPairScreen = false, restoreScreen = false;
uint32_t readStackWatermark = UINT32_MAX;
bool readStackReportPending = false;
uint32_t pairingStarted = 0, menuStarted = 0, transferTouched = 0, connectionTouched = 0;
uint32_t connectionStarted = 0;
bool connectionLogPending = false;
const char *statusState = "ready";
SemaphoreHandle_t stateMutex;
BLEServer *bleServer;
BLECharacteristic *statusCharacteristic;
BLE2902 *statusSubscription;
esp_gatt_if_t gattInterface = ESP_GATT_IF_NONE;
volatile bool advertisingDataReady = false, advertisingAddressReady = false;
volatile int scanResponseState = 0;
bool advertisingReady = false;
bool radioRecoveryPending = false;
enum class AdvertisingPhase : uint8_t { Stopped, Starting, Active, Stopping };
volatile AdvertisingPhase advertisingPhase = AdvertisingPhase::Stopped;
volatile bool advertisingTransitionFailed = false;
bool advertisingFast = true;
uint32_t advertisingRetryAt = 0;
bool advertisingRetryPending = false;
power_policy::FastWindow fastAdvertisingWindow;
TaskHandle_t powerLoopTaskHandle = nullptr;
bool usbRecentlyActive = false;
bool normalBootForSleepObservation = false, lightSleepObserved = false;
esp_sleep_wakeup_cause_t lastObservedLightSleepWake = ESP_SLEEP_WAKEUP_UNDEFINED;
uint32_t usbActiveAt = 0;

void wakeLoop() {
  if (powerLoopTaskHandle) xTaskNotifyGive(powerLoopTaskHandle);
}

using FastCpu = inventory_counter_power::ScopedFastCpu;
bool renderPanel(const uint8_t *pixels) {
  FastCpu boost;
  return panel::render(pixels);
}

struct Lock {
  Lock() { xSemaphoreTake(stateMutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(stateMutex); }
};
void IRAM_ATTR menuPressInterrupt() {
  portENTER_CRITICAL_ISR(&menuEdgeMux);
  menuButton.sample(millis(), digitalRead(panel::kMenuPin) == LOW);
  portEXIT_CRITICAL_ISR(&menuEdgeMux);
  inventory_counter_power::menuLightSleepInterrupt();
}
// Called with stateMutex held; QR privacy never disables authenticated BLE work.
void updateQrVisibility() {
  portENTER_CRITICAL(&menuEdgeMux);
  menuButton.sample(millis(), digitalRead(panel::kMenuPin) == LOW);
  const bool toggle = menuButton.shortPress;
  menuButton.shortPress = false;
  menuDown = menuButton.held; menuStarted = menuButton.pressedAt; menuLatched = menuButton.longReported;
  portEXIT_CRITICAL(&menuEdgeMux);
  if (toggle) {
    qrVisibleRequested = !qrVisibleRequested;
    qrRetryPending = false;
    if (qrVisibleRequested) awakeWindow.begin(millis());
    else awakeWindow.armed = false;
  }
  if (findActive && findTimerArmed && !findRemaining()) cancelFind();
  if (awakeWindow.expired(millis())) {
    qrVisibleRequested = false;
    awakeWindow.armed = false;
  }
}
// Recheck after a bounded refresh so a Menu press or timeout during it is
// corrected before a committed receipt can be issued.
bool storedQrValid(const SavedQr &metadata, const SavedFrame &frame);
bool renderSavedLabel(const SavedFrame &frame, const SavedQr &metadata, bool allowFind = true) {
  for (unsigned attempt = 0; attempt < 2; ++attempt) {
    bool visible, highlight;
    { Lock lock; updateQrVisibility(); visible = qrVisibleRequested; highlight = allowFind && findActive; }
    const bool trustedQr = storedQrValid(metadata, frame);
    const uint8_t *pixels = frame.pixels;
    if (!visible) {
      if (trustedQr) hideQr(frame.pixels, screen, metadata.bounds);
      else memset(screen, 0, sizeof(screen));
      pixels = screen;
    }
    if (highlight) {
      if (!trustedQr) return false;
      invertOutsideQr(pixels, screen, metadata.bounds); pixels = screen;
    }
    if (!renderPanel(pixels)) return false;
    { Lock lock;
      qrVisibleOnPanel = visible; findOnPanel = highlight;
      // Give the first successful highlight a full 30 seconds. QR redraws do
      // not extend it; only a new authenticated Find command may do that.
      if (highlight && findActive && !findTimerArmed) { findStarted = millis(); findTimerArmed = true; }
      updateQrVisibility();
      if (visible == qrVisibleRequested && highlight == (allowFind && findActive)) {
        qrRetryPending = false; return true;
      }
    }
  }
  return false;
}
bool qrVisibilityCurrent() {
  Lock lock; updateQrVisibility();
  return qrVisibleOnPanel == qrVisibleRequested && findOnPanel == findActive;
}
bool paired() { return pairing.magic == kRecordMagic; }
const char *idleState() {
  return hasFrame ? (!screenIsLabel ? "ready" : (findActive || findOnPanel) ? "finding" : "committed")
      : paired() ? "paired" : "ready";
}
void randomHex(char *output, size_t bytes) {
  uint8_t value[32]; esp_fill_random(value, bytes);
  for (size_t i = 0; i < bytes; ++i) sprintf(output + 2 * i, "%02x", value[i]);
  output[bytes * 2] = 0;
}
void hashFrame(const uint8_t *pixels, char *output) {
  FastCpu boost;
  uint8_t digest[32]; mbedtls_sha256_ret(pixels, kFrameBytes, digest, 0);
  for (unsigned i = 0; i < sizeof(digest); ++i) sprintf(output + 2 * i, "%02x", digest[i]);
}
void hashName(const char *value, char *output) {
  uint8_t digest[32]; mbedtls_sha256_ret(reinterpret_cast<const uint8_t *>(value), strlen(value), digest, 0);
  for (unsigned i = 0; i < sizeof(digest); ++i) sprintf(output + 2 * i, "%02x", digest[i]);
}
void bindName(SavedName &name, const SavedFrame &frame, const char *value, bool named) {
  memset(&name, 0, sizeof(name)); name.magic = kNameMagic; name.revision = frame.revision;
  strcpy(name.generation, frame.generation); strcpy(name.profile, frame.profile);
  strcpy(name.box, frame.box); strcpy(name.frameHash, frame.sha256);
  name.namedReceipt = named ? 1 : 0; strcpy(name.value, value);
}
bool storedNameValid(const SavedName &name, const SavedFrame &frame) {
  return name.magic == kNameMagic && name.revision == frame.revision &&
      name.namedReceipt <= 1 && !memcmp(name.generation, frame.generation, sizeof(name.generation)) &&
      !memcmp(name.profile, frame.profile, sizeof(name.profile)) &&
      !memcmp(name.box, frame.box, sizeof(name.box)) &&
      !memcmp(name.frameHash, frame.sha256, sizeof(name.frameHash)) &&
      memchr(name.value, 0, sizeof(name.value)) && validBleName(name.value);
}
void bindQr(SavedQr &metadata, const SavedFrame &frame, const QrBounds &bounds) {
  memset(&metadata, 0, sizeof(metadata)); metadata.magic = kQrMagic; metadata.revision = frame.revision;
  strcpy(metadata.generation, frame.generation); strcpy(metadata.profile, frame.profile);
  strcpy(metadata.box, frame.box); strcpy(metadata.frameHash, frame.sha256); metadata.bounds = bounds;
}
bool storedQrValid(const SavedQr &metadata, const SavedFrame &frame) {
  return metadata.magic == kQrMagic && metadata.revision == frame.revision &&
      !memcmp(metadata.generation, frame.generation, sizeof(metadata.generation)) &&
      !memcmp(metadata.profile, frame.profile, sizeof(metadata.profile)) &&
      !memcmp(metadata.box, frame.box, sizeof(metadata.box)) &&
      !memcmp(metadata.frameHash, frame.sha256, sizeof(metadata.frameHash)) &&
      validQrBounds(metadata.bounds.x, metadata.bounds.y, metadata.bounds.size);
}
bool qrReady() { return hasFrame && storedQrValid(lastQr, lastFrame); }
bool sameSavedQr() {
  const bool previous = qrReady(), next = storedQrValid(candidateQr, candidate);
  return previous == next && (!next || sameQrBounds(lastQr.bounds, candidateQr.bounds));
}
void hmac(const char *message, char *output) {
  uint8_t digest[32];
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_hmac(info, pairing.key, sizeof(pairing.key),
                  reinterpret_cast<const uint8_t *>(message), strlen(message), digest);
  for (unsigned i = 0; i < sizeof(digest); ++i) sprintf(output + 2 * i, "%02x", digest[i]);
}
bool writeBlob(const char *key, const void *value, size_t length) {
  return storageReady && preferences.putBytes(key, value, length) == length;
}
bool clearInactiveSlot(uint8_t slot) {
  const char *frameKey = slot ? "frame1" : "frame0";
  const char *nameKey = slot ? "name1" : "name0";
  const char *qrKey = slot ? "qr1" : "qr0";
  return slot != activeSlot && storageReady &&
      (!preferences.getBytesLength(frameKey) || preferences.remove(frameKey)) &&
      (!preferences.getBytesLength(nameKey) || preferences.remove(nameKey)) &&
      (!preferences.getBytesLength(qrKey) || preferences.remove(qrKey));
}
bool storedFrameValid(const SavedFrame &frame) {
  if (frame.magic != kRecordMagic || !memchr(frame.profile, 0, sizeof(frame.profile)) ||
      !memchr(frame.box, 0, sizeof(frame.box)) || !memchr(frame.sha256, 0, sizeof(frame.sha256)) ||
      !memchr(frame.generation, 0, sizeof(frame.generation)) ||
      strcmp(frame.profile, panel::kProfile) || strcmp(frame.box, pairing.box) ||
      strcmp(frame.generation, pairing.generation) || !frame.revision || frame.revision > kMaxRevision ||
      !hex(frame.sha256, 64) || !paddingValid(frame.pixels)) return false;
  char computed[65]; hashFrame(frame.pixels, computed);
  return constantEqual(computed, frame.sha256);
}
void state(const char *value) {
  statusState = value; errorText[0] = 0; notifyPending = true;
  wakeLoop();
}
void fail(const char *message) {
  receiving = false; receivedBytes = 0; state("error");
  strlcpy(errorText, message, sizeof(errorText));
}
String serialized(cJSON *json) {
  char *text = cJSON_PrintUnformatted(json);
  String result = text ? text : "{\"state\":\"error\",\"error\":\"out of memory\"}";
  cJSON_free(text); cJSON_Delete(json); return result;
}
String identity() {
  static char nameHash[65]; hashName(appliedBleName, nameHash);
  cJSON *json = cJSON_CreateObject();
  addJsonUint32(json, "protocol", 2);
  cJSON_AddStringToObject(json, "device_id", deviceId);
  cJSON_AddStringToObject(json, "panel_profile", panel::kProfile);
  cJSON_AddStringToObject(json, "firmware_version", kVersion);
  cJSON_AddBoolToObject(json, "phone_pairing", false);
  cJSON_AddBoolToObject(json, "find_tag", true);
  addJsonUint32(json, "qr_sleep", qrReady() ? 2 : 1);
  cJSON_AddStringToObject(json, "provisioning", "ble-encrypted-v1");
  cJSON_AddBoolToObject(json, "paired", paired());
  cJSON_AddStringToObject(json, "box_id", paired() ? pairing.box : "");
  cJSON_AddStringToObject(json, "generation", paired() ? pairing.generation : "");
  cJSON_AddStringToObject(json, "challenge", challenge);
  addJsonUint32(json, "revision", hasFrame ? lastFrame.revision : 0);
  cJSON_AddStringToObject(json, "sha256", hasFrame ? lastFrame.sha256 : "");
  cJSON_AddStringToObject(json, "ble_name_sha256", nameHash);
  return serialized(json);
}
String setupIdentity() {
  cJSON *json = cJSON_CreateObject();
  addJsonUint32(json, "protocol", 1);
  cJSON_AddStringToObject(json, "device_id", deviceId);
  cJSON_AddStringToObject(json, "panel_profile", panel::kProfile);
  cJSON_AddStringToObject(json, "authority_key_id", provisionCrypto.authorityId);
  const bool open = pairingOpen && millis() - pairingStarted < kPairWindowMs && setupPublic.nonce[0];
  cJSON_AddBoolToObject(json, "setup_open", open);
  if (open) {
    cJSON_AddStringToObject(json, "setup_nonce", setupPublic.nonce);
    cJSON_AddStringToObject(json, "device_public_key", setupPublic.publicKey);
    cJSON_AddStringToObject(json, "device_signature", setupPublic.signature);
  }
  return serialized(json);
}
String status() {
  // ReadCallbacks holds stateMutex; keep these buffers off the small BTC stack.
  static char signature[65], transcript[kTranscriptBytes], nameHash[65];
  cJSON *json = cJSON_CreateObject();
  cJSON_AddStringToObject(json, "state", statusState);
  cJSON_AddStringToObject(json, "device_id", deviceId);
  cJSON_AddStringToObject(json, "panel_profile", panel::kProfile);
  cJSON_AddStringToObject(json, "box_id", paired() ? pairing.box : "");
  cJSON_AddStringToObject(json, "generation", paired() ? pairing.generation : "");
  const SavedFrame &frame = (receiving || renderingCandidate) ? candidate : lastFrame;
  addJsonUint32(json, "revision", (receiving || renderingCandidate || hasFrame) ? frame.revision : 0);
  cJSON_AddStringToObject(json, "sha256", (receiving || renderingCandidate || hasFrame) ? frame.sha256 : "");
  addJsonUint32(json, "received_bytes", receivedBytes);
  if (findActive || findOnPanel) addJsonUint32(json, "find_remaining_ms", findRemaining());
  if (errorText[0]) cJSON_AddStringToObject(json, "error", errorText);
  if (paired() && !strcmp(statusState, "paired")) {
    pairedTranscript(transcript, sizeof(transcript), deviceId, pairing.generation, pairing.box);
    hmac(transcript, signature);
    cJSON_AddStringToObject(json, "signature", signature);
  } else if (hasFrame && screenIsLabel && !findActive && !findOnPanel && !strcmp(statusState, "committed")) {
    const char *receiptHash = nullptr;
    const bool named = storedNameValid(lastName, lastFrame) && lastName.namedReceipt;
    const bool radioReady = advertisingReady && scanResponseState == 1 &&
        !strcmp(appliedBleName, lastName.value);
    if (named && radioReady) {
      hashName(lastName.value, nameHash); receiptHash = nameHash;
      cJSON_AddStringToObject(json, "ble_name_sha256", nameHash);
    }
    if ((!named || radioReady) && committedTranscript(transcript, sizeof(transcript), deviceId, pairing.generation, pairing.box,
                            panel::kProfile, lastFrame.revision, lastFrame.sha256, receiptHash)) {
      hmac(transcript, signature);
      cJSON_AddStringToObject(json, "signature", signature);
    }
  }
  return serialized(json);
}
const char *jsonString(cJSON *json, const char *name) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(json, name);
  return cJSON_IsString(item) ? item->valuestring : nullptr;
}
bool provisionUsb(cJSON *json) {
  const char *box = jsonString(json, "box_id"), *key = jsonString(json, "key");
  const char *generation = jsonString(json, "generation");
  Pairing replacement = {};
  {
    Lock lock;
    if (!pairingOpen || millis() - pairingStarted >= kPairWindowMs || connected || busy) {
      return false;
    }
    if (cJSON_GetArraySize(json) != 4 || !uuid(box) || !hex(key, 64) || !hex(generation, 16)) {
      return false;
    }
    replacement.magic = kRecordMagic; strcpy(replacement.box, box); strcpy(replacement.generation, generation);
    decodeBleKey(key, replacement.key);
    busy = true; state("busy");
  }
  const bool saved = writeBlob("pair", &replacement, sizeof(replacement));
  {
    Lock lock; busy = false;
    if (!saved) { fail("Could not save provisioning"); return false; }
    const bool same = !memcmp(&pairing, &replacement, sizeof(pairing));
    pairing = replacement;
    if (!same) { hasFrame = false; memset(&lastFrame, 0, sizeof(lastFrame)); }
    pairingOpen = false; receiving = false; receivedBytes = 0;
    randomHex(challenge, 16); state("paired"); showPairScreen = true;
  }
  memset(&replacement, 0, sizeof(replacement));
  return true;
}
void provisionBle(cJSON *json) {
  const ProvisionEnvelope envelope = { jsonString(json, "authority_key_id"), jsonString(json, "box_id"),
      jsonString(json, "generation"), jsonString(json, "setup_nonce"), jsonString(json, "server_public_key"),
      jsonString(json, "iv"), jsonString(json, "ciphertext"), jsonString(json, "tag"), jsonString(json, "signature") };
  uint16_t session;
  {
    Lock lock;
    if (cJSON_GetArraySize(json) != 10 || !connected || disconnectRequested || busy || receiving ||
        !pairingOpen || millis() - pairingStarted >= kPairWindowMs || !setupPublic.nonce[0]) {
      fail("Encrypted setup rejected; hold MENU five seconds"); return;
    }
    session = connectionId; busy = true; state("busy");
  }
  Pairing replacement = {};
  bool accepted;
  {
    FastCpu boost;
    accepted = provisionCrypto.decrypt(envelope, deviceId, panel::kProfile, replacement.key);
  }
  if (accepted) {
    replacement.magic = kRecordMagic;
    strcpy(replacement.box, envelope.box); strcpy(replacement.generation, envelope.generation);
  }
  {
    Lock lock;
    accepted = accepted && connected && !disconnectRequested && connectionId == session && pairingOpen &&
        millis() - pairingStarted < kPairWindowMs && (!paired() || !memcmp(&pairing, &replacement, sizeof(pairing)));
    if (accepted) {
      // Consume the signed setup capability before the NVS write. A failed write
      // requires another physical MENU hold; no replay can mutate the binding.
      pairingOpen = false; memset(&setupPublic, 0, sizeof(setupPublic));
    }
  }
  if (accepted) provisionCrypto.close();
  const bool saved = accepted && writeBlob("pair", &replacement, sizeof(replacement));
  {
    Lock lock; busy = false;
    if (saved) {
      const bool same = paired() && !memcmp(&pairing, &replacement, sizeof(pairing));
      pairing = replacement;
      if (!same) { hasFrame = false; memset(&lastFrame, 0, sizeof(lastFrame)); }
      receiving = false; receivedBytes = 0; connectionAuthorized = true;
      randomHex(challenge, 16); state("paired");
    } else fail("Encrypted setup rejected; hold MENU five seconds");
  }
  mbedtls_platform_zeroize(&replacement, sizeof(replacement));
}
void acceptBegin(uint32_t next, const char *digest, const char *newName, const QrBounds *qr) {
  // Called with stateMutex held after authenticating the signed metadata.
  randomHex(challenge, 16);
  connectionAuthorized = true;
  memset(&candidate, 0, sizeof(candidate)); candidate.magic = kRecordMagic; candidate.revision = next;
  strcpy(candidate.box, pairing.box); strcpy(candidate.generation, pairing.generation);
  strcpy(candidate.profile, panel::kProfile); strcpy(candidate.sha256, digest);
  bindName(candidateName, candidate, newName ? newName : appliedBleName, newName != nullptr);
  memset(&candidateQr, 0, sizeof(candidateQr));
  if (qr) bindQr(candidateQr, candidate, *qr);
  receivedBytes = 0; receiving = true; transferTouched = millis(); state("receiving");
}
bool jsonUint32(cJSON *json, const char *key, uint32_t maximum, uint32_t &value) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
  if (!cJSON_IsNumber(item) || !(item->valuedouble >= 0 && item->valuedouble <= maximum)) return false;
  value = uint32_t(item->valuedouble);
  return item->valuedouble == value;
}
void beginCommand(cJSON *json) {
  const char *box = jsonString(json, "box_id"), *generation = jsonString(json, "generation");
  const char *profile = jsonString(json, "panel_profile");
  const char *digest = jsonString(json, "sha256"), *nonce = jsonString(json, "challenge");
  const char *signature = jsonString(json, "signature");
  const char *newName = jsonString(json, "ble_name");
  cJSON *revision = cJSON_GetObjectItemCaseSensitive(json, "revision");
  const bool withQr = cJSON_GetObjectItemCaseSensitive(json, "qr_x") ||
      cJSON_GetObjectItemCaseSensitive(json, "qr_y") || cJSON_GetObjectItemCaseSensitive(json, "qr_size");
  QrBounds qr = {}; uint32_t x = 0, y = 0, size = 0;
  uint32_t next;
  {
    Lock lock;
    if (radioRecoveryPending || !connected || disconnectRequested || !paired() ||
        cJSON_GetArraySize(json) != (newName ? 9 : 8) + (withQr ? 3 : 0) ||
        (withQr && (!jsonUint32(json, "qr_x", kWidth - 1, x) ||
         !jsonUint32(json, "qr_y", kHeight - 1, y) ||
         !jsonUint32(json, "qr_size", kHeight, size) || !validQrBounds(x, y, size))) ||
        (newName && !validBleName(newName)) || !uuid(box) || strcmp(box, pairing.box) ||
        !profile || strcmp(profile, panel::kProfile) ||
        !hex(generation, 16) || strcmp(generation, pairing.generation) || !hex(digest, 64) ||
        !hex(nonce, 32) || !constantEqual(nonce, challenge) || !hex(signature, 64) ||
        !cJSON_IsNumber(revision) || revision->valuedouble < 1 || revision->valuedouble > kMaxRevision ||
        revision->valuedouble != uint32_t(revision->valuedouble)) {
      fail("Invalid frame metadata or challenge"); return;
    }
    next = uint32_t(revision->valuedouble);
    qr = {uint16_t(x), uint16_t(y), uint16_t(size)};
    char expected[65], transcript[kTranscriptBytes], nameHash[65];
    if (newName) hashName(newName, nameHash);
    if (!beginTranscript(transcript, sizeof(transcript), deviceId, pairing.generation, pairing.box,
                         panel::kProfile, next, digest, nonce, newName ? nameHash : nullptr, withQr ? &qr : nullptr)) {
      fail("Frame authorization transcript overflow"); return;
    }
    hmac(transcript, expected);
    if (!constantEqual(expected, signature)) { fail("Frame authorization failed"); return; }
    if (revisionDecision(hasFrame, lastFrame.revision, lastFrame.sha256, next, digest) == RevisionDecision::Conflict) {
      fail("Stale or conflicting revision"); return;
    }
    acceptBegin(next, digest, newName, withQr ? &qr : nullptr);
  }
}
// The raw scan response contains only the local name AD structure. Keep its
// bytes alive until Bluedroid confirms the asynchronous configuration.
uint8_t scanResponse[31] = {};
bool configureBleName(const char *value) {
  const size_t length = scanResponseName(value, scanResponse);
  if (!length) return false;
  scanResponseState = 0;
  if (esp_ble_gap_config_scan_rsp_data_raw(scanResponse, length) != ESP_OK) {
    scanResponseState = -1; return false;
  }
  const uint32_t started = millis();
  while (!scanResponseState && millis() - started < 2000) delay(5);
  if (!scanResponseState) {
    // A late completion might still install the tentative scan response.
    // Keep advertising disabled until loop() restores the committed name.
    radioRecoveryPending = true; advertisingReady = false; return false;
  }
  return scanResponseState == 1 && esp_ble_gap_set_device_name(value) == ESP_OK;
}
void commitCommand(cJSON *json) {
  uint8_t nextSlot; bool needsRender, needsName;
  {
    Lock lock;
    if (cJSON_GetArraySize(json) != 1 || !receiving || receivedBytes != kFrameBytes) {
      fail("Incomplete frame"); return;
    }
    char computed[65]; hashFrame(candidate.pixels, computed);
    if (!constantEqual(computed, candidate.sha256) || !paddingValid(candidate.pixels)) {
      fail("Frame hash or padding mismatch"); return;
    }
    const RevisionDecision decision = revisionDecision(hasFrame, lastFrame.revision, lastFrame.sha256,
                                                       candidate.revision, candidate.sha256);
    if (decision == RevisionDecision::Conflict) { fail("Stale or conflicting revision"); return; }
    receiving = false;
    cancelFind();
    updateQrVisibility();
    if (decision == RevisionDecision::Duplicate && screenIsLabel && !findOnPanel && qrVisibleOnPanel == qrVisibleRequested &&
        storedNameValid(lastName, lastFrame) &&
        !strcmp(lastName.value, candidateName.value) &&
        lastName.namedReceipt == candidateName.namedReceipt && sameSavedQr() &&
        !strcmp(appliedBleName, candidateName.value)) {
      randomHex(challenge, 16); state("committed"); return;
    }
    busy = renderingCandidate = true; state("busy"); nextSlot = 1 - activeSlot;
    updateQrVisibility();
    needsRender = decision != RevisionDecision::Duplicate || !screenIsLabel || findOnPanel ||
        qrVisibleOnPanel != qrVisibleRequested || (!qrVisibleRequested && !sameSavedQr());
    needsName = strcmp(appliedBleName, candidateName.value) != 0;
  }
  // BLE callbacks remain responsive. The busy flag freezes candidate until the
  // physical panel refresh, radio update, and active-slot commit complete.
  const char *error = nullptr;
  bool renderAttempted = false;
  bool radioAttempted = false, visibleAfter = screenIsLabel;
  if (!clearInactiveSlot(nextSlot)) error = "Could not clear inactive frame slot";
  else if (!writeBlob(nextSlot ? "frame1" : "frame0", &candidate, sizeof(candidate))) error = "Could not stage frame";
  else if (!writeBlob(nextSlot ? "name1" : "name0", &candidateName, sizeof(candidateName)))
    error = "Could not stage BLE name";
  else if (storedQrValid(candidateQr, candidate) &&
           !writeBlob(nextSlot ? "qr1" : "qr0", &candidateQr, sizeof(candidateQr)))
    error = "Could not stage QR metadata";
  else if ((needsRender || !qrVisibilityCurrent()) && (renderAttempted = true) && !renderSavedLabel(candidate, candidateQr, false))
    error = "Display refresh timed out; previous cache retained";
  else if (needsName && (radioAttempted = true) && !configureBleName(candidateName.value))
    error = "Could not configure BLE name";
  else if (!qrVisibilityCurrent() && (renderAttempted = true) && !renderSavedLabel(candidate, candidateQr, false))
    error = "Could not apply QR visibility";
  else if (preferences.putUChar("active", nextSlot) != 1)
    error = "Could not commit frame";
  if (error) {
    // Configuration can have changed the scan response before a later step
    // fails. Never advertise again until the previously committed name is back.
    if (radioAttempted && !radioRecoveryPending) {
      if (!configureBleName(appliedBleName)) advertisingReady = false;
    }
    if (renderAttempted)
      visibleAfter = hasFrame && renderSavedLabel(lastFrame, lastQr);
  }
  {
    Lock lock; busy = renderingCandidate = false; screenIsLabel = visibleAfter;
    if (error) { fail(error); return; }
    activeSlot = nextSlot; lastFrame = candidate; hasFrame = true;
    lastName = candidateName; lastQr = candidateQr; strcpy(appliedBleName, candidateName.value);
    updateQrVisibility(); screenIsLabel = qrVisibleOnPanel == qrVisibleRequested && !findOnPanel;
    pairingOpen = false; randomHex(challenge, 16); state(screenIsLabel ? "committed" : "ready");
  }
}
void findCommand(cJSON *json) {
  uint32_t revision = 0, duration = 0, x = 0, y = 0, size = 0;
  const char *box = jsonString(json, "box_id"), *generation = jsonString(json, "generation");
  const char *profile = jsonString(json, "panel_profile"), *digest = jsonString(json, "sha256");
  const char *nonce = jsonString(json, "challenge"), *signature = jsonString(json, "signature");
  {
    Lock lock; updateQrVisibility();
    if (cJSON_GetArraySize(json) != 12 || !connected || disconnectRequested || radioRecoveryPending ||
        !paired() || !qrReady() || busy || receiving || pairingOpen || !screenIsLabel || qrRetryPending ||
        !uuid(box) || strcmp(box, pairing.box) || !hex(generation, 16) || strcmp(generation, pairing.generation) ||
        !profile || strcmp(profile, panel::kProfile) || !hex(digest, 64) || !constantEqual(digest, lastFrame.sha256) ||
        !hex(nonce, 32) || !constantEqual(nonce, challenge) || !hex(signature, 64) ||
        !jsonUint32(json, "revision", kMaxRevision, revision) || revision != lastFrame.revision ||
        !jsonUint32(json, "duration_ms", kFindDurationMs, duration) || duration != kFindDurationMs ||
        !jsonUint32(json, "qr_x", kWidth - 1, x) || !jsonUint32(json, "qr_y", kHeight - 1, y) ||
        !jsonUint32(json, "qr_size", kHeight, size) || !validQrBounds(x, y, size) ||
        x != lastQr.bounds.x || y != lastQr.bounds.y || size != lastQr.bounds.size) {
      fail("Invalid Find authorization or saved label"); return;
    }
    char expected[65], transcript[kTranscriptBytes];
    if (!findTranscript(transcript, sizeof(transcript), deviceId, pairing.generation, pairing.box,
                        panel::kProfile, revision, digest, nonce, lastQr.bounds)) {
      fail("Find authorization transcript overflow"); return;
    }
    hmac(transcript, expected);
    if (!constantEqual(expected, signature)) { fail("Find authorization failed"); return; }
    randomHex(challenge, 16); connectionAuthorized = true;
    const bool alreadyShown = findActive && findOnPanel && qrVisibleOnPanel == qrVisibleRequested;
    findActive = true; findTimerArmed = alreadyShown; findStarted = millis();
    if (alreadyShown) { state("finding"); return; }
    busy = uiRendering = true; screenIsLabel = false; state("busy");
  }
  const bool rendered = renderSavedLabel(lastFrame, lastQr);
  {
    Lock lock; busy = uiRendering = false; screenIsLabel = rendered;
    if (rendered) state(idleState());
    else {
      cancelFind(); qrRetryPending = true; qrRetryAt = millis();
      fail("Could not highlight the label; restoring");
    }
  }
}
void processCommand(char *message) {
  cJSON *json = flatCommandJson(message) ? cJSON_ParseWithOpts(message, nullptr, true) : nullptr;
  const char *op = jsonString(json, "op");
  if (!cJSON_IsObject(json) || !op) { Lock lock; fail("Invalid control JSON"); }
  else if (!strcmp(op, "pair")) { Lock lock; fail("Provision this display over USB"); }
  else if (!strcmp(op, "provision")) provisionBle(json);
  else if (!strcmp(op, "begin")) beginCommand(json);
  else if (!strcmp(op, "commit")) commitCommand(json);
  else if (!strcmp(op, "find")) findCommand(json);
  else { Lock lock; fail("Unknown control operation"); }
  cJSON_Delete(json);
}

void processUsbCommand(char *message) {
  cJSON *json = flatCommandJson(message) ? cJSON_ParseWithOpts(message, nullptr, true) : nullptr;
  const char *op = jsonString(json, "op");
  String payload;
  const char *field = nullptr;
  if (cJSON_IsObject(json) && op && !strcmp(op, "usb_identity") && cJSON_GetArraySize(json) == 1) {
    Lock lock; payload = identity(); field = "identity";
  } else if (cJSON_IsObject(json) && op && !strcmp(op, "usb_pm") && cJSON_GetArraySize(json) == 1) {
    const esp_err_t result = inventory_counter_power::dumpPmLocks(stdout);
    cJSON *report = cJSON_CreateObject();
    cJSON_AddNumberToObject(report, "result", result);
    payload = serialized(report); field = "pm";
  } else if (cJSON_IsObject(json) && op && !strcmp(op, "usb_power") && cJSON_GetArraySize(json) == 1) {
    cJSON *power = cJSON_CreateObject();
    addPowerRuntimeDiagnostics(power);
    const auto &wake = inventory_counter_power::lightSleepWakeState();
    cJSON_AddBoolToObject(power, "light_sleep_observed", lightSleepObserved);
    cJSON_AddNumberToObject(power, "last_observed_light_sleep_wake_cause", lastObservedLightSleepWake);
    cJSON_AddNumberToObject(power, "reset_reason", esp_reset_reason());
    cJSON_AddBoolToObject(power, "light_sleep_wake_ready", wake.ready);
    cJSON_AddNumberToObject(power, "wake_setup_result", wake.setup_result);
    cJSON_AddNumberToObject(power, "wake_arm_result", wake.arm_result);
    cJSON_AddNumberToObject(power, "wake_restore_result", wake.restore_result);
    cJSON_AddNumberToObject(power, "wake_isr_mask_result", wake.isr_mask_result);
    {
      Lock lock;
      cJSON_AddBoolToObject(power, "connected", connected);
      cJSON_AddBoolToObject(power, "pairing_open", pairingOpen);
      addJsonUint32(power, "fast_window_remaining_ms", fastAdvertisingWindow.left(millis()));
      addJsonUint32(power, "qr_remaining_ms", awakeWindow.left(millis()));
      cJSON_AddBoolToObject(power, "qr_visible", screenIsLabel && qrVisibleOnPanel);
      cJSON_AddBoolToObject(power, "qr_requested", qrVisibleRequested);
      cJSON_AddBoolToObject(power, "qr_ready", qrReady());
      cJSON_AddBoolToObject(power, "finding", findActive || findOnPanel);
      addJsonUint32(power, "find_remaining_ms", findRemaining());
      cJSON_AddBoolToObject(power, "ble_always_available", true);
    }
    const AdvertisingPhase phase = advertisingPhase;
    cJSON_AddBoolToObject(power, "advertising_active", phase == AdvertisingPhase::Active);
    cJSON_AddStringToObject(power, "advertising_mode", advertisingFast ? "fast" : "idle");
    addJsonUint32(power, "advertising_interval_ms", advertisingFast ? 20 : 500);
    cJSON_AddNumberToObject(power, "ble_tx_target_dbm", 0);
    cJSON_AddBoolToObject(power, "ble_tx_0dbm",
        esp_ble_tx_power_get(ESP_BLE_PWR_TYPE_DEFAULT) == ESP_PWR_LVL_N0 &&
        esp_ble_tx_power_get(ESP_BLE_PWR_TYPE_ADV) == ESP_PWR_LVL_N0);
    cJSON_AddStringToObject(power, "firmware_version", kVersion);
    payload = serialized(power); field = "power";
  } else if (cJSON_IsObject(json) && op && !strcmp(op, "usb_enrollment") && cJSON_GetArraySize(json) == 1 &&
             provisionCrypto.ready && provisionCrypto.claimPublic[0]) {
    cJSON *enrollment = cJSON_CreateObject();
    cJSON_AddStringToObject(enrollment, "device_id", deviceId);
    cJSON_AddStringToObject(enrollment, "panel_profile", panel::kProfile);
    cJSON_AddStringToObject(enrollment, "authority_key_id", provisionCrypto.authorityId);
    cJSON_AddStringToObject(enrollment, "claim_public_key", provisionCrypto.claimPublic);
    payload = serialized(enrollment); field = "enrollment";
  } else if (cJSON_IsObject(json) && op && !strcmp(op, "usb_pair") && provisionUsb(json)) {
    Lock lock; payload = status(); field = "receipt";
  }
  // Do not echo requests: provisioning input contains the permanent key.
  if (field) {
    Serial.print("CWUSB {\"ok\":true,\"device_id\":\""); Serial.print(deviceId);
    Serial.print("\",\""); Serial.print(field); Serial.print("\":"); Serial.print(payload); Serial.println("}");
  } else Serial.println("CWUSB {\"ok\":false,\"error\":\"USB command rejected; hold MENU five seconds for provisioning\"}");
  cJSON *key = cJSON_GetObjectItemCaseSensitive(json, "key");
  if (cJSON_IsString(key)) memset(key->valuestring, 0, strlen(key->valuestring));
  cJSON_Delete(json);
}

void readUsbCommands() {
  for (unsigned count = 0; count < 128 && Serial.available(); ++count) {
    usbRecentlyActive = true; usbActiveAt = millis();
    const auto result = usbCommand.push(uint8_t(Serial.read()));
    if (result == UsbCommand::Result::Complete) processUsbCommand(usbCommand.bytes + 6);
    else if (result == UsbCommand::Result::Invalid &&
             strspn(usbCommand.bytes, " \t\r") != usbCommand.used) Serial.println("CWUSB {\"ok\":false,\"error\":\"Invalid USB command\"}");
    if (result != UsbCommand::Result::More) usbCommand.reset();
  }
}

class ReadCallbacks : public BLECharacteristicCallbacks {
  unsigned kind;
 public:
  explicit ReadCallbacks(unsigned value) : kind(value) {}
  void onRead(BLECharacteristic *characteristic, esp_ble_gatts_cb_param_t *param) override {
    Lock lock;
    if (!connected || disconnectRequested || param->read.conn_id != connectionId) return;
    connectionTouched = millis();
    const String value = kind == 2 ? setupIdentity() : kind == 1 ? identity() : status();
    characteristic->setValue(value.c_str());
    // ESP-IDF reports bytes. Sample BTC_TASK here, but print from loopTask
    // so diagnostics do not add printf's stack usage to the read callback.
    const uint32_t remaining = uxTaskGetStackHighWaterMark(nullptr);
    if (remaining < readStackWatermark) {
      readStackWatermark = remaining; readStackReportPending = true;
    }
    wakeLoop();
  }
};
class ControlCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic, esp_ble_gatts_cb_param_t *param) override {
    Lock lock;
    if (!connected || disconnectRequested || param->write.conn_id != connectionId ||
        (busy && !uiRendering)) return;
    connectionTouched = millis();
    if (param->write.is_prep || param->write.offset || commandPending) {
      control.reset(); fail("Control command already pending or invalid write"); return;
    }
    const std::string value = characteristic->getValue();
    const auto result = control.push(reinterpret_cast<const uint8_t *>(value.data()), value.size());
    if (result == ControlMessage::Result::Invalid) { fail("Invalid or oversized control message"); return; }
    if (result == ControlMessage::Result::Complete) {
      memcpy(pendingControl, control.bytes, control.used + 1); control.reset(); commandPending = true;
      wakeLoop();
    }
  }
};
class DataCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic, esp_ble_gatts_cb_param_t *param) override {
    Lock lock;
    if (!connected || disconnectRequested || param->write.conn_id != connectionId || busy) return;
    connectionTouched = millis();
    const std::string value = characteristic->getValue();
    if (param->write.is_prep || param->write.offset || !receiving ||
        !appendBleFrame(candidate.pixels, receivedBytes, reinterpret_cast<const uint8_t *>(value.data()), value.size())) {
      fail("Invalid frame chunk or offset"); return;
    }
    transferTouched = millis();
    wakeLoop();
  }
};
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server, esp_ble_gatts_cb_param_t *param) override {
    bool reject = false;
    {
      Lock lock;
      if (connected) reject = true;
      else {
        connected = true; disconnectRequested = connectionAuthorized = false; connectionId = param->connect.conn_id;
        connectionStarted = connectionTouched = millis(); connectionLogPending = true;
        control.reset(); commandPending = false;
        if (!busy) { receiving = false; receivedBytes = 0; state(idleState()); }
        randomHex(challenge, 16);
        if (hasFrame && !screenIsLabel && !busy) restoreScreen = true;
      }
    }
    advertisingPhase = AdvertisingPhase::Stopped;
    wakeLoop();
    if (reject) server->disconnect(param->connect.conn_id);
  }
  void onDisconnect(BLEServer *, esp_ble_gatts_cb_param_t *param) override {
    Lock lock;
    if (!connected || param->disconnect.conn_id != connectionId) return;
    connected = disconnectRequested = connectionAuthorized = false; connectionLogPending = true;
    control.reset(); commandPending = false;
    if (!busy) { receiving = false; receivedBytes = 0; state(idleState()); }
    randomHex(challenge, 16);
    fastAdvertisingWindow.begin(millis());
    advertisingPhase = AdvertisingPhase::Stopped;
    wakeLoop();
  }
};
class UnbondedCallbacks : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override { return 0; }
  void onPassKeyNotify(uint32_t) override { bleServer->disconnect(connectionId); }
  bool onSecurityRequest() override { return false; }
  bool onConfirmPIN(uint32_t) override { return false; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t) override {}
};
void gapEvent(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  if (event == ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT)
    advertisingDataReady = param->adv_data_raw_cmpl.status == ESP_BT_STATUS_SUCCESS;
  if (event == ESP_GAP_BLE_SET_STATIC_RAND_ADDR_EVT)
    advertisingAddressReady = param->set_rand_addr_cmpl.status == ESP_BT_STATUS_SUCCESS;
  if (event == ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT)
    scanResponseState = param->scan_rsp_data_raw_cmpl.status == ESP_BT_STATUS_SUCCESS ? 1 : -1;
  if (event == ESP_GAP_BLE_ADV_START_COMPLETE_EVT) {
    advertisingPhase = param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS ?
        AdvertisingPhase::Active : AdvertisingPhase::Stopped;
    advertisingTransitionFailed = param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS;
  }
  if (event == ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT) {
    advertisingPhase = param->adv_stop_cmpl.status == ESP_BT_STATUS_SUCCESS ?
        AdvertisingPhase::Stopped : AdvertisingPhase::Active;
    advertisingTransitionFailed = param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS;
  }
  wakeLoop();
}
void gattsEvent(esp_gatts_cb_event_t event, esp_gatt_if_t interface, esp_ble_gatts_cb_param_t *) {
  if (event == ESP_GATTS_REG_EVT) { Lock lock; gattInterface = interface; }
}
void showSetup() {
  bool open; const char *message;
  {
    Lock lock; open = pairingOpen;
    message = paired() ? "UPDATE IN INVENTORY COUNTER" : "SET UP IN INVENTORY COUNTER";
    screenIsLabel = false;
    if (hasFrame && !receiving && !busy) state("ready");
  }
  memset(screen, 0, sizeof(screen));
  panel::text(screen, 5, 5, "INVENTORY COUNTER", 2);
  if (open) {
    panel::text(screen, 5, 35, "PHONE SETUP", 2);
    panel::text(screen, 5, 65, "CHOOSE REPLACE DISPLAY");
    panel::text(screen, 5, 80, "IN INVENTORY COUNTER ON YOUR PHONE");
  } else panel::text(screen, 5, 48, message);
  panel::text(screen, 5, 101, "MENU 5S - SETUP 2 MIN");
  panel::text(screen, 5, 113, "BACK - RETURN TO LABEL");
  if (!renderPanel(screen)) {
    Serial.println("Display setup refresh timed out.");
    Lock lock; if (hasFrame) restoreScreen = true;
  }
}
void restoreLabel() {
  {
    Lock lock;
    if (!hasFrame || busy || receiving) return;
    updateQrVisibility();
    if (screenIsLabel && qrVisibleOnPanel == qrVisibleRequested && findOnPanel == findActive) { state(idleState()); return; }
    busy = uiRendering = true; state("busy");
  }
  const bool visible = renderSavedLabel(lastFrame, lastQr);
  {
    Lock lock; busy = uiRendering = false; screenIsLabel = visible;
    if (visible) {
      state(receiving ? "receiving" : idleState());
    } else {
      fail("Could not restore the saved label");
    }
  }
}
void openPairing() {
  bool disconnect = false; uint16_t previousConnection = 0;
  {
    Lock lock;
    pairingOpen = false; memset(&setupPublic, 0, sizeof(setupPublic)); busy = true;
    cancelFind();
    screenIsLabel = false;
    restoreScreen = false;
    if (connected) {
      // MENU starts a fresh enrollment session. Revoke writes immediately so
      // an old queued commit cannot close the window before GAP disconnects.
      disconnect = disconnectRequested = true; previousConnection = connectionId;
      receiving = false; receivedBytes = 0;
      commandPending = false; control.reset(); state(idleState());
    }
  }
  if (disconnect) bleServer->disconnect(previousConnection);
  bool ready;
  {
    FastCpu boost;
    ready = provisionCrypto.open(deviceId, panel::kProfile);
  }
  {
    Lock lock; busy = false;
    pairingOpen = true; pairingStarted = millis(); showPairScreen = true;
    qrVisibleRequested = true; awakeWindow.begin(pairingStarted);
    if (ready) {
      strcpy(setupPublic.nonce, provisionCrypto.nonce); strcpy(setupPublic.publicKey, provisionCrypto.devicePublic);
      strcpy(setupPublic.signature, provisionCrypto.deviceSignature);
    }
  }
}
void loadClaimIdentity(const char *authorityPublic = kProvisioningAuthorityPublicKey) {
  if (!storageReady || !provisionCrypto.initialize(authorityPublic)) return;
  uint8_t scalar[32] = {};
  const size_t existing = preferences.getBytesLength("claimkey");
  const bool ok = existing ? existing == sizeof(scalar) &&
      preferences.getBytes("claimkey", scalar, sizeof(scalar)) == sizeof(scalar) && provisionCrypto.loadClaim(scalar) :
      provisionCrypto.generateClaim(scalar) && writeBlob("claimkey", scalar, sizeof(scalar));
  if (!ok) { provisionCrypto.ready = false; provisionCrypto.claimPublic[0] = 0; }
  mbedtls_platform_zeroize(scalar, sizeof(scalar));
}
bool loadAdvertisingIdentity() {
  if (!storageReady) return false;
  if (preferences.getBytesLength("bleaddr") == sizeof(advertisingAddress) &&
      preferences.getBytes("bleaddr", advertisingAddress, sizeof(advertisingAddress)) == sizeof(advertisingAddress) &&
      validStaticAddress(advertisingAddress)) return true;
  do {
    esp_fill_random(advertisingAddress, sizeof(advertisingAddress)); advertisingAddress[0] |= 0xc0;
  } while (!validStaticAddress(advertisingAddress));
  return writeBlob("bleaddr", advertisingAddress, sizeof(advertisingAddress));
}
void removeLegacyBluetoothBonds() {
  int count = esp_ble_get_bond_device_num();
  if (count <= 0) return;
  esp_ble_bond_dev_t *bonds = static_cast<esp_ble_bond_dev_t *>(calloc(size_t(count), sizeof(esp_ble_bond_dev_t)));
  if (!bonds) return;
  const size_t bytes = size_t(count) * sizeof(esp_ble_bond_dev_t);
  if (esp_ble_get_bond_device_list(&count, bonds) == ESP_OK)
    for (int i = 0; i < count; ++i) esp_ble_remove_bond_device(bonds[i].bd_addr);
  memset(bonds, 0, bytes); free(bonds);
}
void serviceAdvertising(uint32_t now) {
  if (advertisingTransitionFailed) {
    advertisingTransitionFailed = false;
    advertisingRetryAt = now; advertisingRetryPending = true;
    Serial.println("BLE advertising transition failed; retrying");
  }
  bool shouldRun, shouldFast;
  {
    Lock lock;
    shouldRun = advertisingReady && scanResponseState == 1 && !connected && !radioRecoveryPending;
    shouldFast = pairingOpen || fastAdvertisingWindow.active(now);
  }
  const AdvertisingPhase phase = advertisingPhase;
  if (phase == AdvertisingPhase::Starting || phase == AdvertisingPhase::Stopping) return;
  if (phase == AdvertisingPhase::Active && shouldRun && advertisingFast == shouldFast) return;
  if (advertisingRetryPending && power_policy::remaining(now, advertisingRetryAt, 1000)) return;
  advertisingRetryPending = false;
  if (phase == AdvertisingPhase::Active) {
    // GAP owns the advertisement until STOP_COMPLETE; only then change its
    // interval or scan response and issue the next START.
    advertisingPhase = AdvertisingPhase::Stopping;
    if (esp_ble_gap_stop_advertising() != ESP_OK) {
      advertisingPhase = AdvertisingPhase::Active;
      advertisingRetryAt = now; advertisingRetryPending = true;
      Serial.println("BLE advertising could not stop; retrying");
    }
    return;
  }
  if (!shouldRun) return;
  advertisingFast = shouldFast;
  const uint16_t units = shouldFast ? power_policy::kFastAdvertisingUnits : power_policy::kSlowAdvertisingUnits;
  advertisingParameters.adv_int_min = units;
  advertisingParameters.adv_int_max = units;
  advertisingPhase = AdvertisingPhase::Starting;
  if (esp_ble_gap_start_advertising(&advertisingParameters) != ESP_OK) {
    advertisingPhase = AdvertisingPhase::Stopped;
    advertisingRetryAt = now; advertisingRetryPending = true;
    Serial.println("BLE advertising could not start; retrying");
  } else {
    Serial.printf("BLE advertising %s (%u ms)\n", shouldFast ? "fast" : "idle",
                  shouldFast ? 20u : 500u);
  }
}
void loadSavedState() {
  memset(&lastFrame, 0, sizeof(lastFrame)); memset(&lastName, 0, sizeof(lastName));
  memset(&lastQr, 0, sizeof(lastQr));
  strcpy(appliedBleName, kDefaultBleName); hasFrame = false;
  if (storageReady) {
    if (preferences.getBytesLength("pair") == sizeof(pairing)) preferences.getBytes("pair", &pairing, sizeof(pairing));
    if (pairing.magic != kRecordMagic || !memchr(pairing.box, 0, sizeof(pairing.box)) ||
        !memchr(pairing.generation, 0, sizeof(pairing.generation)) || !uuid(pairing.box) || !hex(pairing.generation, 16))
      memset(&pairing, 0, sizeof(pairing));
    activeSlot = preferences.getUChar("active", 0); if (activeSlot > 1) activeSlot = 0;
    const char *key = activeSlot ? "frame1" : "frame0";
    if (preferences.getBytesLength(key) == sizeof(lastFrame)) preferences.getBytes(key, &lastFrame, sizeof(lastFrame));
    hasFrame = paired() && storedFrameValid(lastFrame);
    if (hasFrame) {
      const char *nameKey = activeSlot ? "name1" : "name0";
      if (preferences.getBytesLength(nameKey) == sizeof(lastName))
        preferences.getBytes(nameKey, &lastName, sizeof(lastName));
      if (storedNameValid(lastName, lastFrame)) strcpy(appliedBleName, lastName.value);
      else bindName(lastName, lastFrame, kDefaultBleName, false);
      const char *qrKey = activeSlot ? "qr1" : "qr0";
      if (preferences.getBytesLength(qrKey) == sizeof(lastQr))
        preferences.getBytes(qrKey, &lastQr, sizeof(lastQr));
      if (!storedQrValid(lastQr, lastFrame)) memset(&lastQr, 0, sizeof(lastQr));
    }
  }
}
void setup() {
  normalBootForSleepObservation = esp_reset_reason() != ESP_RST_DEEPSLEEP;
  lightSleepObserved = false; lastObservedLightSleepWake = ESP_SLEEP_WAKEUP_UNDEFINED;
  Serial.begin(115200);
  releaseSleepPins();
  powerLoopTaskHandle = xTaskGetCurrentTaskHandle();
  stateMutex = xSemaphoreCreateMutex();
  pinMode(19, OUTPUT); digitalWrite(19, LOW);  // Keep the board LED off.
  pinMode(panel::kMenuPin, INPUT_PULLUP); pinMode(panel::kBackPin, INPUT_PULLUP);
  // Count an already held wake button from boot, including the startup refresh.
  menuDown = digitalRead(panel::kMenuPin) == LOW; menuStarted = millis(); menuLatched = false;
  menuButton.begin(menuStarted, menuDown);
  if (!inventory_counter_power::beginLightSleepWake(static_cast<gpio_num_t>(panel::kMenuPin),
        static_cast<gpio_num_t>(panel::kBackPin), powerLoopTaskHandle))
    Serial.println("Automatic light-sleep wake sources unavailable; staying awake");
  attachInterrupt(digitalPinToInterrupt(panel::kMenuPin), menuPressInterrupt, CHANGE);
  const uint64_t mac = ESP.getEfuseMac();
  snprintf(deviceId, sizeof(deviceId), "cw-%04x%08x", unsigned(mac >> 32), unsigned(mac));
  // Separate protocol namespace preserves all existing Wi-Fi records without
  // interpreting their bearer tokens as BLE provisioning credentials.
  storageReady = preferences.begin("cwble2", false);
  loadSavedState();
  randomHex(challenge, 16); state(idleState());
  if (hasFrame) restoreLabel(); else showSetup();
  if (!beginPowerSavingBleController()) Serial.println("BLE controller power setup failed");
  BLEDevice::init(appliedBleName);
  if (esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_N0) != ESP_OK ||
      esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_N0) != ESP_OK)
    Serial.println("BLE TX power could not be set to 0 dBm");
  // With the radio running, esp_fill_random receives hardware entropy for the
  // one-time claim key; do not generate a long-lived private key before this.
  loadClaimIdentity();
  BLEDevice::setMTU(247);
  // Plain GATT never initiates SMP. Explicitly reject incoming pairing too;
  // the library's default security-request callback would otherwise accept it.
  BLEDevice::setSecurityCallbacks(new UnbondedCallbacks());
  BLEDevice::setCustomGapHandler(gapEvent);
  BLEDevice::setCustomGattsHandler(gattsEvent);
  removeLegacyBluetoothBonds();
  bleServer = BLEDevice::createServer(); bleServer->setCallbacks(new ServerCallbacks());
  BLEService *service = bleServer->createService(kService);
  BLECharacteristic *info = service->createCharacteristic(kIdentity, BLECharacteristic::PROPERTY_READ);
  info->setAccessPermissions(ESP_GATT_PERM_READ); info->setCallbacks(new ReadCallbacks(true));
  BLECharacteristic *setupInfo = service->createCharacteristic(kSetup, BLECharacteristic::PROPERTY_READ);
  setupInfo->setAccessPermissions(ESP_GATT_PERM_READ); setupInfo->setCallbacks(new ReadCallbacks(2));
  BLECharacteristic *commands = service->createCharacteristic(kControl, BLECharacteristic::PROPERTY_WRITE);
  commands->setAccessPermissions(ESP_GATT_PERM_WRITE); commands->setCallbacks(new ControlCallbacks());
  BLECharacteristic *data = service->createCharacteristic(kData, BLECharacteristic::PROPERTY_WRITE);
  data->setAccessPermissions(ESP_GATT_PERM_WRITE); data->setCallbacks(new DataCallbacks());
  statusCharacteristic = service->createCharacteristic(kStatus, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  statusCharacteristic->setAccessPermissions(ESP_GATT_PERM_READ);
  statusCharacteristic->setCallbacks(new ReadCallbacks(false));
  statusSubscription = new BLE2902();
  statusSubscription->setAccessPermissions(ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE);
  statusCharacteristic->addDescriptor(statusSubscription); service->start();
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  BLEAdvertisementData advertisement;
  advertisement.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
  advertisement.setCompleteServices(BLEUUID(kService));
  advertising->setAdvertisementData(advertisement); advertising->setScanResponse(false);
  const bool scanReady = configureBleName(appliedBleName);
  const bool stableIdentity = loadAdvertisingIdentity();
  const esp_err_t addressResult = stableIdentity ? esp_ble_gap_set_rand_addr(advertisingAddress) : ESP_FAIL;
  const uint32_t advertisingStart = millis();
  while (addressResult == ESP_OK && (!advertisingAddressReady || !advertisingDataReady) &&
         millis() - advertisingStart < 2000) delay(5);
  // The pinned Arduino advertising wrapper leaves peer_addr uninitialized.
  // Use our zeroed parameters consistently for initial and reconnect starts.
  advertisingParameters.adv_int_min = power_policy::kFastAdvertisingUnits;
  advertisingParameters.adv_int_max = power_policy::kFastAdvertisingUnits;
  advertisingParameters.adv_type = ADV_TYPE_IND;
  advertisingParameters.own_addr_type = BLE_ADDR_TYPE_RANDOM;
  advertisingParameters.peer_addr_type = BLE_ADDR_TYPE_PUBLIC;
  advertisingParameters.channel_map = ADV_CHNL_ALL;
  advertisingParameters.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
  advertisingReady = stableIdentity && advertisingAddressReady && advertisingDataReady && scanReady;
  awakeWindow.begin(millis());
  fastAdvertisingWindow.begin(millis());
  serviceAdvertising(millis());
  Serial.printf("Inventory Counter %s BLE; device %s; panel %s; NVS %s; unbonded advertising %s\n", kVersion, deviceId,
                panel::kProfile, storageReady ? "ready" : "FAILED", advertisingReady ? "ready" : "FAILED");
  if (!inventory_counter_power::beginAutomaticLightSleep()) {
    if (inventory_counter_power::automaticPmState().pm_configured)
      Serial.println("Automatic light sleep could not be verified; retaining the awake safety lock");
    else {
      Serial.println("Automatic light sleep unavailable; using 80 MHz idle clock");
      if (!setCpuFrequencyMhz(80)) Serial.println("CPU idle clock could not be set to 80 MHz");
    }
  }
}
void serviceQrVisibility(uint32_t now) {
  bool transferring;
  {
    Lock lock; updateQrVisibility();
    if (!hasFrame || busy || pairingOpen ||
        (screenIsLabel && qrVisibleOnPanel == qrVisibleRequested && findOnPanel == findActive)) return;
    if (qrRetryPending && power_policy::remaining(now, qrRetryAt, 1000)) return;
    qrRetryPending = false;
    transferring = receiving;
    uiRendering = true; screenIsLabel = false;
    // Frame callbacks may continue filling candidate while the saved label is masked.
    if (!transferring) { busy = true; state("busy"); }
  }
  const bool rendered = renderSavedLabel(lastFrame, lastQr);
  {
    Lock lock;
    if (!transferring) busy = false;
    uiRendering = false; screenIsLabel = rendered;
    if (rendered) {
      if (!receiving && !busy) state(idleState());
    } else {
      qrRetryPending = true; qrRetryAt = millis();
      if (!receiving && !busy) fail("Could not refresh the label; retrying");
      Serial.println("Label refresh failed; retrying with Bluetooth available");
    }
  }
}
void loop() {
  if (!inventory_counter_power::holdAwakeForWork()) abort();
  bool menuHold = false;
  {
    Lock lock;
    updateQrVisibility();
    portENTER_CRITICAL(&menuEdgeMux);
    menuHold = menuButton.longPress;
    menuButton.longPress = false;
    portEXIT_CRITICAL(&menuEdgeMux);
  }
  if (menuHold) openPairing();
  serviceQrVisibility(millis());
  readUsbCommands();
  serviceQrVisibility(millis());
  bool show = false, restore = false, recoverRadio = false;
  bool disconnectIdle = false, reportConnection = false, connectionState = false;
  uint16_t idleConnection = 0;
  bool reportReadStack = false; uint32_t readStackBytes = 0;
  char message[kControlBytes + 1] = {};
  {
    Lock lock;
    if (pairingOpen && (millis() - pairingStarted >= kPairWindowMs || digitalRead(panel::kBackPin) == LOW)) {
      pairingOpen = false; memset(&setupPublic, 0, sizeof(setupPublic)); restoreScreen = true;
    }
    if (receiving && millis() - transferTouched >= kTransferTimeoutMs) fail("Frame transfer timed out");
    if (connected && !disconnectRequested && !busy &&
        (millis() - connectionTouched >= kIdleDisconnectMs ||
         (!connectionAuthorized && millis() - connectionStarted >= kIdleDisconnectMs))) {
      disconnectRequested = disconnectIdle = true; idleConnection = connectionId;
      commandPending = false; control.reset(); receiving = false; receivedBytes = 0;
    }
    if (commandPending && !busy) { strcpy(message, pendingControl); commandPending = false; }
    show = showPairScreen; showPairScreen = false;
    restore = restoreScreen; restoreScreen = false;
    recoverRadio = radioRecoveryPending && scanResponseState != 0 &&
        advertisingPhase == AdvertisingPhase::Stopped;
    reportReadStack = readStackReportPending; readStackReportPending = false;
    readStackBytes = readStackWatermark;
    reportConnection = connectionLogPending; connectionLogPending = false; connectionState = connected;
  }
  if (!pairingOpen && provisionCrypto.active) provisionCrypto.close();
  if (reportConnection) Serial.println(connectionState ? "BLE connected (unbonded)" : "BLE disconnected");
  if (disconnectIdle) bleServer->disconnect(idleConnection);
  if (reportReadStack) Serial.printf("BLE read callback minimum free stack: %u bytes\n", unsigned(readStackBytes));
  if (recoverRadio) {
    radioRecoveryPending = false;
    if (configureBleName(appliedBleName)) {
      advertisingReady = storageReady && advertisingAddressReady && advertisingDataReady;
    } else advertisingReady = false;
  }
  if (restore) { if (hasFrame) restoreLabel(); else showSetup(); }
  else if (show) showSetup();
  serviceQrVisibility(millis());
  if (message[0]) processCommand(message);
  serviceQrVisibility(millis());
  // Send the hint directly, leaving the characteristic's readable JSON
  // snapshot untouched until the next initial read (including long reads).
  char hint[20] = {}; uint16_t notifyConnection = 0;
  {
    Lock lock;
    if (notifyPending && connected && !disconnectRequested && statusSubscription->getNotifications()) {
      strlcpy(hint, statusState, sizeof(hint)); notifyConnection = connectionId;
    }
    notifyPending = false;
  }
  if (hint[0] && gattInterface != ESP_GATT_IF_NONE) esp_ble_gatts_send_indicate(gattInterface, notifyConnection,
      statusCharacteristic->getHandle(), strlen(hint), reinterpret_cast<uint8_t *>(hint), false);
  serviceAdvertising(millis());
  power_policy::WaitState wait;
  const uint32_t waitNow = millis();
  {
    Lock lock;
    wait.menuHeld = menuDown; wait.menuLatched = menuLatched; wait.menuStarted = menuStarted;
    wait.pairingOpen = pairingOpen; wait.pairingStarted = pairingStarted;
    wait.receiving = receiving; wait.transferTouched = transferTouched;
    wait.connected = connected; wait.disconnectRequested = disconnectRequested; wait.busy = busy;
    wait.connectionTouched = connectionTouched; wait.connectionAuthorized = connectionAuthorized;
    wait.connectionStarted = connectionStarted;
    wait.fastWindowLeft = fastAdvertisingWindow.left(waitNow);
  }
  if (Serial.available()) return;
  const TickType_t ticks = pdMS_TO_TICKS(power_policy::loopWaitMs(waitNow, wait));
  // A UART wake preamble must keep the receiver awake for the following command.
  const bool usbWaiting = usbRecentlyActive && power_policy::remaining(millis(), usbActiveAt, 1000);
  bool buttonStable;
  {
    portENTER_CRITICAL(&menuEdgeMux);
    buttonStable = !menuButton.held && !menuButton.rawHeld &&
        uint32_t(millis() - menuButton.rawAt) >= kMenuDebounceMs;
    portEXIT_CRITICAL(&menuEdgeMux);
  }
  const bool wakeArmed = !usbWaiting && buttonStable && !wait.pairingOpen &&
      inventory_counter_power::automaticPmState().auto_light_sleep && inventory_counter_power::armLightSleepWake();
  if (wakeArmed) inventory_counter_power::allowLightSleepWhileWaiting();
  ulTaskNotifyTake(pdTRUE, ticks ? ticks : 1);
  if (!inventory_counter_power::holdAwakeForWork()) abort();
  // IDF exposes a wake cause only after successful sleep. Latch evidence,
  // not a cycle count: the cause can persist across multiple loop iterations.
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (normalBootForSleepObservation && cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
    lightSleepObserved = true; lastObservedLightSleepWake = cause;
  }
  if (wakeArmed) {
    inventory_counter_power::restoreLightSleepWake();
    Lock lock; updateQrVisibility();
  }
}
