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
int main() {
  if (system("mkdir -p lb")) {}
  ui_expression::begin();
  ui_expression::setOrientation((Orient)0);
  ui_expression::setMood(Mood::Speaking);
  ui_expression::convBegin(); ui_expression::convText(false, "明天會下雨嗎？"); ui_expression::convText(true, "泰山明天短暫雨，降雨機率三成，出門帶把傘比較保險。");
  for (int k = 0; k < 60; ++k) { g_ms += 33; g_save = (k == 59); g_name = "../lb/conv"; ui_expression::render(); }
  ui_expression::convEnd();
  for (int k = 0; k < 200; ++k) { g_ms += 33; g_save = false; ui_expression::render(); }
  ui_expression::setMood(Mood::Idle);
  for (int mode = 1; mode <= 2; ++mode) {
    ui_expression::setLowBatteryBlink(mode);
    for (int k = 0; k < 60; ++k) { g_ms += 33; g_save = (k == 40 || k == 55); g_name = "../lb/m" + std::to_string(mode) + "_" + std::to_string(k); ui_expression::render(); }
  }
  printf("done\n");
}
