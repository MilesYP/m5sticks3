// 資訊頁模擬：真的 ui_info.cpp／ui_info_eva.cpp／info_data.cpp ＋ 真的 M5GFX 繪圖核心，其他模組用假值
#include <M5Unified.h>
#include <WiFi.h>
#include <string>
#include <vector>
#include "audio_io.h"
#include "battery_ui.h"
#include "imu_pet.h"
#include "info_data.h"
#include "input_hal.h"
#include "mode_manager.h"
#include "net_hub.h"
#include "ui_expression.h"
#include "ui_fx.h"
#include "ui_info.h"

#include "voice_turn.h"

SimSerial Serial;
SimEsp ESP;
SimWiFi WiFi;
SimM5 M5;
static uint32_t g_ms = 100000;
uint32_t millis() { return g_ms; }
uint32_t micros() { return g_ms * 1000; }
float temperatureRead() { return 47.3f; }
uint32_t getCpuFrequencyMhz() { return 240; }
void configTzTime(const char*, const char*, const char*) {}

static Orient g_or = Orient::Portrait;
static bool g_eva = false, g_link = true;
static std::string g_name;
static int g_reports = 0;
void infoSimReport(const char* what, const char* tile, const char* s, int x, int y, int w, int h) {
  printf("  !! %s [%s] 「%s」 x=%d y=%d w=%d h=%d\n", what, tile, s, x, y, w, h);
  ++g_reports;
}
namespace audio_io { const char* buildTag() { return "26-a5b"; } }
static int g_bat = 87;
namespace battery_ui { int level() { return g_bat; } int voltageMv() { return 4012; } bool charging() { return false; } bool externalPower() { return false; } }
namespace imu_pet { bool tilt(float* r, float* u, float* z) { *r = 0.1f; *u = 0.9f; *z = 0.1f; return true; } }
namespace input_hal { GroveDevice device() { return GroveDevice::None; } const char* deviceName() { return "None"; } }
namespace mode_manager { Mode current() { return Mode::Desktop; } }
namespace net_hub { bool wifiUp() { return true; } bool linkUp() { return g_link; } uint32_t lastRttMs() { return 340; } uint32_t pingIntervalSeconds() { return 20; } }
namespace ui_expression { Orient orientation() { return g_or; } const char* orientName(Orient o) { return o == Orient::Portrait ? "直式" : o == Orient::LandALeft ? "橫A左" : "橫A右"; } }

namespace voice_turn { const char* stateName() { return "IDLE"; } }
namespace ui_fx {
void present(m5gfx::M5Canvas* cv) {
  const int W = cv->width(), H = cv->height();
  std::string fn = "out/" + g_name + ".ppm";
  FILE* f = fopen(fn.c_str(), "wb");
  fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      const uint16_t c = cv->readPixel(x, y);
      const uint8_t px[3] = {(uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31)};
      fwrite(px, 1, 3, f);
    }
  fclose(f);
}
}

static void feed(int variant) {
  const time_t at = 1795167000;   // 2026-11-20 18:10 台北
  if (variant == 0) return;   // 沒資料
  if (variant == 1) {
    info_data::onUsd(0.42f, 2, 8.37f, at);
    info_data::onWeather("rain", 15, 73, 30, "泰山", "短暫雨", at);
    const char* t[] = {"颱風外圍環流影響 北部山區防豪雨", "立院三讀通過AI基本法", "台積電法說會 第四季營收展望優於預期",
                       "美國聯準會維持利率不變", "新北捷運環狀線北環段明年通車", "高中職明起寒假前最後一次段考"};
    for (int i = 0; i < 6; ++i) info_data::onNewsItem(i, 6, t[i], at);
  } else {   // 極端值
    g_bat = 100;
    info_data::onUsd(1.93f, 2, 123.45f, at);
    info_data::onWeather("part", -5, 100, 100, "中正", "晴時多雲偶陣雨或雷雨午後", at);
    info_data::onNewsItem(0, 1, "一二三四五六七八九十一二三四五六七八九十一二三四五六", at);
  }
}

int main() {
  system("mkdir -p out");
  ui_info::begin();
  ui_info::loop();
  for (int k = 0; k < 3000; ++k) { g_ms += (k % 211 == 0) ? 33 : 12 + (int)(8 * (1 + __builtin_sin(k / 90.0))); ui_info::loop(); }
  const char* vn[] = {"empty", "full", "edge"};
  const char* on[] = {"P", "AL", "AR"};
  for (int v = 0; v < 3; ++v) {
    feed(v);
    for (int eva = 0; eva < 2; ++eva)
      for (int o = 0; o < 3; ++o)
        for (int link = 1; link >= 0; --link) {
          if (v == 0 && !link) continue;
          g_eva = eva; g_or = (Orient)o; g_link = link;
          g_name = std::string(eva ? "q1" : "q0") + "_" + vn[v] + "_" + on[o] + (link ? "" : "_off");
          printf("%s\n", g_name.c_str());
          ui_info::setShown(false, "sim");
          ui_info::setShown(true, "sim");
          g_ms += 1000;
          ui_info::render();
        }
  }
  // 格式化單元測試
  char b[48];
  info_data::Usd u; u.valid = true; u.today = 0.42f; u.cap = 2; u.left = 8.37f;
  info_data::formatUsd(b, sizeof b, u, false); printf("短 %s\n", b);
  info_data::formatUsd(b, sizeof b, u, true); printf("長 %s\n", b);
  u.left = -1; info_data::formatUsd(b, sizeof b, u, true); printf("無餘額 %s\n", b);
  info_data::formatAt(b, sizeof b, 1795167000); printf("時間 %s\n", b);
  printf("超出磚塊回報 %d 次\n", g_reports);
  return 0;
}
