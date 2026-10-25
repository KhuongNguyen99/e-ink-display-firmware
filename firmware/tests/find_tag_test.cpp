// Reuse the complete BLE/panel fixture without running the QR-only suite.
// Its renamed entry point relies on main's implicit return and is never called.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main sleepFixtureMain
#include "sleep_test.cpp"
#undef main
#pragma GCC diagnostic pop

void findFixture() {
  findActive = findOnPanel = findTimerArmed = false; findStarted = 0;
  renderingCandidate = false; receivedBytes = 0;
  preferences.failWrites = false; preferences.failKey.clear();
  preferences.failRemoveKey.clear(); preferences.writeAdvanceMs = 0;
  testMillis = 1000;
  installFixture();
  memset(lastFrame.pixels, 0, kFrameBytes);
  for (unsigned x = 0; x < kWidth; ++x)
    for (unsigned y = 0; y < kHeight; ++y)
      if ((3 * x + y) % 5 < 2) blackPixel(lastFrame.pixels, x, y);
  hashFrame(lastFrame.pixels, lastFrame.sha256);
  bindName(lastName, lastFrame, kDefaultBleName, false);
  bindQr(lastQr, lastFrame, qr);
  assert(writeBlob("frame0", &lastFrame, sizeof(lastFrame)) &&
         writeBlob("name0", &lastName, sizeof(lastName)) &&
         writeBlob("qr0", &lastQr, sizeof(lastQr)));
  memcpy(panel::displayed, lastFrame.pixels, kFrameBytes);
}

std::string findWire(const QrBounds &bounds = qr) {
  // Independent legacy wire contract: do not use the firmware transcript helper.
  char transcript[kTranscriptBytes], signature[65];
  snprintf(transcript, sizeof(transcript), "CW2|find|%s|%s|%s|%s|%lu|%s|%s|30000|%u|%u|%u",
      deviceId, pairing.generation, pairing.box, panel::kProfile,
      static_cast<unsigned long>(lastFrame.revision), lastFrame.sha256, challenge,
      unsigned(bounds.x), unsigned(bounds.y), unsigned(bounds.size));
  hmac(transcript, signature);
  cJSON *json = cJSON_CreateObject();
  cJSON_AddStringToObject(json, "op", "find");
  cJSON_AddStringToObject(json, "box_id", pairing.box);
  cJSON_AddStringToObject(json, "generation", pairing.generation);
  cJSON_AddStringToObject(json, "panel_profile", panel::kProfile);
  cJSON_AddNumberToObject(json, "revision", lastFrame.revision);
  cJSON_AddStringToObject(json, "sha256", lastFrame.sha256);
  cJSON_AddStringToObject(json, "challenge", challenge);
  cJSON_AddStringToObject(json, "signature", signature);
  cJSON_AddNumberToObject(json, "duration_ms", 30000);
  cJSON_AddNumberToObject(json, "qr_x", bounds.x);
  cJSON_AddNumberToObject(json, "qr_y", bounds.y);
  cJSON_AddNumberToObject(json, "qr_size", bounds.size);
  return serialized(json);
}

void assertHighlightPixels(const uint8_t *actual, const uint8_t *normal,
                           const QrBounds &bounds, bool visible) {
  assert(paddingValid(actual));
  for (unsigned x = 0; x < kWidth; ++x) for (unsigned y = 0; y < kHeight; ++y) {
    const bool inQr = x >= bounds.x && x < unsigned(bounds.x) + bounds.size &&
        y >= bounds.y && y < unsigned(bounds.y) + bounds.size;
    const bool inHalo = int(x) >= int(bounds.x) - 12 && x < unsigned(bounds.x) + bounds.size + 12 &&
        int(y) >= int(bounds.y) - 12 && y < unsigned(bounds.y) + bounds.size + 12;
    const bool expected = inQr ? visible && pixel(normal, x, y) :
        inHalo ? false : !pixel(normal, x, y);
    assert(pixel(actual, x, y) == expected);
  }
}

void assertFinding(bool visible) {
  assert(findActive && findOnPanel && findTimerArmed && screenIsLabel);
  assert(qrVisibleRequested == visible && qrVisibleOnPanel == visible);
  assertHighlightPixels(panel::displayed, lastFrame.pixels, qr, visible);
  cJSON *report = cJSON_Parse(status().c_str()); assert(report);
  assert(!strcmp(jsonString(report, "state"), "finding"));
  assert(!jsonString(report, "signature"));
  cJSON *remaining = cJSON_GetObjectItemCaseSensitive(report, "find_remaining_ms");
  assert(cJSON_IsNumber(remaining) && remaining->valuedouble >= 0 && remaining->valuedouble <= 30000);
  cJSON_Delete(report);
}

void expireFind() { testMillis = findStarted + kFindDurationMs; }
void expireFindDuringRefresh() { expireFind(); assertUnconfirmed(); }
void expireQrDuringRefresh() { expire(); assertUnconfirmed(); }

void rejectChangedField(const char *field, const char *replacement) {
  findFixture();
  cJSON *json = cJSON_Parse(findWire().c_str()); assert(json);
  cJSON *value = cJSON_Parse(replacement); assert(value);
  assert(cJSON_ReplaceItemInObjectCaseSensitive(json, field, value));
  const unsigned before = panel::renders;
  const std::string oldChallenge = challenge;
  command(serialized(json));
  assert(!findActive && !findOnPanel && panel::renders == before);
  assert(oldChallenge == challenge && !strcmp(statusState, "error"));
  assertUnconfirmed();
}

int main() {
  static_assert(kFindDurationMs == 30000 && kFindRestoreRetryMs == 1000 && kFindQrHalo == 12,
      "Legacy Find duration, retry and quiet zone remain stable");
  bleServer = &server; statusSubscription = &subscription;
  event.connect.conn_id = event.disconnect.conn_id = event.write.conn_id = event.read.conn_id = 7;
  BLEDevice::setCustomGapHandler(gapEvent);

  // Check the full bitmap, padding and clipped quiet border independently.
  findFixture();
  const SavedFrame normal = lastFrame;
  uint8_t highlight[kFrameBytes];
  for (const QrBounds &bounds : {qr, QrBounds{0, 0, 1}, QrBounds{249, 121, 1}, QrBounds{0, 0, 122}}) {
    invertOutsideQr(lastFrame.pixels, highlight, bounds);
    assertHighlightPixels(highlight, lastFrame.pixels, bounds, true);
  }
  assert(!memcmp(&normal, &lastFrame, sizeof(normal)));
  lastFrame.revision = kMaxRevision;
  const std::string maximumIdentity = identity();
  assert(maximumIdentity.size() <= 512 && maximumIdentity.find("\"find_tag\":true") != std::string::npos);

  // Only successfully refreshed pixels start the thirty-second interval.
  findFixture(); const SavedFrame before = lastFrame; const auto saved = preferences.values;
  const uint8_t slot = activeSlot; const uint32_t acceptedAt = millis();
  const std::string originalWire = findWire(); const std::string originalChallenge = challenge;
  preferences.writeAdvanceMs = 10000; panel::advance = 2000;
  command(originalWire); panel::advance = 0;
  assertFinding(true);
  assert(findStarted == acceptedAt + 2000 && findRemaining() == 30000);
  assert(challenge != originalChallenge && connectionAuthorized);
  assert(preferences.values == saved && activeSlot == slot && !memcmp(&before, &lastFrame, sizeof(before)));
  const unsigned highlightedRenders = panel::renders;
  testMillis += 10000; const uint32_t repeatedAt = millis();
  command(findWire()); assertFinding(true);
  assert(millis() == repeatedAt && findStarted == repeatedAt && findRemaining() == 30000);
  assert(panel::renders == highlightedRenders && preferences.values == saved);
  command(originalWire); assert(!strcmp(statusState, "error")); assertUnconfirmed();
  assert(findStarted == repeatedAt && panel::renders == highlightedRenders);
  testMillis = repeatedAt + 29999; serviceQrVisibility(millis());
  assert(findActive && findOnPanel && findRemaining() == 1 && panel::renders == highlightedRenders);
  ++testMillis; serviceQrVisibility(millis());
  assert(!findActive && !findOnPanel && !memcmp(panel::displayed, before.pixels, kFrameBytes));
  assertCommitted(); assert(preferences.values == saved);

  // Find neither reveals a hidden QR nor starts a fresh QR visibility window.
  findFixture(); expire(); serviceQrVisibility(millis());
  const uint32_t hiddenWindow = awakeWindow.started;
  command(findWire()); assertFinding(false);
  assert(!awakeWindow.armed && awakeWindow.started == hiddenWindow);
  expireFind(); serviceQrVisibility(millis()); assertHidden(lastFrame, qr); assertCommitted();

  // Menu can show and hide the QR while retaining the original Find timer.
  findFixture(); command(findWire()); const uint32_t menuFindStarted = findStarted;
  shortPress(); serviceQrVisibility(millis()); assertFinding(false);
  assert(findStarted == menuFindStarted);
  shortPress(); serviceQrVisibility(millis()); assertFinding(true);
  assert(findStarted == menuFindStarted);

  // A tap or QR deadline inside the initial refresh is corrected before status.
  findFixture(); panel::hook = pressDuringRefresh; command(findWire());
  assert(panel::attempts.size() == 2); assertFinding(false);
  findFixture(); panel::hook = expireQrDuringRefresh; command(findWire());
  assert(panel::attempts.size() == 2); assertFinding(false);
  // Find expiry inside a Menu refresh corrects the final panel to canonical.
  findFixture(); command(findWire()); shortPress(); panel::attempts.clear();
  panel::hook = expireFindDuringRefresh; serviceQrVisibility(millis());
  assert(panel::attempts.size() == 2 && !findActive && !findOnPanel);
  assertHidden(lastFrame, qr); assertCommitted();

  // Timer arithmetic and restoration survive millis wraparound.
  findFixture(); testMillis = UINT32_MAX - 15000; awakeWindow.begin(millis());
  command(findWire()); testMillis += 29999; serviceQrVisibility(millis());
  assert(findActive && findRemaining() == 1);
  ++testMillis; serviceQrVisibility(millis()); assert(!findOnPanel); assertCommitted();

  // Phone disconnection and reconnection do not cancel the physical highlight.
  findFixture(); command(findWire()); const unsigned connectedRenders = panel::renders;
  connections.onDisconnect(&server, &event); testMillis += 1000;
  connections.onConnect(&server, &event); serviceQrVisibility(millis());
  assertFinding(true); assert(!restoreScreen && panel::renders == connectedRenders);
  connections.onDisconnect(&server, &event); expireFind(); serviceQrVisibility(millis());
  assert(!connected && !findOnPanel); assertCommitted();

  // Failed initial refresh consumes the nonce and restores only after a retry.
  findFixture(); const std::string failedWire = findWire(); panel::failNext = true;
  command(failedWire); assert(!findActive && !screenIsLabel && qrRetryPending); assertUnconfirmed();
  const unsigned failedRenders = panel::renders;
  testMillis += 999; serviceQrVisibility(millis()); assert(panel::renders == failedRenders);
  ++testMillis; serviceQrVisibility(millis()); assertCommitted();
  command(failedWire); assert(!findActive && !strcmp(statusState, "error"));
  // Another successful recovery path must clear a pending UI retry too.
  findFixture(); panel::failNext = true; command(findWire()); assert(qrRetryPending);
  command(begin(1, lastFrame.sha256, &qr, false)); upload(lastFrame.pixels);
  assertCommitted(); assert(!qrRetryPending); command(findWire()); assertFinding(true);
  findFixture(); panel::failNext = true; command(findWire()); assert(qrRetryPending);
  connections.onDisconnect(&server, &event); connections.onConnect(&server, &event);
  assert(restoreScreen); restoreLabel();
  assertCommitted(); assert(!qrRetryPending); command(findWire()); assertFinding(true);
  // Restoration can fail repeatedly without issuing a false committed receipt.
  findFixture(); command(findWire()); expireFind(); panel::failNext = true;
  serviceQrVisibility(millis()); assert(!findActive && findOnPanel && qrRetryPending); assertUnconfirmed();
  const unsigned failedRestoreRenders = panel::renders;
  testMillis += 999; serviceQrVisibility(millis()); assert(panel::renders == failedRestoreRenders);
  ++testMillis; panel::failNext = true; serviceQrVisibility(millis()); assertUnconfirmed();
  testMillis += 1000; serviceQrVisibility(millis()); assert(!findOnPanel); assertCommitted();

  // Expiry restores the old canonical frame while data callbacks keep uploading.
  findFixture(); command(findWire());
  uint8_t changed[kFrameBytes]; memcpy(changed, lastFrame.pixels, kFrameBytes); changed[0] ^= 0x80;
  char changedHash[65]; hashFrame(changed, changedHash);
  command(begin(2, changedHash, &qr)); assert(receiving && findActive);
  hookUploadPixels = changed; expireFind(); panel::hook = appendDuringRefresh;
  serviceQrVisibility(millis());
  assert(receiving && receivedBytes == 1 && !findActive && !findOnPanel);
  assert(!memcmp(panel::displayed, lastFrame.pixels, kFrameBytes)); assertUnconfirmed();
  upload(changed, 1); assert(lastFrame.revision == 2); assertCommitted();

  // New and duplicate normal commits end Find before any candidate is rendered.
  for (bool duplicate : {false, true}) {
    findFixture(); command(findWire());
    const uint8_t *next = duplicate ? lastFrame.pixels : changed;
    command(begin(duplicate ? 1 : 2, duplicate ? lastFrame.sha256 : changedHash, &qr, false));
    panel::attempts.clear(); upload(next);
    assert(!findActive && !findOnPanel && panel::attempts.size() == 1);
    assert(!memcmp(panel::attempts.front().data(), next, kFrameBytes)); assertCommitted();
  }
  // A failed commit rolls back to the old canonical frame, then retries if needed.
  findFixture(); command(findWire()); const SavedFrame old = lastFrame;
  command(begin(2, changedHash, &qr)); panel::failuresLeft = 2; upload(changed);
  assert(!findActive && !screenIsLabel && lastFrame.revision == 1); assertUnconfirmed();
  serviceQrVisibility(millis()); assert(!memcmp(panel::displayed, old.pixels, kFrameBytes)); assertCommitted();

  // Physical setup cancels Find; closing setup restores an ordinary label.
  findFixture(); command(findWire()); openPairing();
  assert(pairingOpen && !findActive); assertUnconfirmed();
  pairingOpen = false; restoreLabel(); assert(!findOnPanel); assertCommitted();

  // Reject stale, malformed, unauthenticated and unbound geometry without refresh.
  rejectChangedField("signature", "\"0000000000000000000000000000000000000000000000000000000000000000\"");
  rejectChangedField("duration_ms", "29999");
  rejectChangedField("revision", "1.5");
  rejectChangedField("qr_x", "-1");
  rejectChangedField("qr_y", "null");
  rejectChangedField("qr_size", "0");
  rejectChangedField("qr_size", "true");
  rejectChangedField("sha256", "\"0000000000000000000000000000000000000000000000000000000000000000\"");
  findFixture(); const unsigned beforeWrongQr = panel::renders;
  command(findWire(QrBounds{171, 20, 70}));
  assert(!findActive && panel::renders == beforeWrongQr && !strcmp(statusState, "error"));
  findFixture(); lastQr.frameHash[0] ^= 1; command(findWire());
  assert(!findActive && !strcmp(statusState, "error"));
  findFixture(); cJSON *extra = cJSON_Parse(findWire().c_str());
  cJSON_AddBoolToObject(extra, "extra", true); command(serialized(extra));
  assert(!findActive && !strcmp(statusState, "error"));
  assert(testDeepSleepCalls == 0 && testControllerStatus == ESP_BT_CONTROLLER_STATUS_ENABLED);
  std::cout << "Find authentication, pixels, QR privacy, timers, reconnects, retries and normal commits passed\n";
  return 0;
}
