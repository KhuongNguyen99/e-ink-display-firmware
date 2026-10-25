#pragma once
#include <stdint.h>
#include <stddef.h>
constexpr int HIGH = 1, LOW = 0, OUTPUT = 1, INPUT = 0;
void pinMode(uint8_t, int);
void digitalWrite(uint8_t, int);
int digitalRead(uint8_t);
uint32_t millis();
void delay(uint32_t);
struct TestSerial { void printf(const char *, ...) {} };
using SerialMock = TestSerial;
extern TestSerial Serial;
