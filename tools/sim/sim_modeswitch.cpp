#include <M5Unified.h>
#include <string>
#include "ui_expression.h"
#include "mode_manager.h"
#include "ui_fx.h"
SimSerial Serial; SimEsp ESP; SimM5 M5;
static uint32_t g_ms = 100000;
uint32_t millis() { return g_ms; }
uint32_t micros() { return g_ms * 1000; }
static Orient g_o = Orient::Portrait;
namespace ui_expression { Orient orientation() { return g_o; } }
namespace mode_manager { bool currentReady(const char** s) { if (s) *s = "開藍牙…"; return false; } }
namespace ui_fx { void invalidate() {} void cancel() {} }
#include "ui_modeswitch.cpp"   // 直接 include 才看得到內部畫布 s_cv
static void dump(const char* name) {
  auto& cv = s_cv; const int W = cv.width(), H = cv.height();
  FILE* f = fopen(name, "wb"); fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) { const uint16_t c = cv.readPixel(x, y);
    const uint8_t px[3] = {(uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31)}; fwrite(px, 1, 3, f); }
  fclose(f);
}
int main() {
  ui_modeswitch::begin();
  const Mode ms[] = {Mode::Desktop, Mode::HidAgent, Mode::RfTool, Mode::AirMouse};
  const char* nm[] = {"ms_voice.ppm", "ms_hid.ppm", "ms_rf.ppm", "ms_air.ppm"};
  for (int i = 0; i < 4; ++i) { ui_modeswitch::start(ms[i]); ui_modeswitch::ready(); g_ms += 300; ui_modeswitch::render(); dump(nm[i]); }
  g_o = Orient::LandARight; s_cv.setRotation(0);
  ui_modeswitch::start(Mode::AirMouse); dump("ms_air_land.ppm");
  puts("done");
}
