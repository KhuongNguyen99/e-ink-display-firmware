#include "../countwell_label/main.cpp"
#include <assert.h>
#include <fstream>
#include <iostream>
#include <iterator>

namespace panel {
bool render(const uint8_t *) { return true; }
void text(uint8_t *, unsigned, unsigned, const char *, unsigned) {}
}
void (*cryptoHook)() = nullptr;
void fixtureRandom(void *buffer, size_t size) {
  if (cryptoHook) { auto callback = cryptoHook; cryptoHook = nullptr; callback(); }
  auto *out = static_cast<uint8_t *>(buffer); memset(out, 0, size);
  if (size == 16) for (size_t i = 0; i < size; ++i) out[i] = uint8_t(0x10 + i);
  else if (size) out[size - 1] = 3;
}
void shortPressDuringCrypto() {
  testPinLevels[panel::kMenuPin] = LOW; menuPressInterrupt(); testMillis += 80;
  testPinLevels[panel::kMenuPin] = HIGH; menuPressInterrupt(); testMillis += kMenuDebounceMs;
}
void expireDuringCrypto() { testMillis = awakeWindow.started + kAwakeWindowMs; }
void expireSetupDuringCrypto() { testMillis = pairingStarted + kPairWindowMs; }
void command(cJSON *json) {
  char *message = cJSON_PrintUnformatted(json); assert(message && strlen(message) <= kControlBytes);
  processCommand(message); cJSON_free(message);
}
void freshSession(BLEServerCallbacks &callbacks, BLEServer &server, esp_ble_gatts_cb_param_t &event) {
  if (connected) callbacks.onDisconnect(&server, &event);
  // Direct setup calls stand in for another completed physical hold. Do not
  // carry an unsampled release from the initial loop-driven hold into it.
  testPinLevels[panel::kMenuPin] = HIGH; menuButton.begin(millis(), false);
  openPairing(); callbacks.onConnect(&server, &event);
  assert(pairingOpen && setupPublic.nonce[0] && !disconnectRequested);
}
void verifyDeviceSignature(const char *publicHex, const char *expectedContext) {
  mbedtls_ecp_group group; mbedtls_ecp_group_init(&group);
  mbedtls_ecp_point publicPoint; mbedtls_ecp_point_init(&publicPoint);
  mbedtls_mpi r, s; mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
  uint8_t point[65], signature[64], hash[32];
  assert(decodeHex(publicHex, point, sizeof(point)));
  assert(decodeHex(setupPublic.signature, signature, sizeof(signature)));
  assert(!setupSha256(reinterpret_cast<const uint8_t *>(expectedContext), strlen(expectedContext), hash));
  assert(!mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1));
  assert(!mbedtls_ecp_point_read_binary(&group, &publicPoint, point, sizeof(point)));
  assert(!mbedtls_mpi_read_binary(&r, signature, 32) && !mbedtls_mpi_read_binary(&s, signature + 32, 32));
  assert(!mbedtls_ecdsa_verify(&group, hash, sizeof(hash), &publicPoint, &r, &s));
  hash[0] ^= 1; assert(mbedtls_ecdsa_verify(&group, hash, sizeof(hash), &publicPoint, &r, &s));
  mbedtls_mpi_free(&r); mbedtls_mpi_free(&s); mbedtls_ecp_point_free(&publicPoint); mbedtls_ecp_group_free(&group);
}
void resignFixture(cJSON *request) {
  // Deliberately public fixture authority scalar 1. This allows testing a
  // correctly authorized envelope with an invalid GCM tag or invalid EC point.
  char transcript[612], signatureHex[129]; uint8_t hash[32], raw[64];
  snprintf(transcript, sizeof(transcript), "CW3|provision|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s",
      jsonString(request, "authority_key_id"), deviceId, panel::kProfile, jsonString(request, "box_id"),
      jsonString(request, "generation"), jsonString(request, "setup_nonce"), setupPublic.publicKey,
      jsonString(request, "server_public_key"), jsonString(request, "iv"), jsonString(request, "ciphertext"), jsonString(request, "tag"));
  mbedtls_ecp_group group; mbedtls_ecp_group_init(&group);
  mbedtls_mpi scalar, r, s; mbedtls_mpi_init(&scalar); mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
  assert(!mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) && !mbedtls_mpi_lset(&scalar, 1));
  assert(!setupSha256(reinterpret_cast<const uint8_t *>(transcript), strlen(transcript), hash));
  assert(!mbedtls_ecdsa_sign(&group, &r, &s, &scalar, hash, sizeof(hash), provisioningRandom, nullptr));
  assert(!mbedtls_mpi_write_binary(&r, raw, 32) && !mbedtls_mpi_write_binary(&s, raw + 32, 32));
  encodeHex(raw, sizeof(raw), signatureHex); cJSON_ReplaceItemInObjectCaseSensitive(request, "signature", cJSON_CreateString(signatureHex));
  mbedtls_mpi_free(&scalar); mbedtls_mpi_free(&r); mbedtls_mpi_free(&s); mbedtls_ecp_group_free(&group);
}
int main(int argc, char **argv) {
  assert(argc == 2); std::ifstream stream(argv[1]);
  const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  cJSON *fixture = cJSON_Parse(text.c_str()); assert(fixture);
  cJSON *request = cJSON_GetObjectItemCaseSensitive(fixture, "command");
  cJSON *setup = cJSON_GetObjectItemCaseSensitive(fixture, "setup");
  const char *authority = jsonString(cJSON_GetObjectItemCaseSensitive(fixture, "authority"), "public_key");
  uint8_t expectedKey[32], claimScalar[32] = {}; claimScalar[31] = 2;
  assert(decodeHex(jsonString(fixture, "permanent_key"), expectedKey, 32));
  strcpy(deviceId, jsonString(setup, "device_id")); storageReady = true; testRandomOverride = fixtureRandom;
  preferences.putBytes("claimkey", claimScalar, sizeof(claimScalar));
  const auto originalStorage = preferences.values;
  loadClaimIdentity(authority); assert(provisionCrypto.ready);
  assert(!strcmp(provisionCrypto.claimPublic, jsonString(fixture, "claim_public_key")));
  assert(preferences.values == originalStorage);
  BLEServer server; bleServer = &server; BLE2902 subscription; statusSubscription = &subscription;
  ServerCallbacks serverCallbacks; BLEServerCallbacks &callbacks = serverCallbacks;
  esp_ble_gatts_cb_param_t event; event.connect.conn_id = event.disconnect.conn_id = event.write.conn_id = 7;
  assert(setupIdentity().find("\"setup_open\":false") != std::string::npos);
  command(request); assert(!paired()); // Physical setup is required.
  const uint32_t holdStarted = millis();
  testPinLevels[panel::kMenuPin] = LOW; loop(); testMillis = holdStarted + 4999; loop(); assert(!pairingOpen);
  testMillis = holdStarted + 5000; loop(); assert(pairingOpen); testPinLevels[panel::kMenuPin] = HIGH;
  callbacks.onConnect(&server, &event);
  assert(!strcmp(setupPublic.publicKey, jsonString(setup, "device_public_key")));
  assert(!strcmp(setupPublic.nonce, jsonString(setup, "setup_nonce")));
  verifyDeviceSignature(jsonString(fixture, "claim_public_key"), jsonString(fixture, "setup_context"));
  assert(setupIdentity().size() <= 512 && identity().size() <= 512);
  assert(setupIdentity().find(jsonString(fixture, "permanent_key")) == std::string::npos);
  Serial.output.clear(); char enrollment[] = "{\"op\":\"usb_enrollment\"}"; processUsbCommand(enrollment);
  assert(Serial.output.find(jsonString(fixture, "claim_public_key")) != std::string::npos);
  assert(Serial.output.find("private") == std::string::npos && Serial.output.find(jsonString(fixture, "permanent_key")) == std::string::npos);
  for (const char *field : {"authority_key_id", "box_id", "generation", "setup_nonce", "server_public_key", "iv", "ciphertext", "tag", "signature"}) {
    cJSON *bad = cJSON_Duplicate(request, true); cJSON *item = cJSON_GetObjectItemCaseSensitive(bad, field);
    item->valuestring[strlen(item->valuestring)-1] ^= 1;
    command(bad); assert(!paired() && pairingOpen && !busy); cJSON_Delete(bad);
  }
  cJSON *badTag = cJSON_Duplicate(request, true); cJSON_GetObjectItemCaseSensitive(badTag, "tag")->valuestring[0] = '0';
  resignFixture(badTag); command(badTag); assert(!paired()); cJSON_Delete(badTag);
  cJSON *badPoint = cJSON_Duplicate(request, true); memset(cJSON_GetObjectItemCaseSensitive(badPoint, "server_public_key")->valuestring + 2, '0', 128);
  resignFixture(badPoint); command(badPoint); assert(!paired()); cJSON_Delete(badPoint);
  cJSON *extra = cJSON_Duplicate(request, true); cJSON_AddStringToObject(extra, "key", jsonString(fixture, "permanent_key"));
  command(extra); assert(!paired()); cJSON_Delete(extra);
  char nested[] = "{\"op\":\"provision\",\"ciphertext\":{}}"; processCommand(nested); assert(!paired());
  const std::string originalId = deviceId; strcpy(deviceId, "cw-wrong-device"); command(request); assert(!paired()); strcpy(deviceId, originalId.c_str());
  testMillis += kPairWindowMs; command(request); assert(!paired());
  freshSession(callbacks, server, event);
  pairing.magic = kRecordMagic; strcpy(pairing.box, "11234567-89ab-4cde-8123-456789abcdef");
  strcpy(pairing.generation, jsonString(request, "generation")); memcpy(pairing.key, expectedKey, 32);
  const Pairing otherBinding = pairing; command(request); assert(!memcmp(&pairing, &otherBinding, sizeof(pairing)));
  memset(&pairing, 0, sizeof(pairing));
  preferences.failKey = "pair"; command(request);
  assert(!paired() && !pairingOpen && !provisionCrypto.active); preferences.failKey.clear();
  command(request); assert(!paired()); // Consumed even when persistence failed.
  freshSession(callbacks, server, event);
  ControlCallbacks controlCallbacks; BLECharacteristicCallbacks &controlCallback = controlCallbacks; BLECharacteristic controlCharacteristic;
  char *encoded = cJSON_PrintUnformatted(request); const std::string wire = std::string(encoded) + "\n"; cJSON_free(encoded);
  for (size_t i = 0; i < wire.size(); i += kBlePacketBytes) {
    controlCharacteristic.setValue(wire.substr(i, kBlePacketBytes)); controlCallback.onWrite(&controlCharacteristic, &event);
  }
  assert(commandPending && !paired()); loop();
  assert(paired() && !pairingOpen && !provisionCrypto.active && !busy && connectionAuthorized);
  assert(!memcmp(pairing.key, expectedKey, 32)); assert(preferences.getBytesLength("pair") == sizeof(Pairing));
  const Pairing saved = pairing; command(request); assert(!memcmp(&saved, &pairing, sizeof(pairing)));
  callbacks.onDisconnect(&server, &event); callbacks.onConnect(&server, &event);
  cJSON *receipt = cJSON_Parse(status().c_str()); assert(receipt);
  assert(!strcmp(jsonString(receipt, "state"), "paired"));
  char transcript[kTranscriptBytes], signature[65]; pairedTranscript(transcript, sizeof(transcript), deviceId, pairing.generation, pairing.box);
  hmac(transcript, signature); assert(!strcmp(jsonString(receipt, "signature"), signature)); cJSON_Delete(receipt);
  assert(identity().size() <= 512 && status().size() <= 512);
  freshSession(callbacks, server, event); command(request); assert(!memcmp(&saved, &pairing, sizeof(pairing))); // Exact retry.
  // QR policy changes during ECDH do not revoke a physical setup capability.
  freshSession(callbacks, server, event);
  cryptoHook = shortPressDuringCrypto; command(request);
  { Lock lock; updateQrVisibility(); }
  assert(!cryptoHook && !qrVisibleRequested && !busy && !pairingOpen);
  assert(!strcmp(statusState, "paired") && connectionAuthorized);
  assert(!memcmp(&saved, &pairing, sizeof(pairing)));
  freshSession(callbacks, server, event);
  // Make the QR deadline earlier than the independent provisioning deadline.
  awakeWindow.begin(millis() - kAwakeWindowMs + 1000);
  cryptoHook = expireDuringCrypto; command(request);
  { Lock lock; updateQrVisibility(); }
  assert(!cryptoHook && !qrVisibleRequested && !busy && !pairingOpen);
  assert(!strcmp(statusState, "paired") && !memcmp(&saved, &pairing, sizeof(pairing)));
  // Physical setup expiry still rejects a decrypt that crosses its deadline.
  freshSession(callbacks, server, event);
  const auto beforeExpiredSetup = preferences.values;
  cryptoHook = expireSetupDuringCrypto; command(request);
  assert(!cryptoHook && !busy && !strcmp(statusState, "error"));
  assert(preferences.values == beforeExpiredSetup && !memcmp(&saved, &pairing, sizeof(pairing)));
  // Short taps and QR expiry during setup key generation cannot cancel enrollment.
  cryptoHook = shortPressDuringCrypto; openPairing();
  assert(!cryptoHook && pairingOpen && !busy && setupPublic.nonce[0]);
  awakeWindow.begin(millis());
  cryptoHook = expireDuringCrypto; openPairing();
  assert(!cryptoHook && pairingOpen && !busy && setupPublic.nonce[0]);
  freshSession(callbacks, server, event); testPinLevels[panel::kBackPin] = LOW; loop(); testPinLevels[panel::kBackPin] = HIGH;
  assert(!pairingOpen && !provisionCrypto.active); command(request); assert(!memcmp(&saved, &pairing, sizeof(pairing)));
  uint8_t invalidScalar[32] = {}; preferences.putBytes("claimkey", invalidScalar, sizeof(invalidScalar));
  loadClaimIdentity(authority); assert(!provisionCrypto.ready && !provisionCrypto.claimPublic[0]);
  assert(preferences.values["claimkey"] == std::vector<uint8_t>(32, 0)); // Never regenerate corrupt established identity.
  preferences.values.erase("claimkey"); loadClaimIdentity(authority);
  assert(provisionCrypto.ready && preferences.values["claimkey"].size() == 32 && !memcmp(&saved, &pairing, sizeof(pairing)));
  preferences.values.erase("claimkey"); testRadioEntropyReady = false; loadClaimIdentity(authority);
  assert(!provisionCrypto.ready && preferences.values["claimkey"].empty());
  if (connected) callbacks.onDisconnect(&server, &event);
  openPairing(); // Legacy USB window still opens, but encrypted setup stays unavailable.
  assert(pairingOpen && !setupPublic.nonce[0]);
  cJSON_Delete(fixture);
  std::cout << "Encrypted provisioning, device signatures, replay, malformed input and NVS tests passed\n";
}
