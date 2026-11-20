#pragma once
// 模擬標頭：只放資訊頁用到的 Arduino API（結構跟真的一樣：全域 RX/TX 常數、Serial、ESP）
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdarg.h>
#include <algorithm>
static const uint8_t TX = 43;
static const uint8_t RX = 44;
struct SimSerial {
  int printf(const char* f, ...) { va_list a; va_start(a, f); int n = vprintf(f, a); va_end(a); return n; }
  void println(const char* s) { puts(s); }
  size_t write(const uint8_t* d, size_t n) { return fwrite(d, 1, n, stdout); }
};
extern SimSerial Serial;
uint32_t millis();
uint32_t micros();
struct SimEsp {
  uint32_t getFreeHeap() { return 150000 + (millis() / 1000 * 7919) % 40000; }
  uint32_t getMinFreeHeap() { return 150000; }
  uint32_t getHeapSize() { return 320000; }
  uint32_t getFreePsram() { return 2700000; }
  uint32_t getPsramSize() { return 8 * 1024 * 1024; }
};
extern SimEsp ESP;
float temperatureRead();
uint32_t getCpuFrequencyMhz();
void configTzTime(const char*, const char*, const char*);
template <class T> T constrain(T v, T lo, T hi) { return v < lo ? lo : v > hi ? hi : v; }
uint32_t esp_random();
