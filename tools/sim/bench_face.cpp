#include <M5Unified.h>
#include <string>
#include "ui_expression.h"
#include "ui_fx.h"


namespace battery_ui { int level() { return 87; } }
SimSerial Serial; SimEsp ESP; SimM5 M5;
static uint32_t g_ms = 100000; static uint32_t g_seed = 12345;
uint32_t millis() { return g_ms; } uint32_t micros() { return g_ms * 1000; }
uint32_t esp_random() { g_seed = g_seed * 1103515245 + 12345; return g_seed; }
static std::string g_name; static bool g_save = false;
namespace ui_fx {
void present(m5gfx::M5Canvas* cv) {
  if (!g_save) return;
  const int W = cv->width(), H = cv->height();
  FILE* f = fopen(("ex/" + g_name + ".ppm").c_str(), "wb");
  fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
    const uint16_t c = cv->readPixel(x, y);
    const uint8_t px[3] = {(uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31)};
    fwrite(px, 1, 3, f);
  }
  fclose(f);
}
bool active() { return false; }
}
#include <chrono>
int main() {
  ui_expression::begin();
  const Mood ms[] = {Mood::Idle, Mood::Speaking, Mood::Angry};
  for (int o = 0; o < 2; ++o) for (Mood m : ms) {
    ui_expression::setOrientation((Orient)(o ? 2 : 0));
    ui_expression::setMood(m);
    for (int k = 0; k < 30; ++k) { g_ms += 33; ui_expression::render(); }
    auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < 300; ++k) { g_ms += 33; ui_expression::render(); }
    double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 300;
    printf("o%d mood%d %.0f us/frame\n", o, (int)m, us);
  }
}
