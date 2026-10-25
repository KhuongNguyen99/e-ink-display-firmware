#include "../inventory_counter_label/main.cpp"
#include <assert.h>
#include <iostream>

namespace panel {
unsigned renders = 0;
bool failNext = false;
unsigned failuresLeft = 0;
uint32_t advance = 0;
uint8_t displayed[kFrameBytes] = {};
std::vector<std::vector<uint8_t>> attempts;
void (*hook)() = nullptr;
bool render(const uint8_t *pixels) {
  ++renders; attempts.emplace_back(pixels, pixels + kFrameBytes); testMillis += advance;
  if (hook) { auto callback = hook; hook = nullptr; callback(); }
  if (failNext || failuresLeft) {
    failNext = false; if (failuresLeft) --failuresLeft; return false;
  }
  memcpy(displayed, pixels, sizeof(displayed)); return true;
}
void text(uint8_t *, unsigned, unsigned, const char *, unsigned) {}
}

static BLEServer server;
static BLE2902 subscription;
static BLECharacteristic frameCharacteristic, readCharacteristic, controlCharacteristic;
static ServerCallbacks connectionImplementation;
static BLEServerCallbacks &connections = connectionImplementation;
static DataCallbacks dataImplementation;
static BLECharacteristicCallbacks &data = dataImplementation;
static ReadCallbacks readImplementation(1);
static BLECharacteristicCallbacks &reads = readImplementation;
static ControlCallbacks controlImplementation;
static BLECharacteristicCallbacks &controls = controlImplementation;
static esp_ble_gatts_cb_param_t event;
static const QrBounds qr = {170, 20, 70};

void command(const std::string &wire) {
  std::vector<char> buffer(wire.begin(), wire.end()); buffer.push_back(0); processCommand(buffer.data());
}
void resetWindow() {
  qrVisibleRequested = qrVisibleOnPanel = true; qrRetryPending = false;
  awakeWindow.begin(millis());
  menuDown = menuLatched = pairingOpen = receiving = busy = uiRendering = false;
  menuButton.begin(millis(), false);
  restoreScreen = showPairScreen = commandPending = false; control.reset();
  connected = disconnectRequested = false;
  screenIsLabel = true; server.closed = false;
  advertisingReady = true; scanResponseState = 1; radioRecoveryPending = false;
  testRadioEntropyReady = true; testControllerStatus = ESP_BT_CONTROLLER_STATUS_ENABLED;
  testBluedroidStatus = ESP_BLUEDROID_STATUS_ENABLED;
  testPinLevels[2] = testPinLevels[1] = HIGH; panel::advance = 0;
  panel::failNext = false; panel::failuresLeft = 0; panel::hook = nullptr; panel::attempts.clear();
  connections.onConnect(&server, &event);
}
void installFixture() {
  storageReady = true; pairing.magic = kRecordMagic;
  strcpy(deviceId, "cw-18cedc8fcba4"); strcpy(pairing.box, "01234567-89ab-4cde-8123-456789abcdef");
  strcpy(pairing.generation, "0123456789abcdef");
  for (unsigned i = 0; i < 32; ++i) pairing.key[i] = uint8_t(i);
  memset(&lastFrame, 0, sizeof(lastFrame)); lastFrame.magic = kRecordMagic; lastFrame.revision = 1;
  strcpy(lastFrame.box, pairing.box); strcpy(lastFrame.generation, pairing.generation);
  strcpy(lastFrame.profile, panel::kProfile);
  for (unsigned x = 0; x < kWidth; ++x) {
    memset(lastFrame.pixels + x * 16, 255, 15); lastFrame.pixels[x * 16 + 15] = 0xc0;
  }
  hashFrame(lastFrame.pixels, lastFrame.sha256);
  bindName(lastName, lastFrame, kDefaultBleName, false); strcpy(appliedBleName, kDefaultBleName);
  bindQr(lastQr, lastFrame, qr); activeSlot = 0; hasFrame = true;
  preferences.values.clear();
  assert(writeBlob("pair", &pairing, sizeof(pairing)) && writeBlob("frame0", &lastFrame, sizeof(lastFrame)) &&
         writeBlob("name0", &lastName, sizeof(lastName)) && writeBlob("qr0", &lastQr, sizeof(lastQr)));
  memcpy(panel::displayed, lastFrame.pixels, kFrameBytes); resetWindow();
}
std::string begin(uint32_t revision, const char *digest, const QrBounds *bounds, bool named = true) {
  char transcript[kTranscriptBytes], signature[65], nameHash[65]; hashName(kDefaultBleName, nameHash);
  assert(beginTranscript(transcript, sizeof(transcript), deviceId, pairing.generation, pairing.box,
      panel::kProfile, revision, digest, challenge, named ? nameHash : nullptr, bounds));
  hmac(transcript, signature);
  cJSON *json = cJSON_CreateObject();
  cJSON_AddStringToObject(json, "op", "begin"); cJSON_AddStringToObject(json, "box_id", pairing.box);
  cJSON_AddStringToObject(json, "generation", pairing.generation); cJSON_AddStringToObject(json, "panel_profile", panel::kProfile);
  cJSON_AddNumberToObject(json, "revision", revision); cJSON_AddStringToObject(json, "sha256", digest);
  cJSON_AddStringToObject(json, "challenge", challenge); cJSON_AddStringToObject(json, "signature", signature);
  if (named) cJSON_AddStringToObject(json, "ble_name", kDefaultBleName);
  if (bounds) {
    cJSON_AddNumberToObject(json, "qr_x", bounds->x); cJSON_AddNumberToObject(json, "qr_y", bounds->y);
    cJSON_AddNumberToObject(json, "qr_size", bounds->size);
  }
  return serialized(json);
}
void upload(const uint8_t *pixels, size_t from = 0) {
  for (size_t offset = from; offset < kFrameBytes; offset += 18) {
    std::string packet; packet += char(offset & 255); packet += char(offset >> 8);
    packet.append(reinterpret_cast<const char *>(pixels + offset), std::min(size_t(18), kFrameBytes - offset));
    frameCharacteristic.setValue(packet); data.onWrite(&frameCharacteristic, &event);
  }
  assert(receiving && receivedBytes == kFrameBytes); command("{\"op\":\"commit\"}");
}
bool pixel(const uint8_t *frame, unsigned x, unsigned y) {
  return (frame[16 * (kWidth - 1 - x) + y / 8] & (0x80 >> (y % 8))) != 0;
}
void expire() { testMillis = awakeWindow.started + kAwakeWindowMs; }
void menuEdge(bool held) {
  testPinLevels[2] = held ? LOW : HIGH; menuPressInterrupt();
}
void shortPress() {
  menuEdge(true); testMillis += 80; menuEdge(false); testMillis += kMenuDebounceMs;
}
void callbackAtExpiry() {
  expire(); frameCharacteristic.setValue(std::string("\0\0\xff", 3));
  data.onWrite(&frameCharacteristic, &event);
}
void pressDuringRefresh() {
  // The button is pressed and released while the main loop cannot poll it.
  shortPress();
}
void twoPressesDuringRefresh() {
  shortPress(); shortPress();
}
const uint8_t *hookUploadPixels = nullptr;
void appendDuringRefresh() {
  const size_t offset = receivedBytes;
  std::string packet; packet += char(offset & 255); packet += char(offset >> 8);
  packet += char(hookUploadPixels[offset]);
  frameCharacteristic.setValue(packet); data.onWrite(&frameCharacteristic, &event);
}
std::string hookControlWire;
bool hookWholeControl = false;
void sendControl(size_t from, size_t limit) {
  for (size_t offset = from; offset < limit; offset += kBlePacketBytes) {
    controlCharacteristic.setValue(hookControlWire.substr(offset, std::min(kBlePacketBytes, limit - offset)));
    controls.onWrite(&controlCharacteristic, &event);
  }
}
void beginDuringUiRefresh() {
  assert(busy && uiRendering && !renderingCandidate);
  sendControl(0, hookWholeControl ? hookControlWire.size() : kBlePacketBytes);
  if (hookWholeControl) assert(commandPending && !control.used);
  else assert(!commandPending && control.used == kBlePacketBytes);
}
void releaseDuringStartup() {
  testMillis += 80; menuEdge(false); testMillis += 100;
}
void secondPressDuringStartup() {
  releaseDuringStartup(); shortPress();
}
void longPressDuringRefresh() {
  menuEdge(true); testMillis += power_policy::kMenuHoldMs;
  menuEdge(false); testMillis += kMenuDebounceMs;
}
void checkButtonDebounce() {
  MenuButton button;
  button.begin(0, false);
  button.sample(1, true); button.sample(20, false); button.sample(100, false);
  assert(!button.shortPress && !button.longPress && !button.held);
  button.sample(200, true); button.sample(280, false); button.sample(300, true);
  button.sample(320, false); button.sample(369, false);
  assert(!button.shortPress && button.held);
  button.sample(370, false); assert(button.shortPress && !button.longPress && !button.held);
  button.begin(1000, true); // Original wake release, including bounce, is ignored.
  button.sample(1100, false); button.sample(1120, true); button.sample(1140, false);
  button.sample(1190, false); assert(!button.shortPress && !button.longPress);
  button.sample(1200, true); button.sample(1280, false); button.sample(1330, false);
  assert(button.shortPress);
  button.begin(0, false); button.sample(10, true); button.sample(5009, false);
  button.sample(5059, false); assert(button.shortPress && !button.longPress);
  button.begin(0, false); button.sample(10, true); button.sample(5010, false);
  button.sample(5060, false); assert(!button.shortPress && button.longPress);
  button.begin(0, true); button.sample(5000, true); assert(button.longPress);
  button.longPress = false; button.sample(6000, false); button.sample(6050, false);
  assert(!button.longPress && !button.shortPress);
  button.begin(UINT32_MAX - 100, false);
  button.sample(UINT32_MAX - 80, true); button.sample(0, false); button.sample(50, false);
  assert(button.shortPress && !button.longPress);
  button.begin(UINT32_MAX - 100, true);
  button.sample(4899, true); assert(button.longPress && !button.shortPress);
  button.begin(0, false);
  button.sample(10, true); button.sample(90, false); button.sample(140, false);
  button.sample(150, true); button.sample(230, false); button.sample(280, false);
  assert(!button.shortPress && !button.longPress); // Two queued toggles cancel.
}

void assertCommitted() {
  cJSON *receipt = cJSON_Parse(status().c_str()); assert(receipt);
  assert(!strcmp(jsonString(receipt, "state"), "committed"));
  assert(!strcmp(jsonString(receipt, "sha256"), lastFrame.sha256));
  assert(cJSON_GetObjectItemCaseSensitive(receipt, "revision")->valuedouble == lastFrame.revision);
  char transcript[kTranscriptBytes], signature[65], nameHash[65];
  if (lastName.namedReceipt) hashName(lastName.value, nameHash);
  assert(committedTranscript(transcript, sizeof(transcript), deviceId, pairing.generation, pairing.box,
      panel::kProfile, lastFrame.revision, lastFrame.sha256, lastName.namedReceipt ? nameHash : nullptr));
  hmac(transcript, signature);
  const char *actual = jsonString(receipt, "signature"); assert(actual && !strcmp(actual, signature));
  cJSON_Delete(receipt);
}

void assertUnconfirmed() {
  cJSON *receipt = cJSON_Parse(status().c_str()); assert(receipt);
  assert(!jsonString(receipt, "signature")); cJSON_Delete(receipt);
}

void assertHidden(const SavedFrame &frame, const QrBounds &bounds) {
  uint8_t expected[kFrameBytes]; hideQr(frame.pixels, expected, bounds);
  assert(!memcmp(panel::displayed, expected, kFrameBytes));
  assert(!qrVisibleRequested && !qrVisibleOnPanel && screenIsLabel);
}

int main() {
  static_assert(sizeof(QrBounds) == 6 && offsetof(QrBounds, x) == 0 &&
      offsetof(QrBounds, y) == 2 && offsetof(QrBounds, size) == 4, "QR geometry storage layout");
  static_assert(sizeof(SavedQr) == 152 && offsetof(SavedQr, bounds) == 144 &&
      kQrMagic == 0x43575131, "QR sidecar remains compatible with firmware 3.5.0");
  static_assert(sizeof(SavedFrame) == 4144 && offsetof(SavedFrame, pixels) == 143 &&
      kRecordMagic == 0x43574232, "Legacy frame storage layout");
  static_assert(kAwakeWindowMs == 120000, "QR visibility window remains two minutes");
  assert(validQrBounds(0, 0, 122) && validQrBounds(249, 121, 1));
  assert(!validQrBounds(0, 0, 0) && !validQrBounds(249, 0, 2) &&
         !validQrBounds(0, 121, 2) && !validQrBounds(250, 0, 1));
  bleServer = &server; statusSubscription = &subscription;
  event.connect.conn_id = event.disconnect.conn_id = event.write.conn_id = event.read.conn_id = 7;
  BLEDevice::setCustomGapHandler(gapEvent);
  AwakeWindow window; window.begin(UINT32_MAX - 50);
  assert(window.left(49) == kAwakeWindowMs - 100 && !window.expired(49));
  assert(window.expired(uint32_t(UINT32_MAX - 50 + kAwakeWindowMs)));
  checkButtonDebounce();

  installFixture();
  const SavedFrame normal = lastFrame;
  uint8_t hidden[kFrameBytes]; hideQr(lastFrame.pixels, hidden, qr);
  assert(paddingValid(hidden) && !memcmp(&normal, &lastFrame, sizeof(normal)));
  for (unsigned x = 0; x < kWidth; ++x) for (unsigned y = 0; y < kHeight; ++y)
    assert(pixel(hidden, x, y) == !(x >= qr.x && x < qr.x + qr.size && y >= qr.y && y < qr.y + qr.size));
  loadSavedState(); assert(qrReady() && sameQrBounds(lastQr.bounds, qr));
  const SavedQr validMetadata = lastQr;
  for (unsigned corrupt : {0u, 4u, 8u, 25u, 41u, 78u}) {
    SavedQr bad = validMetadata; reinterpret_cast<uint8_t *>(&bad)[corrupt] ^= 1;
    writeBlob("qr0", &bad, sizeof(bad)); loadSavedState(); assert(!qrReady() && hasFrame);
  }
  SavedQr badBounds = validMetadata; badBounds.bounds.size = 0;
  writeBlob("qr0", &badBounds, sizeof(badBounds)); loadSavedState(); assert(!qrReady());
  preferences.values.erase("qr0"); loadSavedState(); assert(!qrReady());
  assert(identity().find("\"qr_sleep\":1") != std::string::npos);
  const unsigned beforeRepair = panel::renders;
  command(begin(lastFrame.revision, lastFrame.sha256, &qr)); upload(lastFrame.pixels);
  assert(qrReady() && panel::renders == beforeRepair && !strcmp(statusState, "committed"));
  assert(identity().size() <= 512 && identity().find("\"qr_sleep\":2") != std::string::npos);
  const uint32_t savedRevision = lastFrame.revision; lastFrame.revision = kMaxRevision;
  assert(identity().size() <= 512); lastFrame.revision = savedRevision;
  const auto savedSlots = preferences.values; const uint8_t slot = activeSlot;
  command(begin(lastFrame.revision, lastFrame.sha256, &qr)); upload(lastFrame.pixels);
  assert(preferences.values == savedSlots && activeSlot == slot && panel::renders == beforeRepair);
  std::string tampered = begin(lastFrame.revision, lastFrame.sha256, &qr);
  tampered.replace(tampered.find("\"qr_x\":170"), strlen("\"qr_x\":170"), "\"qr_x\":171");
  command(tampered); assert(!receiving);
  for (const char *replacement : {"-1", "170.5", "250"}) {
    std::string invalid = begin(lastFrame.revision, lastFrame.sha256, &qr);
    invalid.replace(invalid.find("\"qr_x\":170"), strlen("\"qr_x\":170"), std::string("\"qr_x\":") + replacement);
    command(invalid); assert(!receiving);
  }
  const QrBounds shifted = {169, 20, 70};
  preferences.failKey = activeSlot ? "qr0" : "qr1";
  command(begin(lastFrame.revision, lastFrame.sha256, &shifted)); upload(lastFrame.pixels);
  assert(!strcmp(statusState, "error") && activeSlot == slot && sameQrBounds(lastQr.bounds, qr));
  assert(panel::renders == beforeRepair); preferences.failKey.clear();
  uint8_t changed[kFrameBytes]; memcpy(changed, lastFrame.pixels, kFrameBytes); changed[0] ^= 0x80;
  char changedHash[65]; hashFrame(changed, changedHash); preferences.failKey = "active";
  command(begin(2, changedHash, &shifted)); upload(changed);
  assert(activeSlot == slot && sameQrBounds(lastQr.bounds, qr) && !memcmp(panel::displayed, normal.pixels, kFrameBytes));
  preferences.failKey.clear(); loadSavedState(); assert(qrReady() && lastFrame.revision == 1);
  // An unnamed QR command is also signed, and legacy updates never inherit bounds.
  command(begin(2, changedHash, &shifted, false)); upload(changed);
  assert(qrReady() && lastFrame.revision == 2 && !lastName.namedReceipt);
  command(begin(3, changedHash, nullptr, false)); upload(changed);
  assert(!qrReady() && !preferences.getBytesLength(activeSlot ? "qr1" : "qr0"));

  installFixture();
  const uint32_t start = awakeWindow.started;
  testMillis += 10000; reads.onRead(&readCharacteristic, &event);
  command(begin(1, lastFrame.sha256, &qr)); upload(lastFrame.pixels);
  assert(awakeWindow.started == start);
  menuEdge(true); testMillis += 80; loop();
  assert(awakeWindow.started == start && qrVisibleRequested);
  menuEdge(false); testMillis += kMenuDebounceMs; loop();
  assertHidden(lastFrame, qr); assertCommitted();
  assert(connected && !server.closed && awakeWindow.started == start);
  shortPress(); loop();
  assert(qrVisibleRequested && qrVisibleOnPanel && screenIsLabel);
  assert(!memcmp(panel::displayed, lastFrame.pixels, kFrameBytes));
  assert(awakeWindow.started > start && awakeWindow.left(millis()) == kAwakeWindowMs);
  assertCommitted();

  // UI refreshes must preserve incoming BEGIN fragments and complete commands.
  for (bool restoring : {false, true}) {
    installFixture();
    if (!restoring) {
      // The phone connects at the QR deadline; an untouched old connection
      // would already have hit the independent thirty-second idle timeout.
      expire(); connections.onDisconnect(&server, &event); connections.onConnect(&server, &event);
    }
    hookControlWire = begin(2, changedHash, &shifted) + "\n";
    hookWholeControl = restoring; panel::hook = beginDuringUiRefresh;
    if (restoring) { screenIsLabel = false; restoreLabel(); }
    else serviceQrVisibility(millis());
    assert(!receiving && !busy && !uiRendering);
    if (!restoring) sendControl(kBlePacketBytes, hookControlWire.size());
    assert(commandPending);
    loop(); assert(receiving && !commandPending && candidate.revision == 2);
    upload(changed); assert(lastFrame.revision == 2); assertCommitted();
    if (restoring) assert(qrVisibleOnPanel && !memcmp(panel::displayed, changed, kFrameBytes));
    else assertHidden(lastFrame, shifted);
  }

  // Expiry masks the label without disturbing an in-flight BLE transfer.
  installFixture(); command(begin(2, changedHash, &shifted));
  hookUploadPixels = changed; appendDuringRefresh(); assert(receivedBytes == 1);
  const uint32_t beforeExpiry = awakeWindow.started;
  const auto beforeMaskStorage = preferences.values;
  expire(); panel::hook = appendDuringRefresh; serviceQrVisibility(millis());
  assertHidden(lastFrame, qr);
  assert(connected && !server.closed && receiving && receivedBytes == 2);
  assert(!strcmp(statusState, "receiving") && preferences.values == beforeMaskStorage);
  assert(testControllerStatus == ESP_BT_CONTROLLER_STATUS_ENABLED && testBluedroidStatus == ESP_BLUEDROID_STATUS_ENABLED);
  assertUnconfirmed();
  panel::attempts.clear(); upload(changed, 2);
  assert(lastFrame.revision == 2 && sameQrBounds(lastQr.bounds, shifted));
  assertHidden(lastFrame, shifted); assertCommitted();
  uint8_t changedHidden[kFrameBytes]; hideQr(changed, changedHidden, shifted);
  assert(panel::attempts.size() == 1 && !memcmp(panel::attempts[0].data(), changedHidden, kFrameBytes));
  assert(awakeWindow.started == beforeExpiry && testDeepSleepCalls == 0);

  // Hidden state accepts connections, reads, duplicate updates, and new frames.
  connections.onDisconnect(&server, &event); connections.onConnect(&server, &event);
  assert(connected && !restoreScreen); reads.onRead(&readCharacteristic, &event);
  assert(readCharacteristic.getValue().find("\"revision\":2") != std::string::npos);
  assertHidden(lastFrame, shifted); assertCommitted();
  const unsigned beforeHiddenDuplicate = panel::renders;
  command(begin(2, changedHash, &shifted)); upload(changed);
  assert(panel::renders == beforeHiddenDuplicate); assertCommitted();
  // A new signed mask for the same pixels changes the physical projection.
  command(begin(2, changedHash, &qr)); upload(changed);
  assert(panel::renders == beforeHiddenDuplicate + 1);
  assertHidden(lastFrame, qr); assertCommitted();

  // Hidden legacy updates cannot reuse another frame's old QR bounds.
  command(begin(3, changedHash, nullptr, false)); upload(changed);
  assert(!qrReady() && !preferences.getBytesLength(activeSlot ? "qr1" : "qr0"));
  for (uint8_t value : panel::displayed) assert(value == 0);
  assert(!memcmp(lastFrame.pixels, changed, kFrameBytes)); assertCommitted();

  // Hide and show failures retry while the radio and canonical cache survive.
  installFixture(); const SavedFrame beforeFailedHide = lastFrame;
  const auto beforeFailedHideStorage = preferences.values;
  panel::failNext = true; expire(); serviceQrVisibility(millis());
  assert(!qrVisibleRequested && !screenIsLabel && qrRetryPending);
  assert(connected && !server.closed && !memcmp(&lastFrame, &beforeFailedHide, sizeof(lastFrame)));
  assert(preferences.values == beforeFailedHideStorage); assertUnconfirmed();
  const unsigned failedHideRenders = panel::renders;
  testMillis += 999; serviceQrVisibility(millis()); assert(panel::renders == failedHideRenders);
  ++testMillis; serviceQrVisibility(millis());
  assertHidden(lastFrame, qr); assertCommitted();
  shortPress(); panel::failNext = true; serviceQrVisibility(millis());
  assert(qrVisibleRequested && !screenIsLabel && qrRetryPending); assertUnconfirmed();
  testMillis += 1000; serviceQrVisibility(millis());
  assert(qrVisibleOnPanel && screenIsLabel); assertCommitted();
  assert(!memcmp(panel::displayed, lastFrame.pixels, kFrameBytes));

  // Failed commit and rollback preserve the currently requested hidden mode.
  installFixture(); expire(); serviceQrVisibility(millis());
  const SavedFrame beforeFailedCommit = lastFrame;
  const uint8_t previousSlot = activeSlot;
  preferences.failKey = "active"; panel::attempts.clear();
  command(begin(2, changedHash, &shifted)); upload(changed);
  assert(!strcmp(statusState, "error") && activeSlot == previousSlot);
  assert(!memcmp(&lastFrame, &beforeFailedCommit, sizeof(lastFrame)));
  assertHidden(lastFrame, qr); assertUnconfirmed();
  assert(panel::attempts.size() == 2);
  assert(!memcmp(panel::attempts[0].data(), changedHidden, kFrameBytes));
  assert(!memcmp(panel::attempts[1].data(), hidden, kFrameBytes));
  preferences.failKey.clear(); loadSavedState();
  assert(qrReady() && sameQrBounds(lastQr.bounds, qr) && lastFrame.revision == 1);

  // If both candidate refresh and rollback fail, retry the durable masked frame.
  installFixture(); expire(); serviceQrVisibility(millis());
  command(begin(2, changedHash, &shifted)); panel::failuresLeft = 2; upload(changed);
  assert(lastFrame.revision == 1 && !screenIsLabel && !strcmp(statusState, "error"));
  assert(connected && !server.closed); assertUnconfirmed();
  serviceQrVisibility(millis()); assertHidden(lastFrame, qr); assertCommitted();

  // A visibility deadline during a refresh corrects the candidate and commits it.
  installFixture(); panel::attempts.clear();
  command(begin(2, changedHash, &shifted)); panel::hook = callbackAtExpiry; upload(changed);
  assert(lastFrame.revision == 2 && sameQrBounds(lastQr.bounds, shifted));
  assert(panel::attempts.size() == 2); assertHidden(lastFrame, shifted); assertCommitted();
  assert(connected && !server.closed);
  // Staging that crosses the deadline starts with the masked candidate directly.
  installFixture(); command(begin(2, changedHash, &shifted));
  testMillis = awakeWindow.started + kAwakeWindowMs - 1; preferences.writeAdvanceMs = 100;
  panel::attempts.clear(); upload(changed); preferences.writeAdvanceMs = 0;
  assert(lastFrame.revision == 2 && panel::attempts.size() == 1);
  assert(!memcmp(panel::attempts[0].data(), changedHidden, kFrameBytes));
  assertHidden(lastFrame, shifted); assertCommitted();

  // A metadata-only duplicate must also notice expiry during NVS staging.
  installFixture(); command(begin(1, lastFrame.sha256, &qr));
  testMillis = awakeWindow.started + kAwakeWindowMs - 1; preferences.writeAdvanceMs = 100;
  panel::attempts.clear(); upload(lastFrame.pixels); preferences.writeAdvanceMs = 0;
  assert(lastFrame.revision == 1 && lastName.namedReceipt && panel::attempts.size() == 1);
  assertHidden(lastFrame, qr); assertCommitted();

  // Expiry inside the final durable write defers the receipt until correction.
  installFixture(); command(begin(2, changedHash, &shifted));
  testMillis = awakeWindow.started + kAwakeWindowMs - 350; preferences.writeAdvanceMs = 100;
  upload(changed); preferences.writeAdvanceMs = 0;
  assert(lastFrame.revision == 2 && !qrVisibleRequested && !screenIsLabel);
  assert(!strcmp(statusState, "ready")); assertUnconfirmed();
  serviceQrVisibility(millis()); assertHidden(lastFrame, shifted); assertCommitted();

  // Menu events captured during refresh change visibility without aborting commit.
  installFixture(); command(begin(2, changedHash, &shifted));
  panel::hook = pressDuringRefresh; upload(changed);
  assert(lastFrame.revision == 2); assertHidden(lastFrame, shifted); assertCommitted();
  installFixture(); const uint32_t beforeTwoPresses = awakeWindow.started;
  command(begin(2, changedHash, &shifted)); panel::hook = twoPressesDuringRefresh; upload(changed);
  assert(lastFrame.revision == 2 && qrVisibleRequested && qrVisibleOnPanel);
  assert(awakeWindow.started == beforeTwoPresses); assertCommitted();
  installFixture(); shortPress(); reads.onRead(&readCharacteristic, &event);
  command(begin(2, changedHash, &shifted)); upload(changed);
  assert(lastFrame.revision == 2); assertHidden(lastFrame, shifted); assertCommitted();

  // A hold opens physical provisioning even after the QR window expired.
  installFixture(); expire(); serviceQrVisibility(millis());
  menuEdge(true); loop(); testMillis += power_policy::kMenuHoldMs; loop();
  assert(pairingOpen && awakeWindow.started == pairingStarted);
  assert(qrVisibleRequested && awakeWindow.left(millis()) == kAwakeWindowMs);
  menuEdge(false); testMillis += kMenuDebounceMs; loop(); assert(pairingOpen && qrVisibleRequested);
  resetWindow();
  menuEdge(true); testMillis += 4999; menuEdge(false); testMillis += kMenuDebounceMs; loop();
  assert(!qrVisibleRequested && !pairingOpen);
  resetWindow(); command(begin(2, changedHash, &shifted));
  panel::hook = longPressDuringRefresh; upload(changed);
  assert(qrVisibleRequested && lastFrame.revision == 2);
  loop(); assert(pairingOpen && qrVisibleRequested);

  // Releasing a held boot button is ignored, but another complete tap toggles.
  installFixture(); connected = false; screenIsLabel = false; testPinLevels[2] = LOW;
  testControllerStatus = ESP_BT_CONTROLLER_STATUS_IDLE; panel::hook = releaseDuringStartup;
  setup(); loop(); assert(qrVisibleRequested && !pairingOpen && !menuDown);
  assert(awakeWindow.left(millis()) == kAwakeWindowMs);
  installFixture(); connected = false; screenIsLabel = false; testPinLevels[2] = LOW;
  testControllerStatus = ESP_BT_CONTROLLER_STATUS_IDLE; panel::hook = secondPressDuringStartup;
  setup(); loop(); assert(!qrVisibleRequested && !qrVisibleOnPanel);
  // A failed startup panel refresh does not prevent a BLE connection.
  installFixture(); connected = false; screenIsLabel = false;
  testControllerStatus = ESP_BT_CONTROLLER_STATUS_IDLE; panel::failNext = true;
  setup(); assert(!screenIsLabel && advertisingReady);
  connections.onConnect(&server, &event); assert(connected); assertUnconfirmed();
  loop(); assert(screenIsLabel); assertCommitted();
  // Startup display work counts toward a five-second physical setup hold.
  installFixture(); connected = false; screenIsLabel = false;
  testPinLevels[2] = LOW; const uint32_t bootAt = millis(); panel::advance = 6000;
  testControllerStatus = ESP_BT_CONTROLLER_STATUS_IDLE;
  setup();
  assert(menuDown && menuStarted == bootAt);
  assert(testInterruptPin == 2 && testPinInterrupt == menuPressInterrupt);
  loop(); assert(menuLatched && pairingOpen);
  menuEdge(false); testMillis += kMenuDebounceMs; loop(); assert(pairingOpen && qrVisibleRequested);
  assert(testDeepSleepCalls == 0 && testControllerStatus == ESP_BT_CONTROLLER_STATUS_ENABLED);
  // A quiet PM mode is not sleep evidence; only a successful wake is latched.
  testPinLevels[2] = HIGH; testPinLevels[1] = HIGH;
  normalBootForSleepObservation = true; lightSleepObserved = false;
  lastObservedLightSleepWake = ESP_SLEEP_WAKEUP_UNDEFINED;
  testSleepWakeCause = ESP_SLEEP_WAKEUP_UNDEFINED; loop(); assert(!lightSleepObserved);
  testSleepWakeCause = ESP_SLEEP_WAKEUP_TIMER; loop();
  assert(lightSleepObserved && lastObservedLightSleepWake == ESP_SLEEP_WAKEUP_TIMER);
  testSleepWakeCause = ESP_SLEEP_WAKEUP_UNDEFINED; loop();
  assert(lightSleepObserved && lastObservedLightSleepWake == ESP_SLEEP_WAKEUP_TIMER);
  normalBootForSleepObservation = false; lightSleepObserved = false;
  testSleepWakeCause = ESP_SLEEP_WAKEUP_TIMER; loop(); assert(!lightSleepObserved);
  testSleepWakeCause = ESP_SLEEP_WAKEUP_UNDEFINED;
  std::cout << "Always-available BLE, transient QR masking, signed receipts, failure retries, Menu and persistence tests passed\n";
}
