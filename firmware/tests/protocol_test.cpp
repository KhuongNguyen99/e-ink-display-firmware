#include "../countwell_label/ble_protocol.h"
#include "../countwell_label/ble_auth.h"
#include "../countwell_label/ble_name.h"
#include <assert.h>
#include <algorithm>
#include <string>
#include <vector>
#include <iostream>

using namespace countwell;
const char *box = "01234567-89ab-4cde-8123-456789abcdef";
const char *hash = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

ControlMessage::Result feed(ControlMessage &message, const std::string &text, size_t stride) {
  auto result = ControlMessage::Result::More;
  for (size_t i = 0; i < text.size(); i += stride) {
    result = message.push(reinterpret_cast<const uint8_t *>(text.data() + i), std::min(stride, text.size() - i));
    if (i + stride < text.size()) assert(result == ControlMessage::Result::More);
  }
  return result;
}
int main() {
  uint32_t value = 9;
  assert(!decimal("", 4000, value) && value == 9);
  assert(decimal("2147483647", kMaxRevision, value));
  assert(!decimal("2147483648", kMaxRevision, value));
  assert(uuid(box) && !uuid("01234567-89ab-4cde-8123-456789abcdeg"));
  assert(hex(hash, 64) && !hex("ab", 64));

  uint8_t key[32];
  const char *keyHex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
  assert(decodeBleKey(keyHex, key));
  for (unsigned i = 0; i < 32; ++i) assert(key[i] == i);
  assert(!decodeBleKey("not-a-key", key));
  for (unsigned i = 0; i < 32; ++i) assert(key[i] == i);
  char transcript[kTranscriptBytes];
  const char *device = "cw-18cedc8fcba4", *generation = "0123456789abcdef";
  const char *challenge = "000102030405060708090a0b0c0d0e0f";
  const std::string binding = std::string(device) + "|" + generation + "|" + box;
  assert(pairedTranscript(transcript, sizeof(transcript), device, generation, box));
  assert(std::string(transcript) == "CW2|paired|" + binding);
  assert(beginTranscript(transcript, sizeof(transcript), device, generation, box, "ssd1680", 2147483647U, hash, challenge));
  assert(std::string(transcript) == "CW2|begin|" + binding + "|ssd1680|2147483647|" + hash + "|" + challenge);
  assert(committedTranscript(transcript, sizeof(transcript), device, generation, box, "ssd1680", 2147483647U, hash));
  assert(std::string(transcript) == "CW2|committed|" + binding + "|ssd1680|2147483647|" + hash);
  assert(beginTranscript(transcript, sizeof(transcript), device, generation, box, "ssd1680", 2147483647U,
                         hash, challenge, hash));
  assert(std::string(transcript) == "CW2|begin|" + binding + "|ssd1680|2147483647|" + hash + "|" + challenge + "|" + hash);
  assert(committedTranscript(transcript, sizeof(transcript), device, generation, box, "ssd1680", 2147483647U, hash, hash));
  assert(std::string(transcript) == "CW2|committed|" + binding + "|ssd1680|2147483647|" + hash + "|" + hash);
  assert(!beginTranscript(transcript, 24, device, generation, box, "ssd1680", 1, hash, challenge, hash));
  assert(!pairedTranscript(transcript, 10, device, generation, box));

  uint8_t scan[31] = {};
  assert(validBleName("E-ink Widget"));
  assert(scanResponseName("E-ink Widget", scan) == 14 && scan[0] == 13 && scan[1] == 0x09);
  assert(!memcmp(scan + 2, "E-ink Widget", 12));
  const std::string unicode = "E-ink " + std::string(21, 'x') + "\xf0\x9f\x93\xa6" + "z";
  assert(validBleName(unicode.c_str()));
  assert(scanResponseName(unicode.c_str(), scan) == 29 && scan[0] == 28 && scan[1] == 0x08);
  assert(!memcmp(scan + 2, unicode.data(), 27));
  assert(!validBleName("Widget") && !validBleName("E-ink "));
  assert(!validBleName("E-ink \xc0\xaf") && !validBleName("E-ink \xed\xa0\x80"));
  assert(!validBleName("E-ink \xf0\x9f\x93") && !validBleName("E-ink A\nB"));
  assert(validBleName(("E-ink " + std::string(242, 'a')).c_str()));
  assert(!validBleName(("E-ink " + std::string(243, 'a')).c_str()));

  const std::string json = "{\"op\":\"begin\",\"revision\":2147483647,\"box_id\":\"" + std::string(box) + "\"}";
  for (size_t stride : {1U, 7U, 18U, 20U}) {
    ControlMessage message;
    assert(feed(message, json + "\n", stride) == ControlMessage::Result::Complete);
    assert(message.used == json.size() && json == message.bytes);
    message.reset();
    assert(feed(message, "{\"op\":\"commit\"}\n", stride) == ControlMessage::Result::Complete);
  }
  ControlMessage message;
  const std::string maximum(kControlBytes, 'a');
  assert(feed(message, maximum, 20) == ControlMessage::Result::More);
  const uint8_t newline = '\n';
  assert(message.push(&newline, 1) == ControlMessage::Result::Complete);
  assert(message.used == kControlBytes && strlen(message.bytes) == kControlBytes);
  message.reset();
  assert(feed(message, maximum, 20) == ControlMessage::Result::More);
  const uint8_t extra = 'b';
  assert(message.push(&extra, 1) == ControlMessage::Result::Invalid);
  // Overflow must discard even a syntactically valid tail through its newline.
  const std::string tail = "{\"op\":\"commit\"}\n";
  assert(message.push(reinterpret_cast<const uint8_t *>(tail.data()), tail.size()) == ControlMessage::Result::Invalid);
  assert(message.used == 0 && !message.discarding);
  assert(feed(message, tail, 20) == ControlMessage::Result::Complete);
  message.reset();
  const uint8_t nul[] = {'{', 0, '}'};
  assert(message.push(nul, sizeof(nul)) == ControlMessage::Result::Invalid);
  assert(message.push(&newline, 1) == ControlMessage::Result::Invalid);
  assert(message.used == 0);
  const std::string twoCommands = "{}\n{}\n";
  assert(message.push(reinterpret_cast<const uint8_t *>(twoCommands.data()), twoCommands.size()) == ControlMessage::Result::Invalid);
  assert(message.used == 0);
  uint8_t oversize[21] = {};
  assert(message.push(oversize, sizeof(oversize)) == ControlMessage::Result::Invalid);
  assert(message.push(&newline, 1) == ControlMessage::Result::Invalid);
  assert(message.push(&newline, 1) == ControlMessage::Result::Invalid);

  // Every supported fragment size reconstructs the full binary framebuffer,
  // including zero bytes, while rejecting overlapping/out-of-order packets.
  std::vector<uint8_t> source(kFrameBytes), target(kFrameBytes + 2, 0xab);
  for (size_t i = 0; i < source.size(); ++i) source[i] = i % 256;
  for (size_t stride : {1U, 7U, 18U}) {
    size_t used = 0;
    while (used < source.size()) {
      uint8_t packet[20]; packet[0] = used & 0xff; packet[1] = used >> 8;
      const size_t size = std::min(stride, source.size() - used);
      memcpy(packet + 2, source.data() + used, size);
      const size_t before = used;
      assert(appendBleFrame(target.data() + 1, used, packet, size + 2));
      assert(used == before + size);
      assert(!appendBleFrame(target.data() + 1, used, packet, size + 2));
      assert(used == before + size);
    }
    assert(!memcmp(target.data() + 1, source.data(), kFrameBytes));
    assert(target.front() == 0xab && target.back() == 0xab);
    uint8_t beyond[] = {uint8_t(kFrameBytes & 0xff), uint8_t(kFrameBytes >> 8), 1};
    assert(!appendBleFrame(target.data() + 1, used, beyond, sizeof(beyond)));
  }
  size_t used = 0;
  uint8_t invalid[] = {1, 0, 1};
  assert(!appendBleFrame(target.data() + 1, used, invalid, sizeof(invalid)) && used == 0);
  assert(!appendBleFrame(target.data() + 1, used, invalid, 2) && used == 0);
  assert(!appendBleFrame(target.data() + 1, used, oversize, sizeof(oversize)) && used == 0);
  used = 3999; invalid[0] = used & 0xff; invalid[1] = used >> 8;
  uint8_t overflow[] = {invalid[0], invalid[1], 1, 2};
  assert(!appendBleFrame(target.data() + 1, used, overflow, sizeof(overflow)) && used == 3999);
  assert(target.front() == 0xab && target.back() == 0xab);

  uint8_t frame[kFrameBytes] = {};
  blackPixel(frame, 0, 0); assert(frame[3984] == 0x80);
  blackPixel(frame, 249, 121); assert(frame[15] == 0x40);
  for (unsigned y = 0; y < kHeight; ++y) for (unsigned x = 0; x < kWidth; ++x) blackPixel(frame, x, y);
  assert(paddingValid(frame));
  for (size_t i = 0; i < kFrameBytes; ++i) assert(frame[i] == (i % 16 == 15 ? 0xc0 : 0xff));
  blackPixel(frame, 250, 0); blackPixel(frame, 0, 122); assert(paddingValid(frame));
  frame[15] |= 1; assert(!paddingValid(frame));
  assert(revisionDecision(false, 0, "", 1, hash) == RevisionDecision::Apply);
  assert(revisionDecision(true, 3, hash, 3, hash) == RevisionDecision::Duplicate);
  assert(revisionDecision(true, 3, hash, 3, "other") == RevisionDecision::Conflict);
  assert(revisionDecision(true, 3, hash, 2, hash) == RevisionDecision::Conflict);
  assert(revisionDecision(true, 3, hash, 4, hash) == RevisionDecision::Apply);
  assert(revisionDecision(false, 0, "", 0, hash) == RevisionDecision::Conflict);
  std::cout << "BLE canonical authorization, raw key decoding, framing, bounds, transfer ordering, revision and framebuffer tests passed\n";
}
