#include "../countwell_label/ble_json.h"
#include "../countwell_label/protocol.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace countwell;

// The pinned cJSON number printer calls sscanf. Any regression to that path
// fails this test even if the resulting JSON would otherwise look correct.
extern "C" int __wrap_sscanf(const char *, const char *, ...) {
  abort();
}
extern "C" int __wrap___isoc99_sscanf(const char *, const char *, ...) {
  abort();
}

int main() {
  // Prove the guard intercepts cJSON's real numeric printing path. Some libc
  // headers redirect sscanf to another symbol, which can bypass a macro hook.
  const pid_t child = fork();
  assert(child >= 0);
  if (child == 0) {
    cJSON *number = cJSON_CreateNumber(42);
    char *wire = cJSON_PrintUnformatted(number);
    cJSON_free(wire); cJSON_Delete(number);
    _exit(0);
  }
  int childStatus = 0;
  assert(waitpid(child, &childStatus, 0) == child);
  assert(WIFSIGNALED(childStatus) && WTERMSIG(childStatus) == SIGABRT);
  const uint32_t values[] = {0, 1, 2, 9, 10, 99, 100, 4000, kMaxRevision, UINT32_MAX};
  for (const uint32_t value : values) {
    cJSON *json = cJSON_CreateObject();
    cJSON *token = addJsonUint32(json, "value", value);
    assert(cJSON_IsRaw(token));
    char *wire = cJSON_PrintUnformatted(json);
    assert(wire);
    const std::string expected = "{\"value\":" + std::to_string(value) + "}";
    assert(expected == wire);
    cJSON *decoded = cJSON_Parse(wire);
    const cJSON *number = cJSON_GetObjectItemCaseSensitive(decoded, "value");
    assert(cJSON_IsNumber(number) && number->valuedouble == double(value));
    cJSON_Delete(decoded); cJSON_free(wire); cJSON_Delete(json);
  }
  // Exercise the signed receipt shape through the real pinned serializer.
  // Strings must remain escaped strings alongside the unquoted integer fields.
  for (const char *state : {"paired", "committed"}) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "state", state);
    cJSON_AddStringToObject(json, "signature", "0123456789abcdef");
    cJSON_AddStringToObject(json, "error", "quote \" and slash \\");
    addJsonUint32(json, "protocol", 2);
    addJsonUint32(json, "revision", kMaxRevision);
    addJsonUint32(json, "received_bytes", kFrameBytes);
    char *wire = cJSON_PrintUnformatted(json);
    assert(wire);
    cJSON *decoded = cJSON_Parse(wire);
    assert(decoded && cJSON_GetArraySize(decoded) == 6);
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(decoded, "state")->valuestring, state));
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(decoded, "signature")->valuestring, "0123456789abcdef"));
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(decoded, "error")->valuestring, "quote \" and slash \\"));
    for (const char *field : {"protocol", "revision", "received_bytes"}) {
      assert(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(decoded, field)));
    }
    cJSON_Delete(decoded); cJSON_free(wire); cJSON_Delete(json);
  }
  std::cout << "Pinned cJSON BLE integer bounds, numeric wire types and receipt serialization passed\n";
}
