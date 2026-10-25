#include "../countwell_label/main.cpp"
#include <assert.h>
#include <iostream>

namespace panel {
unsigned renders = 0;
bool failNext = false;
uint8_t lastPixels[kFrameBytes] = {};
uint32_t lastRenderCpuMHz = 0;
bool render(const uint8_t *pixels) {
  ++renders; lastRenderCpuMHz = getCpuFrequencyMhz();
  memcpy(lastPixels, pixels, kFrameBytes);
  if (failNext) { failNext = false; return false; } return true;
}
void text(uint8_t *, unsigned, unsigned, const char *, unsigned) {}
}

static const char *testKey = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
static const char *testBox = "01234567-89ab-4cde-8123-456789abcdef";
static const char *testGeneration = "0123456789abcdef";

void command(const std::string &text) {
  std::vector<char> bytes(text.begin(), text.end()); bytes.push_back(0); processCommand(bytes.data());
}
void usb(const std::string &text) {
  std::vector<char> bytes(text.begin(), text.end()); bytes.push_back(0); processUsbCommand(bytes.data());
}
std::string beginRequest(uint32_t revision, const char *digest, const char *nonce, bool valid = true,
                         const char *name = nullptr) {
  char signature[65], transcript[kTranscriptBytes], nameHash[65];
  if (name) hashName(name, nameHash);
  beginTranscript(transcript, sizeof(transcript), deviceId, pairing.generation, pairing.box,
                  panel::kProfile, revision, digest, nonce, name ? nameHash : nullptr);
  hmac(transcript, signature);
  if (!valid) signature[0] = signature[0] == '0' ? '1' : '0';
  return std::string("{\"op\":\"begin\",\"box_id\":\"") + pairing.box + "\",\"generation\":\"" + pairing.generation +
      "\",\"panel_profile\":\"ssd1680\",\"revision\":" + std::to_string(revision) + " ,\"sha256\":\"" + digest +
      "\",\"challenge\":\"" + nonce + "\",\"signature\":\"" + signature + "\"" +
      (name ? std::string(",\"ble_name\":\"") + name + "\"" : std::string()) + "}";
}
void sendFrame(BLECharacteristicCallbacks &callback, BLECharacteristic &characteristic,
               esp_ble_gatts_cb_param_t &event, const uint8_t *pixels) {
  for (size_t offset = 0; offset < kFrameBytes; offset += 18) {
    const size_t count = std::min(size_t(18), kFrameBytes - offset);
    std::string packet; packet += char(offset & 255); packet += char(offset >> 8);
    packet.append(reinterpret_cast<const char *>(pixels + offset), count);
    characteristic.setValue(packet); callback.onWrite(&characteristic, &event);
  }
}
void checkReceipt(const std::string &value, const char *name = nullptr) {
  assert(value.size() <= 512);
  cJSON *receipt = cJSON_Parse(value.c_str()); assert(receipt);
  const char *signature = jsonString(receipt, "signature"); assert(signature && hex(signature, 64));
  char transcript[kTranscriptBytes], expected[65], nameHash[65];
  if (name) hashName(name, nameHash);
  committedTranscript(transcript, sizeof(transcript), deviceId, pairing.generation, pairing.box,
                      panel::kProfile, lastFrame.revision, lastFrame.sha256, name ? nameHash : nullptr);
  hmac(transcript, expected); assert(constantEqual(expected, signature));
  assert(name ? constantEqual(nameHash, jsonString(receipt, "ble_name_sha256")) :
                !cJSON_GetObjectItemCaseSensitive(receipt, "ble_name_sha256"));
  assert(!cJSON_GetObjectItemCaseSensitive(receipt, "key"));
  cJSON_Delete(receipt);
}
int main() {
  assert(flatCommandJson(" {\"op\":\"begin\",\"revision\":1}\n"));
  assert(flatCommandJson(R"({"op":"test","text":"{ [ ] } \" quote \\ slash"})"));
  for (const char *invalid : {"", "[]", "{\"op\":{}}", "{\"op\":[]}", "{}{}", "{\"op\":\"unterminated}"})
    assert(!flatCommandJson(invalid));
  UsbCommand usbLine;
  const std::string validUsb = "CWUSB {\"op\":\"usb_identity\"}\r\n";
  for (size_t i = 0; i < validUsb.size(); ++i)
    assert(usbLine.push(validUsb[i]) == (i + 1 == validUsb.size() ? UsbCommand::Result::Complete : UsbCommand::Result::More));
  usbLine.reset();
  const std::string excessiveUsb = "CWUSB " + std::string(kControlBytes + 1, 'a') + "\n";
  for (size_t i = 0; i < excessiveUsb.size(); ++i)
    assert(usbLine.push(excessiveUsb[i]) == (i + 1 == excessiveUsb.size() ? UsbCommand::Result::Invalid : UsbCommand::Result::More));
  usbLine.reset(); usbLine.push(0);
  for (size_t i = 0; i < validUsb.size(); ++i)
    assert(usbLine.push(validUsb[i]) == (i + 1 == validUsb.size() ? UsbCommand::Result::Invalid : UsbCommand::Result::More));
  uint8_t address[6] = {0xc0, 0, 0, 0, 0, 0}; assert(!validStaticAddress(address));
  memset(address, 0xff, sizeof(address)); assert(!validStaticAddress(address));
  address[5] = 0; assert(validStaticAddress(address)); address[0] = 0x7f; assert(!validStaticAddress(address));
  storageReady = true; pairing.magic = kRecordMagic;
  strcpy(deviceId, "cw-18cedc8fcba4"); strcpy(pairing.box, testBox); strcpy(pairing.generation, testGeneration);
  assert(decodeBleKey(testKey, pairing.key));
  BLEServer server; bleServer = &server;
  BLE2902 subscription; statusSubscription = &subscription;
  ServerCallbacks serverCallbacks; BLEServerCallbacks &connectionCallbacks = serverCallbacks;
  ControlCallbacks controlCallbacks; BLECharacteristicCallbacks &commandCallbacks = controlCallbacks;
  DataCallbacks dataCallbacks; BLECharacteristicCallbacks &frameCallbacks = dataCallbacks;
  UnbondedCallbacks securityCallbacks; BLESecurityCallbacks &security = securityCallbacks;
  assert(!security.onSecurityRequest() && !security.onConfirmPIN(123456));
  esp_ble_gatts_cb_param_t event; event.connect.conn_id = event.disconnect.conn_id = event.write.conn_id = 7;
  connectionCallbacks.onConnect(&server, &event);
  assert(connected && !connectionAuthorized && !receiving);
  const std::string initialChallenge = challenge;
  const std::string nested = "{\"op\":" + std::string(500, '[') + std::string(500, ']') + "}";
  command(nested); assert(!receiving && !connectionAuthorized && initialChallenge == challenge);
  Serial.output.clear(); usb(nested);
  assert(Serial.output.find("\"ok\":false") != std::string::npos && initialChallenge == challenge);
  BLECharacteristic frame;
  frame.setValue(std::string("\0\0\x80", 3)); frameCallbacks.onWrite(&frame, &event);
  assert(!receiving && receivedBytes == 0 && !hasFrame && panel::renders == 0);
  command("{\"op\":\"commit\"}");
  assert(!hasFrame && panel::renders == 0 && !connectionAuthorized && initialChallenge == challenge);

  const Pairing original = pairing;
  const std::string pairRequest = std::string("{\"op\":\"pair\",\"box_id\":\"") + testBox +
      "\",\"generation\":\"" + testGeneration + "\",\"key\":\"" + testKey + "\"}";
  command(pairRequest);
  assert(!memcmp(&pairing, &original, sizeof(pairing)) && !receiving);
  assert(preferences.values.find("pair") == preferences.values.end());
  uint8_t pixels[kFrameBytes] = {}; char digest[65]; hashFrame(pixels, digest);
  command(beginRequest(1, digest, challenge, false));
  assert(!receiving && !connectionAuthorized && initialChallenge == challenge);
  const std::string firstBegin = beginRequest(1, digest, challenge);
  command(firstBegin);
  assert(receiving && connectionAuthorized && initialChallenge != challenge);
  command(firstBegin); // accepted challenge was already consumed
  assert(!receiving && !hasFrame);
  command(beginRequest(1, digest, challenge));
  frame.setValue(std::string("\1\0\x80", 3)); frameCallbacks.onWrite(&frame, &event);
  assert(!receiving && receivedBytes == 0);
  command(beginRequest(1, digest, challenge));
  sendFrame(frameCallbacks, frame, event, pixels);
  assert(receiving && receivedBytes == kFrameBytes);
  command("{\"op\":\"commit\"}");
  assert(hasFrame && lastFrame.revision == 1 && panel::renders == 1);
  checkReceipt(status());

  const std::string replayAfterReconnect = beginRequest(2, digest, challenge);
  const std::string oldChallenge = challenge;
  connectionCallbacks.onDisconnect(&server, &event);
  connectionCallbacks.onConnect(&server, &event);
  assert(oldChallenge != challenge && !connectionAuthorized && !receiving);
  command(replayAfterReconnect); assert(!receiving);
  command(beginRequest(2, digest, challenge));
  pixels[0] = 0x80; sendFrame(frameCallbacks, frame, event, pixels);
  command("{\"op\":\"commit\"}");
  assert(hasFrame && lastFrame.revision == 1 && panel::renders == 1); // forged pixels never persisted/rendered

  Serial.output.clear(); usb("{\"op\":\"usb_identity\"}");
  assert(Serial.output.find("\"phone_pairing\":false") != std::string::npos);
  assert(Serial.output.find(testKey) == std::string::npos && Serial.output.find("\"key\"") == std::string::npos);
  const std::string usbPair = std::string("{\"op\":\"usb_pair\",\"box_id\":\"") + testBox +
      "\",\"generation\":\"" + testGeneration + "\",\"key\":\"" + testKey + "\"}";
  Serial.output.clear(); usb(usbPair); assert(Serial.output.find("\"ok\":false") != std::string::npos);
  connectionCallbacks.onDisconnect(&server, &event);
  pairingOpen = true; pairingStarted = millis();
  Serial.output.clear(); usb(usbPair);
  assert(Serial.output.find("\"ok\":true") != std::string::npos && Serial.output.find("\"receipt\":") != std::string::npos);
  assert(Serial.output.find(testKey) == std::string::npos && Serial.output.find("\"key\"") == std::string::npos);
  cJSON *usbResponse = cJSON_Parse(Serial.output.c_str() + 6); assert(usbResponse);
  cJSON *pairedReceipt = cJSON_GetObjectItemCaseSensitive(usbResponse, "receipt"); assert(cJSON_IsObject(pairedReceipt));
  assert(!strcmp(jsonString(pairedReceipt, "state"), "paired"));
  assert(!strcmp(jsonString(pairedReceipt, "device_id"), deviceId));
  assert(!strcmp(jsonString(pairedReceipt, "box_id"), testBox));
  assert(!strcmp(jsonString(pairedReceipt, "generation"), testGeneration));
  char pairTranscript[kTranscriptBytes], pairSignature[65];
  pairedTranscript(pairTranscript, sizeof(pairTranscript), deviceId, pairing.generation, pairing.box);
  hmac(pairTranscript, pairSignature);
  assert(constantEqual(pairSignature, jsonString(pairedReceipt, "signature")));
  cJSON_Delete(usbResponse);
  assert(hasFrame && lastFrame.revision == 1 && !pairingOpen);
  screenIsLabel = false; restoreScreen = false; connectionCallbacks.onConnect(&server, &event);
  assert(restoreScreen); restoreLabel(); assert(screenIsLabel);
  // Reading repeatedly cannot hold an unauthorized connection indefinitely.
  restoreScreen = showPairScreen = false;
  testMillis = connectionStarted + kIdleDisconnectMs + 1; connectionTouched = testMillis;
  loop(); assert(server.closed && disconnectRequested && !receiving);

  preferences.failWrites = true; assert(!loadAdvertisingIdentity());
  preferences.failWrites = false; assert(loadAdvertisingIdentity() && validStaticAddress(advertisingAddress));
  uint8_t savedAddress[6]; memcpy(savedAddress, advertisingAddress, 6); memset(advertisingAddress, 0, 6);
  assert(loadAdvertisingIdentity() && !memcmp(savedAddress, advertisingAddress, 6));
  BLEDevice::setCustomGapHandler(gapEvent);
  assert(configureBleName(appliedBleName));
  advertisingReady = true;
  connectionCallbacks.onDisconnect(&server, &event);
  assert(!connected && scanResponseState == 1);
  testAdvertisingStarts = testAdvertisingStops = 0;
  testAdvertisingIntervals.clear();
  fastAdvertisingWindow.begin(testMillis);
  serviceAdvertising(testMillis);
  assert(advertisingPhase == AdvertisingPhase::Active && advertisingFast &&
         testAdvertisingIntervals.back() == power_policy::kFastAdvertisingUnits);
  testMillis += power_policy::kFastWindowMs;
  serviceAdvertising(testMillis);  // A completed stop precedes the interval change.
  assert(advertisingPhase == AdvertisingPhase::Stopped && testAdvertisingStops == 1);
  serviceAdvertising(testMillis);
  assert(advertisingPhase == AdvertisingPhase::Active && !advertisingFast &&
         testAdvertisingIntervals.back() == power_policy::kSlowAdvertisingUnits);
  pairingOpen = true;
  serviceAdvertising(testMillis);
  serviceAdvertising(testMillis);
  assert(advertisingPhase == AdvertisingPhase::Active && advertisingFast &&
         testAdvertisingStops == 2 && testAdvertisingStarts == 3 &&
         testAdvertisingIntervals.back() == power_policy::kFastAdvertisingUnits);
  pairingOpen = false;
  connectionCallbacks.onConnect(&server, &event);
  assert(!strcmp(appliedBleName, kDefaultBleName));
  cJSON *initialIdentity = cJSON_Parse(identity().c_str()); assert(initialIdentity);
  char nameHash[65]; hashName(kDefaultBleName, nameHash);
  assert(constantEqual(jsonString(initialIdentity, "ble_name_sha256"), nameHash));
  assert(!cJSON_GetObjectItemCaseSensitive(initialIdentity, "ble_name"));
  cJSON_Delete(initialIdentity);

  pixels[0] = 0;
  const char *widget = "E-ink Widget", *gizmo = "E-ink Gizmo", *newName = "E-ink Next";
  std::string forgedName = beginRequest(2, digest, challenge, true, widget);
  forgedName.replace(forgedName.find("Widget"), 6, "Gadget");
  command(forgedName);
  assert(!receiving && !strcmp(appliedBleName, kDefaultBleName));
  command(beginRequest(2, digest, challenge, true, widget));
  sendFrame(frameCallbacks, frame, event, pixels);
  const unsigned firstNamedRender = panel::renders;
  command("{\"op\":\"commit\"}");
  assert(hasFrame && lastFrame.revision == 2 && panel::renders == firstNamedRender + 1);
  assert(!strcmp(appliedBleName, widget) && testDeviceName == widget);
  assert(testScanResponse[1] == 0x09 && testScanResponse.size() == strlen(widget) + 2);
  checkReceipt(status(), widget);
  cJSON *namedIdentity = cJSON_Parse(identity().c_str()); assert(namedIdentity);
  hashName(widget, nameHash);
  assert(constantEqual(jsonString(namedIdentity, "ble_name_sha256"), nameHash));
  cJSON_Delete(namedIdentity);

  command(beginRequest(2, digest, challenge, true, gizmo));
  sendFrame(frameCallbacks, frame, event, pixels);
  const unsigned duplicateRenders = panel::renders;
  command("{\"op\":\"commit\"}");
  assert(panel::renders == duplicateRenders && !strcmp(appliedBleName, gizmo));
  checkReceipt(status(), gizmo);
  assert(storedNameValid(lastName, lastFrame));
  memset(&lastFrame, 0, sizeof(lastFrame)); memset(&lastName, 0, sizeof(lastName));
  strcpy(appliedBleName, kDefaultBleName); hasFrame = false;
  loadSavedState();
  assert(hasFrame && lastFrame.revision == 2 && !strcmp(appliedBleName, gizmo));
  checkReceipt(status(), gizmo);

  preferences.failWrites = true;
  command(beginRequest(2, digest, challenge, true, gizmo));
  sendFrame(frameCallbacks, frame, event, pixels);
  command("{\"op\":\"commit\"}");
  assert(!strcmp(statusState, "committed") && !strcmp(appliedBleName, gizmo));
  preferences.failWrites = false;

  const uint8_t preservedSlot = activeSlot;
  const std::string preservedFrame = preservedSlot ? "frame1" : "frame0";
  const std::string inactiveFrame = preservedSlot ? "frame0" : "frame1";
  preferences.failRemoveKey = inactiveFrame;
  command(beginRequest(2, digest, challenge, true, newName));
  sendFrame(frameCallbacks, frame, event, pixels);
  command("{\"op\":\"commit\"}");
  assert(activeSlot == preservedSlot && lastFrame.revision == 2 && !strcmp(appliedBleName, gizmo));
  assert(preferences.getBytesLength(preservedFrame.c_str()) == sizeof(SavedFrame));
  preferences.failRemoveKey.clear();

  pixels[0] = 0x80; char nextDigest[65]; hashFrame(pixels, nextDigest);
  preferences.failKey = activeSlot ? "frame0" : "frame1";
  command(beginRequest(3, nextDigest, challenge, true, newName));
  sendFrame(frameCallbacks, frame, event, pixels);
  const unsigned beforeStorage = panel::renders;
  command("{\"op\":\"commit\"}");
  assert(panel::renders == beforeStorage && lastFrame.revision == 2 && !strcmp(appliedBleName, gizmo));
  preferences.failKey.clear();

  command(beginRequest(3, nextDigest, challenge, true, newName));
  sendFrame(frameCallbacks, frame, event, pixels);
  panel::failNext = true;
  command("{\"op\":\"commit\"}");
  assert(lastFrame.revision == 2 && !strcmp(appliedBleName, gizmo) && panel::renders == beforeStorage + 2);
  assert(screenIsLabel && !strcmp(statusState, "error"));

  testFailScanCount = 1;
  command(beginRequest(3, nextDigest, challenge, true, newName));
  sendFrame(frameCallbacks, frame, event, pixels);
  command("{\"op\":\"commit\"}");
  assert(lastFrame.revision == 2 && !strcmp(appliedBleName, gizmo) && testDeviceName == gizmo);
  assert(testScanResponse[1] == 0x09 && !memcmp(testScanResponse.data() + 2, gizmo, strlen(gizmo)));

  preferences.failKey = "active";
  command(beginRequest(3, nextDigest, challenge, true, newName));
  sendFrame(frameCallbacks, frame, event, pixels);
  command("{\"op\":\"commit\"}");
  assert(lastFrame.revision == 2 && !strcmp(appliedBleName, gizmo) && testDeviceName == gizmo);
  preferences.failKey.clear();

  size_t storedBytes = 0;
  for (const auto &entry : preferences.values) storedBytes += entry.second.size();
  preferences.capacityBytes = storedBytes + 64;
  preferences.removedKeys.clear();
  const uint8_t oldActive = activeSlot;
  command(beginRequest(3, nextDigest, challenge));
  sendFrame(frameCallbacks, frame, event, pixels);
  command("{\"op\":\"commit\"}");
  preferences.capacityBytes = std::numeric_limits<size_t>::max();
  assert(lastFrame.revision == 3 && !strcmp(appliedBleName, gizmo));
  assert(activeSlot == 1 - oldActive);
  assert(std::find(preferences.removedKeys.begin(), preferences.removedKeys.end(),
                   oldActive ? "frame0" : "frame1") != preferences.removedKeys.end());
  assert(std::find(preferences.removedKeys.begin(), preferences.removedKeys.end(),
                   oldActive ? "frame1" : "frame0") == preferences.removedKeys.end());
  checkReceipt(status());  // Legacy update retains the advertised name and legacy signature.
  preferences.values.erase(activeSlot ? "name1" : "name0");
  memset(&lastFrame, 0, sizeof(lastFrame)); memset(&lastName, 0, sizeof(lastName));
  strcpy(appliedBleName, "E-ink wrong"); hasFrame = false;
  loadSavedState();
  assert(hasFrame && lastFrame.revision == 3 && !strcmp(appliedBleName, kDefaultBleName));
  assert(!lastName.namedReceipt && storedNameValid(lastName, lastFrame));
  testCpuMHz = 80;
  assert(renderPanel(pixels) && panel::lastRenderCpuMHz == 240 && testCpuMHz == 80);
  // The actual startup path enables controller sleep, sets default/advertising
  // power, and leaves the CPU at its idle clock after display work is done.
  connected = pairingOpen = radioRecoveryPending = false;
  testControllerStatus = ESP_BT_CONTROLLER_STATUS_IDLE;
  countwell_power::controllerPowerState() = countwell_power::ControllerPowerState{};
  testControllerInitCalls = testControllerEnableCalls = testControllerSleepCalls = 0;
  testTxPower.clear();
  testCpuMHz = 240;
  setup();
  assert(testControllerConfig.sleep_mode == ESP_BT_SLEEP_MODE_1 &&
         testControllerConfig.sleep_clock == ESP_BT_SLEEP_CLOCK_MAIN_XTAL);
  assert(testControllerInitCalls == 1 && testControllerEnableCalls == 1 &&
         testControllerSleepCalls == 1 && testCpuMHz == 80);
  assert(testTxPower[ESP_BLE_PWR_TYPE_DEFAULT] == ESP_PWR_LVL_N0 &&
         testTxPower[ESP_BLE_PWR_TYPE_ADV] == ESP_PWR_LVL_N0);
  Serial.output.clear(); usb("{\"op\":\"usb_power\"}");
  assert(Serial.output.find("\"ble_tx_0dbm\":true") != std::string::npos);
  assert(Serial.output.find("\"cpu_mhz\":80") != std::string::npos);
  assert(awakeWindow.left(millis()) == kAwakeWindowMs);
  connectionCallbacks.onConnect(bleServer, &event);
  const std::string normalIdentity = identity().c_str();
  assert(normalIdentity.size() <= 512 && normalIdentity.find("\"find_tag\":true") != std::string::npos);
  assert(normalIdentity.find("\"firmware_version\":\"3.7.0\"") != std::string::npos);
  const SavedFrame savedFrame = lastFrame;
  const SavedName savedName = lastName;
  const auto savedStorage = preferences.values;
  const std::string rejectedChallenge = challenge;
  const unsigned beforeUnsupported = panel::renders;
  const uint32_t windowStart = awakeWindow.started;
  BLECharacteristic unsupportedControl;
  unsupportedControl.setValue("{\"op\":\"find\"}\n");
  commandCallbacks.onWrite(&unsupportedControl, &event);
  assert(commandPending);
  loop();
  assert(!commandPending && !receiving && !connectionAuthorized && screenIsLabel);
  assert(!strcmp(statusState, "error") && !strcmp(errorText, "Invalid Find authorization or saved label"));
  assert(rejectedChallenge == challenge && panel::renders == beforeUnsupported && awakeWindow.started == windowStart);
  assert(!memcmp(&lastFrame, &savedFrame, sizeof(savedFrame)) &&
         !memcmp(&lastName, &savedName, sizeof(savedName)) && preferences.values == savedStorage);
  assert(!memcmp(panel::lastPixels, savedFrame.pixels, kFrameBytes));
  assert(status().find("\"find_remaining_ms\"") == std::string::npos);

  // Rejection leaves the same challenge available for a normal signed update.
  uint8_t nextPixels[kFrameBytes]; memcpy(nextPixels, lastFrame.pixels, sizeof(nextPixels));
  nextPixels[0] ^= 0x80;
  char updateHash[65]; hashFrame(nextPixels, updateHash);
  command(beginRequest(savedFrame.revision + 1, updateHash, rejectedChallenge.c_str()));
  assert(receiving && connectionAuthorized && rejectedChallenge != challenge && panel::renders == beforeUnsupported);
  sendFrame(frameCallbacks, frame, event, nextPixels);
  command("{\"op\":\"commit\"}");
  assert(screenIsLabel && lastFrame.revision == savedFrame.revision + 1 &&
         !strcmp(statusState, "committed") && panel::renders == beforeUnsupported + 1);
  assert(!memcmp(panel::lastPixels, nextPixels, kFrameBytes) && awakeWindow.started == windowStart);
  checkReceipt(status());
  std::cout << "Actual firmware unbonded callbacks, signed transfer, replay rejection, legacy USB provisioning, receipts and persistent identity passed\n";
}
