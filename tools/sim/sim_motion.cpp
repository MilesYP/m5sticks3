// A8 甩碎（＋A4b 充電液體）模擬：連續一段「搖→翻→拼回…」，存圖到 .out/mo/，並照 ui_fx 的部分更新演算法算每段要送幾 % 的像素
#include <M5Unified.h>
#include <string>
#include <vector>
#include <math.h>
#include "ui_expression.h"
#include "ui_fx.h"
namespace battery_ui { int level() { return 45; } }
SimSerial Serial; SimEsp ESP; SimM5 M5;
static uint32_t g_ms = 100000; static uint32_t g_seed = 12345;
uint32_t millis() { return g_ms; } uint32_t micros() { return g_ms * 1000; }
uint32_t esp_random() { g_seed = g_seed * 1103515245 + 12345; return g_seed; }
static std::string g_name; static bool g_save = false;
static std::vector<uint16_t> g_sh; static double g_sum = 0, g_max = 0; static int g_n = 0; static bool g_meas = false;
double pushFrac(const uint16_t* cur, int W, int H) {
  if (g_sh.empty()) { g_sh.assign(cur, cur + W * H); return 1; }
  uint16_t* sh = g_sh.data(); long px = 0; const int GAP = 6; int y = 0;
  while (y < H) {
    if (memcmp(cur + y * W, sh + y * W, W * 2) == 0) { ++y; continue; }
    const int y0 = y; int yl = y, x0 = W, x1 = -1, gap = 0;
    for (; y < H; ++y) { const uint16_t* a = cur + y * W; const uint16_t* b = sh + y * W;
      if (memcmp(a, b, W * 2) == 0) { if (++gap > GAP) break; continue; }
      gap = 0; yl = y; int l = 0; while (a[l] == b[l]) ++l; int r = W - 1; while (a[r] == b[r]) --r; if (l < x0) x0 = l; if (r > x1) x1 = r; }
    px += (long)(x1 - x0 + 1) * (yl - y0 + 1);
    for (int yy = y0; yy <= yl; ++yy) memcpy(sh + yy * W + x0, cur + yy * W + x0, (x1 - x0 + 1) * 2);
    y = yl + 1;
  }
  return (double)px / (W * H);
}
namespace ui_fx {
void present(m5gfx::M5Canvas* cv) {
  const double f = pushFrac((const uint16_t*)cv->getBuffer(), 135, 240);
  if (g_meas) { g_sum += f; ++g_n; if (f > g_max) g_max = f; }
  if (!g_save) return;
  const int W = cv->width(), H = cv->height();
  FILE* fo = fopen(("mo/" + g_name + ".ppm").c_str(), "wb");
  fprintf(fo, "P6\n%d %d\n255\n", W, H);
  for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) { const uint16_t c = cv->readPixel(x, y);
    const uint8_t px[3] = {(uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31)}; fwrite(px, 1, 3, fo); }
  fclose(fo);
}
bool active() { return false; }
}
int g_fr = 0;
void meas(const char* t) { if (g_n) printf("  %-22s 部分更新 平均 %.0f%%、最多 %.0f%%\n", t, g_sum / g_n * 100, g_max * 100); g_sum = g_max = 0; g_n = 0; }
// gdeg：重力方向（0＝下、90＝右）；shake：g
void seg(const char* tag, int dur, float g0, float g1, float shake, int, int, Mood m = Mood::Idle) {
  for (int t = 0; t < dur; t += 21) {
    g_ms += 21;
    float mx = 0, my = 0;
    if (shake > 0) { const float ph = g_ms * 0.025f; mx = shake * sinf(ph) * 0.7f; my = shake * cosf(ph * 1.3f); }
    ui_expression::setMotion(mx, my);
    const float a = (g0 + (g1 - g0) * t / dur) * 3.14159f / 180;
    ui_expression::setGravity(sinf(a) - 0.6f * mx, cosf(a) - 0.6f * my);
    
    ui_expression::setMood(m);
    char nm[64]; snprintf(nm, sizeof nm, "%s_%04d", tag, g_fr++);
    g_name = nm; g_save = (g_fr % 2 == 0);
    ui_expression::render();
  }
}
int main(int argc, char** argv) {
  if (system("mkdir -p mo")) {}
  ui_expression::begin();
  const int o = argc > 1 ? atoi(argv[1]) : 0;
  ui_expression::setOrientation((Orient)o);
  const char* on[] = {"P", "AL", "AR"}; std::string T = on[o];
  printf("%s\n", on[o]);
  seg((T + "a_idle").c_str(), 500, 0, 0, 0, 0, 45);
  g_meas = true; seg((T + "b_shake").c_str(), 1500, 0, 0, 1.6f, 0, 45, Mood::Scared); meas("搖（碎片）");
  seg((T + "c_flip").c_str(), 1000, 180, 180, 1.4f, 0, 45, Mood::Scared); meas("翻過來搖");
  seg((T + "d_back").c_str(), 1200, 0, 0, 0, 0, 45); meas("停手拼回");
  g_meas = false;
}
