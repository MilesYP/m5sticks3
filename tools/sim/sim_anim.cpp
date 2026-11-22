#include <M5Unified.h>
#include <string>
#include "ui_expression.h"
#include "ui_fx.h"


namespace battery_ui { int level() { return 87; } }
namespace power_key { bool rootMode() { return false; } }   // P2：狀態列 ROOT 標籤
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
  if (system("mkdir -p an")) {}
  ui_expression::begin();
  ui_expression::setOrientation((Orient)0);
  ui_expression::setMood(Mood::Idle);
  for (int k = 0; k < 90; ++k) { g_ms += 33; g_save = false; ui_expression::render(); }
  ui_expression::setMood(Mood::Scared);
  for (int k = 0; k < 40; ++k) { g_ms += 33; g_save = true; g_name = "../an/s_" + std::to_string(k); ui_expression::render(); }
  ui_expression::setMood(Mood::Happy);
  for (int k = 0; k < 150; ++k) { g_ms += 33; g_save = true; g_name = "../an/h_" + std::to_string(k); ui_expression::render(); }
  printf("done\n");
}
