#include "ui_info.h"

#include <M5Unified.h>
#include <WiFi.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "audio_io.h"
#include "battery_ui.h"
#include "config.h"
#include "ui_fx.h"
#include "imu_pet.h"
#include "input_hal.h"
#include "mode_manager.h"
#include "net_hub.h"
#include "ui_expression.h"
#include "voice_turn.h"

#ifdef INFO_SIM
void infoSimReport(const char* what, const char* tile, const char* s, int x, int y, int w, int h);
#endif

namespace {

M5Canvas s_cv(&M5.Display);
bool s_ready = false;
bool s_shown = false;
bool s_dirty = true;
long s_last_sec = -1;
Orient s_drawn_orient = Orient::Portrait;

// ── 合成波色盤（底色與表情頁相同；強調色取自表情頁的情緒色）──
struct C3 { uint8_t r, g, b; };
constexpr C3 K_BG   = {0x0A, 0x04, 0x20};
constexpr C3 K_TILE = {0x1C, 0x12, 0x44};
constexpr C3 K_DIM  = {0x7A, 0x6A, 0xA8};
constexpr C3 K_TXT  = {0xF2, 0xE9, 0xFF};
constexpr C3 K_PINK = {0xFF, 0x2B, 0xD6};
constexpr C3 K_CYAN = {0x00, 0xF0, 0xFF};
constexpr C3 K_YEL  = {0xFF, 0xD1, 0x66};
constexpr C3 K_PUR  = {0xB3, 0x88, 0xEB};
constexpr C3 K_GRN  = {0x39, 0xFF, 0x88};
constexpr C3 K_ORG  = {0xFF, 0x9F, 0x1C};
constexpr C3 K_BLUE = {0x4D, 0x8D, 0xFF};
constexpr C3 K_VIO  = {0x9D, 0x5C, 0xFF};   // 深紫（跟 MEM 的淡紫區分）
constexpr C3 K_RED  = {0xFF, 0x4D, 0x6D};
inline uint16_t col(const C3& c) { return s_cv.color565(c.r, c.g, c.b); }
inline uint16_t mixCol(const C3& a, const C3& b, float t) {
  return s_cv.color565((uint8_t)(a.r + (b.r - a.r) * t), (uint8_t)(a.g + (b.g - a.g) * t), (uint8_t)(a.b + (b.b - a.b) * t));
}

// ── 版面（實際像素；直式 135×240，橫式 240×135、A 鍵在右；A 鍵在左＝左右鏡像）──
struct Tile { const char* name; int16_t x, y, w, h; };
enum { T_CLOCK, T_STAT, T_IMU, T_WX, T_CHIP, T_MEM, T_NET, T_MOD, T_NEWS, T_N };
const Tile kPortrait[T_N] = {
  {"clock", 2, 2, 89, 62},   {"stat", 93, 2, 40, 62},
  {"imu", 2, 66, 32, 34},    {"wx", 36, 66, 97, 34},
  {"chip", 2, 102, 65, 44},  {"mem", 69, 102, 64, 44},
  {"net", 2, 148, 65, 44},   {"mod", 69, 148, 64, 44},
  {"news", 2, 194, 131, 44},
};
const Tile kLandRight[T_N] = {
  {"clock", 2, 2, 148, 44},  {"stat", 152, 2, 86, 30},
  {"imu", 2, 48, 34, 34},    {"wx", 38, 48, 112, 34},
  {"chip", 2, 84, 73, 49},   {"mem", 77, 84, 73, 49},
  {"net", 152, 34, 86, 44},  {"mod", 152, 80, 86, 24},
  {"news", 152, 106, 86, 27},
};
Tile s_t[T_N];
bool s_land = false;
constexpr int PAD = 3;
// 小字（11-20 Miles 兩輪實機）：英文字母用 Font0（細，好認）；數字與數字符號用 efontTW_10_b（粗體，對比清楚）。
// Font0 字母 6px 寬、8px 高；efontTW_10_b 數字 5px 寬、10px 高 → 一行 10px，Font0 往下 1px 讓底部對齊。
const lgfx::IFont* const THIN = &fonts::Font0;
const lgfx::IFont* const BOLD = &fonts::efontTW_10_b;
constexpr int THIN_DY = 1;
constexpr int ROW1 = 12, ROW2 = 22, ROW3 = 32;   // 標題下面三行的 y（磚塊內）

void layout(Orient o) {
  s_land = (o != Orient::Portrait);
  const Tile* src = s_land ? kLandRight : kPortrait;
  const int W = s_land ? 240 : 135;
  for (int i = 0; i < T_N; ++i) {
    s_t[i] = src[i];
    if (o == Orient::LandALeft) s_t[i].x = W - src[i].x - src[i].w;   // A 鍵側在左 → 鏡像
  }
}

// ── 文字：一律在磚塊內；模擬時檢查有沒有超出 ──
#ifdef INFO_SIM
void simCheck(int ti, const char* s, int x, int y, int w, int h) {
  const Tile& t = s_t[ti];
  if (x < t.x + 1 || x + w > t.x + t.w - 1 || y < t.y || y + h > t.y + t.h)
    infoSimReport("超出磚塊", t.name, s, x, y, w, h);
}
#endif

// 指定字型的一段字（大時鐘、中文標題）
void txtF(int ti, int dx, int dy, const char* s, const C3& c, const lgfx::IFont* f, bool right = false) {
  const Tile& t = s_t[ti];
  s_cv.setFont(f);
  const int w = s_cv.textWidth(s);
  const int x = right ? t.x + t.w - PAD - w : t.x + PAD + dx;
  const int y = t.y + dy;
#ifdef INFO_SIM
  simCheck(ti, s, x, y, w, s_cv.fontHeight());
#endif
  s_cv.setTextColor(col(c));
  s_cv.drawString(s, x, y);
}

// 小字：數字（0-9 . : / % - +）粗體、其餘（字母、空白）細體，逐段換字型
inline bool numChar(char ch) { return (ch >= '0' && ch <= '9') || ch == '.' || ch == ':' || ch == '/' || ch == '%' || ch == '-' || ch == '+'; }
int mixedWidth(const char* s) {
  int w = 0;
  char run[32];
  for (const char* p = s; *p;) {
    const bool num = numChar(*p);
    size_t n = 0;
    while (p[n] && numChar(p[n]) == num && n < sizeof(run) - 1) { run[n] = p[n]; ++n; }
    run[n] = '\0';
    s_cv.setFont(num ? BOLD : THIN);
    w += s_cv.textWidth(run);
    p += n;
  }
  return w;
}
void txt(int ti, int dx, int dy, const char* s, const C3& c, bool right = false) {
  const Tile& t = s_t[ti];
  const int w = mixedWidth(s);
  int x = right ? t.x + t.w - PAD - w : t.x + PAD + dx;
  const int y = t.y + dy;
#ifdef INFO_SIM
  simCheck(ti, s, x, y, w, 10);
#endif
  s_cv.setTextColor(col(c));
  char run[32];
  for (const char* p = s; *p;) {
    const bool num = numChar(*p);
    size_t n = 0;
    while (p[n] && numChar(p[n]) == num && n < sizeof(run) - 1) { run[n] = p[n]; ++n; }
    run[n] = '\0';
    s_cv.setFont(num ? BOLD : THIN);
    x += s_cv.drawString(run, x, num ? y : y + THIN_DY);
    p += n;
  }
}
void header(int ti, const char* s, const C3& c) { txt(ti, 0, 1, s, c); }

// 各塊的主色（標題字與外框）；電量塊跟著電量色
const C3& batColor(int lv) { return lv < 0 ? K_DIM : lv >= 60 ? K_GRN : lv >= 20 ? K_YEL : K_RED; }
C3 tileAccent(int i) {
  switch (i) {
    case T_CLOCK: return K_TXT;
    case T_STAT:  return batColor(battery_ui::level());
    case T_IMU:   return K_YEL;
    case T_WX:    return K_CYAN;
    case T_CHIP:  return K_BLUE;
    case T_MEM:   return K_PUR;
    case T_NET:   return K_VIO;
    case T_MOD:   return K_GRN;
    default:      return K_ORG;    // 快訊
  }
}

// ── 歷史資料 ──
uint8_t  s_loop_hist[cfg::INFO_HIST_SEC];    // 每秒主迴圈最長一圈（ms，封頂 255）
int      s_loop_pos = 0, s_loop_n = 0;
uint32_t s_loop_sec_max_us = 0, s_loop_last_us = 0, s_loop_sec_t0 = 0;
uint8_t  s_loop_now_ms = 0;
uint32_t s_heap_hist[cfg::INFO_HEAP_BARS];
int      s_heap_pos = 0, s_heap_n = 0;
uint32_t s_heap_t = 0;

bool s_ntp_started = false;
bool s_synced = false;

bool synced() {
  if (s_synced) return true;
  if (!s_ntp_started) return false;
  if (time(nullptr) > 1735689600) {   // 2025-01-01 之後＝已對到時間
    s_synced = true;
    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    Serial.printf("[time] 已對時：%04d-%02d-%02d %02d:%02d:%02d（台北）\n", lt.tm_year + 1900, lt.tm_mon + 1,
                  lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
  }
  return s_synced;
}

void sampleLoop() {
  const uint32_t us = micros();
  if (s_loop_last_us) {
    const uint32_t gap = us - s_loop_last_us;
    if (gap > s_loop_sec_max_us) s_loop_sec_max_us = gap;
  }
  s_loop_last_us = us;
  const uint32_t now = millis();
  if (now - s_loop_sec_t0 >= 1000) {
    s_loop_sec_t0 = now;
    const uint32_t ms = (s_loop_sec_max_us + 500) / 1000;
    s_loop_now_ms = ms > 255 ? 255 : (uint8_t)ms;
    s_loop_hist[s_loop_pos] = s_loop_now_ms;
    s_loop_pos = (s_loop_pos + 1) % cfg::INFO_HIST_SEC;
    if (s_loop_n < cfg::INFO_HIST_SEC) ++s_loop_n;
    s_loop_sec_max_us = 0;
  }
  if (now - s_heap_t >= cfg::INFO_HEAP_EVERY_MS || s_heap_n == 0) {
    s_heap_t = now;
    s_heap_hist[s_heap_pos] = ESP.getFreeHeap();
    s_heap_pos = (s_heap_pos + 1) % cfg::INFO_HEAP_BARS;
    if (s_heap_n < cfg::INFO_HEAP_BARS) ++s_heap_n;
  }
}

// ── 小圖示（9×9，中心 cx,cy）──
enum { IC_BT, IC_RF, IC_NRF, IC_IR, IC_USB, IC_MOUSE, IC_N };
bool iconActive(int i) {
  const Mode m = mode_manager::current();
  switch (i) {
    case IC_BT: case IC_MOUSE: return m == Mode::AirMouse;   // BLE 滑鼠（鐵則二：只有模式 4 開藍牙）
    case IC_RF: case IC_NRF:   return m == Mode::RfTool;     // RF Pack SPI（Desktop 不開，不為了顯示去開）
    case IC_USB:               return m == Mode::HidAgent;   // USB HID
    default:                   return false;                 // 紅外線（M1.5 之後）
  }
}
void drawIcon(int i, int cx, int cy, uint16_t c) {
  switch (i) {
    case IC_BT:   // 藍牙符文
      s_cv.drawLine(cx, cy - 4, cx, cy + 4, c);
      s_cv.drawLine(cx, cy - 4, cx + 3, cy - 2, c);
      s_cv.drawLine(cx + 3, cy - 2, cx - 3, cy + 2, c);
      s_cv.drawLine(cx - 3, cy - 2, cx + 3, cy + 2, c);
      s_cv.drawLine(cx + 3, cy + 2, cx, cy + 4, c);
      break;
    case IC_RF:   // 天線：桿子＋V（CC1101 315/433MHz）
      s_cv.drawLine(cx, cy - 1, cx, cy + 4, c);
      s_cv.drawLine(cx - 4, cy - 4, cx, cy - 1, c);
      s_cv.drawLine(cx + 4, cy - 4, cx, cy - 1, c);
      s_cv.drawFastHLine(cx - 2, cy + 4, 5, c);
      break;
    case IC_NRF:  // 2.4GHz：點＋兩道弧
      s_cv.fillCircle(cx, cy + 3, 1, c);
      s_cv.drawArc(cx, cy + 3, 4, 4, 225, 315, c);
      s_cv.drawArc(cx, cy + 3, 7, 7, 230, 310, c);
      break;
    case IC_IR:   // 遙控器
      s_cv.drawRoundRect(cx - 2, cy - 3, 5, 8, 1, c);
      s_cv.drawPixel(cx, cy - 1, c);
      s_cv.drawPixel(cx, cy + 2, c);
      s_cv.drawFastHLine(cx - 1, cy - 5, 3, c);
      break;
    case IC_USB:  // USB 叉子
      s_cv.drawLine(cx, cy - 4, cx, cy + 3, c);
      s_cv.fillCircle(cx, cy + 3, 1, c);
      s_cv.drawLine(cx, cy + 1, cx - 3, cy - 1, c);
      s_cv.drawLine(cx, cy, cx + 3, cy - 2, c);
      s_cv.drawPixel(cx - 3, cy - 2, c);
      s_cv.drawFastHLine(cx + 2, cy - 3, 3, c);
      s_cv.drawLine(cx - 1, cy - 3, cx, cy - 4, c);
      s_cv.drawLine(cx + 1, cy - 3, cx, cy - 4, c);
      break;
    case IC_MOUSE:
      s_cv.drawRoundRect(cx - 3, cy - 4, 7, 9, 3, c);
      s_cv.drawFastVLine(cx, cy - 4, 3, c);
      break;
  }
}

void wifiBars(int x, int y, int bars, bool up) {   // 右下角對齊在 (x, y)，寬 11、高 8
  for (int i = 0; i < 4; ++i) {
    const int h = 2 + i * 2;
    const C3& c = (up && i < bars) ? K_GRN : K_DIM;
    s_cv.fillRect(x - 11 + i * 3, y - h, 2, h, col(c));
  }
  if (!up) {   // 沒連上：打叉
    s_cv.drawLine(x - 11, y - 8, x - 4, y - 1, col(K_RED));
    s_cv.drawLine(x - 4, y - 8, x - 11, y - 1, col(K_RED));
  }
}

// ── 各磚塊 ──
const char* kWday[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

void uptimeStr(char* b, size_t n) {
  const uint32_t s = millis() / 1000;
  const uint32_t h = s / 3600, m = (s / 60) % 60;
  if (h < 24) snprintf(b, n, "UP %lu:%02lu", (unsigned long)h, (unsigned long)m);
  else snprintf(b, n, "UP %lud%02luh", (unsigned long)(h / 24), (unsigned long)(h % 24));
}

void drawClock(bool ok, const struct tm& lt) {
  char hm[8], ss[4], date[12], up[16];
  if (ok) {
    snprintf(hm, sizeof hm, "%02d:%02d", lt.tm_hour, lt.tm_min);
    snprintf(ss, sizeof ss, "%02d", lt.tm_sec);
    snprintf(date, sizeof date, "%s %02d/%02d", kWday[lt.tm_wday % 7], lt.tm_mon + 1, lt.tm_mday);
  } else {
    strcpy(hm, "--:--"); strcpy(ss, "--"); strcpy(date, "--- --/--");
  }
  uptimeStr(up, sizeof up);
  const Tile& t = s_t[T_CLOCK];
  txtF(T_CLOCK, 0, 3, hm, ok ? K_TXT : K_DIM, &fonts::Font4);
  const int hw = s_cv.textWidth(hm);
  txtF(T_CLOCK, hw + 2, 13, ss, K_CYAN, &fonts::Font2);
  if (!s_land) {                   // 直式：日期／開機時間／語音額度各一行（小字一行約 16 字，「FRI 11/20 UP 2:14」17 字放不下）
    txt(T_CLOCK, 0, 30, date, K_YEL);
    txt(T_CLOCK, 0, 40, up, K_DIM);
    txt(T_CLOCK, 0, 50, "GPT -", K_DIM);           // A5-2：今日花費／上限 餘額（中控送）
  } else {                         // 橫式：日期、開機時間在時鐘右邊；語音額度在下面整行
    txt(T_CLOCK, 0, 4, date, K_YEL, true);
    txt(T_CLOCK, 0, 15, up, K_DIM, true);
    txt(T_CLOCK, 0, 32, "GPT -", K_DIM);
  }
  (void)t;
}

void drawStat() {
  const Tile& t = s_t[T_STAT];
  const int lv = battery_ui::level();
  const int mv = battery_ui::voltageMv();
  const bool chg = battery_ui::charging();
  char pct[8], volt[8];
  if (lv >= 0) snprintf(pct, sizeof pct, "%d%%", lv); else strcpy(pct, "--%");
  if (mv > 0) snprintf(volt, sizeof volt, "%d.%02dV", mv / 1000, (mv % 1000) / 10); else strcpy(volt, "-.--V");
  const C3& pc = batColor(lv);
  // 11-20 Miles：電量用粗體「100」會糊成一坨 → 電量、電壓都用細體
  txtF(T_STAT, 0, 2 + THIN_DY, pct, chg ? K_YEL : pc, THIN);
  if (chg) {   // 充電中：百分比後面一個小閃電
    const int bx = t.x + PAD + s_cv.textWidth(pct) + 1, by = t.y + 3;   // 字型還是上一行設的細體
    s_cv.drawLine(bx + 3, by, bx, by + 4, col(K_YEL));
    s_cv.drawLine(bx, by + 4, bx + 3, by + 4, col(K_YEL));
    s_cv.drawLine(bx + 3, by + 4, bx, by + 7, col(K_YEL));
  }
  int iy0, cell, perRow;
  if (!s_land) {
    txtF(T_STAT, 0, 13 + THIN_DY, volt, K_DIM, THIN);
    iy0 = t.y + 37; cell = 11; perRow = 3;
  } else {
    txtF(T_STAT, 0, 2 + THIN_DY, volt, K_DIM, THIN, true);
    iy0 = t.y + 23; cell = 13; perRow = 6;
  }
  const int barY = s_land ? t.y + 14 : t.y + 25, barW = t.w - PAD * 2;
  const int barH = s_land ? 3 : 4;
  s_cv.fillRect(t.x + PAD, barY, barW, barH, mixCol(K_TILE, K_DIM, 0.4f));
  if (lv > 0) s_cv.fillRect(t.x + PAD, barY, barW * (lv > 100 ? 100 : lv) / 100, barH, col(pc));
  const int x0 = t.x + (t.w - cell * perRow) / 2 + cell / 2;
  for (int i = 0; i < IC_N; ++i) {
    const int r = i / perRow, c = i % perRow;
    drawIcon(i, x0 + c * cell, iy0 + r * 14, col(iconActive(i) ? K_CYAN : K_DIM));
  }
}

void drawImu() {
  const Tile& t = s_t[T_IMU];
  header(T_IMU, "IMU", K_YEL);
  const int cx = t.x + t.w / 2, cy = t.y + 12 + (t.h - 12) / 2, r = 10;
  s_cv.drawCircle(cx, cy, r, col(K_DIM));
  s_cv.drawCircle(cx, cy, 4, mixCol(K_TILE, K_DIM, 0.5f));
  float right, up, z;
  if (imu_pet::tilt(&right, &up, &z)) {
    auto cl = [](float v) { return v < -1 ? -1.0f : v > 1 ? 1.0f : v; };
    // 氣泡：左右看「畫面右方向」的重力分量、上下看前後傾（螢幕法線 z）
    const int dx = (int)lroundf(cl(right / cfg::INFO_TILT_FULL_G) * (r - 3));
    const int dy = (int)lroundf(cl(z / cfg::INFO_TILT_FULL_G) * (r - 3));
    s_cv.fillCircle(cx + dx, cy + dy, 2, col(K_PINK));
  }
}

void drawWeather() {   // A5-2 才有資料
  header(T_WX, "WEATHER", K_CYAN);
  txtF(T_WX, 0, 15, "-", K_DIM, &fonts::Font2);
  txt(T_WX, 0, 22, "A5-2", K_DIM, true);
}

void drawChip() {
  const Tile& t = s_t[T_CHIP];
  header(T_CHIP, "CHIP", K_BLUE);   // 11-20：外框跟標題同色，CHIP 改藍（避免和 WEATHER 同為青色）
  const float tc = temperatureRead();
  char tb[10];
  if (isnan(tc)) strcpy(tb, "--C"); else snprintf(tb, sizeof tb, "%dC", (int)lroundf(tc));
  txtF(T_CHIP, 0, 1 + THIN_DY, tb, tc >= 60 ? K_RED : K_ORG, THIN, true);   // 11-20 Miles：溫度用粗體看不清 → 細體
  // 波形：每秒一點（主迴圈最長一圈），頂端 40ms、虛線 25ms
  const int gx = t.x + PAD, gw = t.w - PAD * 2;
  const int gy = t.y + 12, gh = t.h - 12 - 12;
  const float full = cfg::INFO_LOOP_FULL_MS;
  const int ly = gy + gh - (int)(gh * cfg::INFO_LOOP_LIMIT_MS / full);
  for (int x = 0; x < gw; x += 4) s_cv.drawFastHLine(gx + x, ly, 2, col(K_DIM));
  const int n = s_loop_n < gw ? s_loop_n : gw;
  int px = -1, py = 0;
  for (int k = 0; k < n; ++k) {   // 最新的在右邊
    const int idx = (s_loop_pos - n + k + cfg::INFO_HIST_SEC * 2) % cfg::INFO_HIST_SEC;
    const float v = s_loop_hist[idx] > full ? full : s_loop_hist[idx];
    const int x = gx + gw - n + k, y = gy + gh - 1 - (int)((gh - 1) * v / full);
    if (px >= 0) s_cv.drawLine(px, py, x, y, col(s_loop_hist[idx] > cfg::INFO_LOOP_LIMIT_MS ? K_RED : K_CYAN));
    else s_cv.drawPixel(x, y, col(K_CYAN));
    px = x; py = y;
  }
  char fb[8], lb[10];
  snprintf(fb, sizeof fb, "%luM", (unsigned long)getCpuFrequencyMhz());
  snprintf(lb, sizeof lb, "%ums", (unsigned)s_loop_now_ms);
  txt(T_CHIP, 0, t.h - 11, fb, K_DIM);
  txt(T_CHIP, 0, t.h - 11, lb, s_loop_now_ms > cfg::INFO_LOOP_LIMIT_MS ? K_RED : K_CYAN, true);
}

void kb(char* b, size_t n, uint32_t bytes) {
  if (bytes >= 1024 * 1024) snprintf(b, n, "%lu.%luM", (unsigned long)(bytes >> 20), (unsigned long)(((bytes & 0xFFFFF) * 10) >> 20));
  else snprintf(b, n, "%luK", (unsigned long)(bytes >> 10));
}

void drawMem() {
  const Tile& t = s_t[T_MEM];
  header(T_MEM, "MEM", K_PUR);
  const uint32_t lo = ESP.getMinFreeHeap();
  char lb[12], cur[10], ps[12];
  kb(cur, sizeof cur, lo);
  snprintf(lb, sizeof lb, "lo%s", cur);
  txt(T_MEM, 0, 1, lb, K_DIM, true);
  // 長條：heap 剩餘，每根 2px（最新在右），頂端＝歷史最大值，紅線＝開機以來最低
  const int gx = t.x + PAD, gw = t.w - PAD * 2;
  const int gy = t.y + 12, gh = t.h - 12 - 12;
  uint32_t top = lo;
  for (int k = 0; k < s_heap_n; ++k) if (s_heap_hist[k] > top) top = s_heap_hist[k];
  if (top == 0) top = 1;
  const int maxBars = gw / 2;
  const int n = s_heap_n < maxBars ? s_heap_n : maxBars;
  for (int k = 0; k < n; ++k) {
    const int idx = (s_heap_pos - n + k + cfg::INFO_HEAP_BARS * 2) % cfg::INFO_HEAP_BARS;
    const int h = (int)((uint64_t)gh * s_heap_hist[idx] / top);
    s_cv.fillRect(gx + gw - (n - k) * 2, gy + gh - h, 1, h, col(K_PUR));
  }
  const int ly = gy + gh - (int)((uint64_t)gh * lo / top);
  s_cv.drawFastHLine(gx, ly, gw, col(K_RED));
  kb(cur, sizeof cur, ESP.getFreeHeap());
  char psb[10];
  kb(psb, sizeof psb, ESP.getFreePsram());
  snprintf(ps, sizeof ps, "P%s", psb);
  txt(T_MEM, 0, t.h - 11, cur, K_PUR);
  txt(T_MEM, 0, t.h - 11, ps, K_DIM, true);
}

void drawNet() {
  const Tile& t = s_t[T_NET];
  header(T_NET, "NET LINK", K_VIO);   // 11-20 Miles：NET LINK 紫、MODULES 綠、快訊橘
  const bool up = net_hub::wifiUp();
  const int rssi = up ? (int)WiFi.RSSI() : 0;
  const int bars = !up ? 0 : rssi >= cfg::WIFI_BARS_4 ? 4 : rssi >= cfg::WIFI_BARS_3 ? 3 : rssi >= cfg::WIFI_BARS_2 ? 2 : 1;
  wifiBars(t.x + t.w - PAD, t.y + 10, bars, up);
  char rb[8];
  if (up) snprintf(rb, sizeof rb, "%d", rssi); else strcpy(rb, "OFF");
  const bool cloud = net_hub::linkUp();
  const char* vs = voice_turn::stateName();
  txt(T_NET, 0, ROW1, "WiFi", K_TXT);
  txt(T_NET, 0, ROW1, rb, up ? K_TXT : K_RED, true);
  txt(T_NET, 0, ROW2, "CLOUD", K_TXT);
  txt(T_NET, 0, ROW2, cloud ? "ON" : "OFF", cloud ? K_GRN : K_RED, true);
  txt(T_NET, 0, ROW3, "VOICE", K_TXT);
  txt(T_NET, 0, ROW3, vs, strcmp(vs, "IDLE") == 0 ? K_DIM : K_PINK, true);
}

void drawMod() {
  header(T_MOD, "MODULES", K_GRN);
  const GroveDevice g = input_hal::device();
  const char* gn = g == GroveDevice::None ? "none" : input_hal::deviceName();
  const C3& gc = g == GroveDevice::None ? K_DIM : K_TXT;
  if (!s_land) {   // 直式寬 64：「GROVE」與值放不進同一行
    txt(T_MOD, 0, ROW1, "GROVE", K_TXT);
    txt(T_MOD, 0, ROW2, gn, gc, true);
    txt(T_MOD, 0, ROW3, "FW", K_TXT);
    txt(T_MOD, 0, ROW3, audio_io::buildTag(), K_DIM, true);
  } else {
    txt(T_MOD, 0, 1, audio_io::buildTag(), K_DIM, true);
    txt(T_MOD, 0, ROW1, "GROVE", K_TXT);
    txt(T_MOD, 0, ROW1, gn, gc, true);
  }
}

void drawNews() {   // A5-2 才有快訊；沒有內容時畫合成波地平線
  const Tile& t = s_t[T_NEWS];
  s_cv.setClipRect(t.x, t.y, t.w, t.h);
  const int hy = t.y + (s_land ? 12 : 22);   // 地平線
  for (int i = 0; i < 5; ++i) {
    const int y = hy + (i * i * (t.y + t.h - hy)) / 16;
    if (y < t.y + t.h) s_cv.drawFastHLine(t.x, y, t.w, mixCol(K_TILE, K_PINK, 0.25f + i * 0.1f));
  }
  const int cx = t.x + t.w / 2;
  for (int i = -5; i <= 5; ++i)
    s_cv.drawLine(cx + i * t.w / 12, hy, cx + i * t.w / 3, t.y + t.h + 10, mixCol(K_TILE, K_PINK, 0.3f));
  s_cv.clearClipRect();
  txtF(T_NEWS, 0, 1, "快訊", K_ORG, &fonts::efontTW_12);
  const int hw = s_cv.textWidth("快訊");
  txt(T_NEWS, hw + 4, 2, "- A5-2", K_DIM);
}

void drawAll() {
  const Orient o = ui_expression::orientation();
  const uint8_t rot = o == Orient::Portrait ? 0 : o == Orient::LandALeft ? cfg::ORIENT_ROT_A_LEFT : cfg::ORIENT_ROT_A_RIGHT;
  s_cv.setRotation(rot);
  layout(o);
  s_drawn_orient = o;
  s_cv.fillScreen(col(K_BG));
  // 11-20 Miles：磚塊加 1px 亮邊（邊緣銳利），外框顏色跟該塊標題字同色（電量塊跟著電量色）
  for (int i = 0; i < T_N; ++i) {
    s_cv.fillRoundRect(s_t[i].x, s_t[i].y, s_t[i].w, s_t[i].h, 3, col(K_TILE));
    s_cv.drawRoundRect(s_t[i].x, s_t[i].y, s_t[i].w, s_t[i].h, 3, mixCol(K_TILE, tileAccent(i), 0.8f));
  }
  s_cv.setTextDatum(textdatum_t::top_left);
  struct tm lt;
  memset(&lt, 0, sizeof lt);
  const bool ok = synced();
  if (ok) { const time_t now = time(nullptr); localtime_r(&now, &lt); }
  drawClock(ok, lt);
  drawStat();
  drawImu();
  drawWeather();
  drawChip();
  drawMem();
  drawNet();
  drawMod();
  drawNews();
}

long currentSec() { return synced() ? (long)time(nullptr) : (long)(millis() / 1000); }

}  // namespace

namespace ui_info {

void begin() {
  s_cv.setPsram(true);
  s_cv.setColorDepth(16);
  s_cv.createSprite(M5.Display.width(), M5.Display.height());   // 135×240（螢幕 rotation 0），PSRAM 約 64KB
  s_ready = (s_cv.getBuffer() != nullptr);
  if (!s_ready) Serial.println("[ui] 資訊頁畫布配置失敗（PSRAM 不足？）");
  layout(Orient::Portrait);
  s_loop_sec_t0 = millis();
}

void loop() {
  sampleLoop();
  if (!s_ntp_started && net_hub::wifiUp()) {
    configTzTime(cfg::NTP_TZ, cfg::NTP_SERVER1, cfg::NTP_SERVER2);   // SNTP 在背景對時，不等待
    s_ntp_started = true;
    Serial.printf("[time] Wi-Fi 已連上，開始對時（%s）\n", cfg::NTP_SERVER1);
  }
  synced();   // 對到時印一次
}

void render() {
  if (!s_ready || !s_shown) return;
  const long sec = currentSec();
  if (!s_dirty && sec == s_last_sec && ui_expression::orientation() == s_drawn_orient) return;
  const bool turned = ui_expression::orientation() != s_drawn_orient;
  const uint32_t t0 = micros();
  drawAll();                       // 畫在記憶體裡的畫布
  const uint32_t t1 = micros();
  ui_fx::present(&s_cv);           // 整張 64KB 經 SPI（40MHz）送上螢幕（換整頁時由 ui_fx 交叉淡化）
  const uint32_t t2 = micros();
  if (s_dirty || turned)
    Serial.printf("[ui] 資訊頁重畫（%s，%s）：畫 %.1f ms＋送上螢幕 %.1f ms\n", s_dirty ? "切過來" : "轉向",
                  ui_expression::orientName(s_drawn_orient), (t1 - t0) / 1000.0f, (t2 - t1) / 1000.0f);
  s_dirty = false;
  s_last_sec = sec;
}

bool shown() { return s_shown; }

void setShown(bool on, const char* why) {
  if (on == s_shown) return;
  s_shown = on;
  s_dirty = true;
  Serial.printf("[ui] %s → %s（%s）\n", on ? "表情頁" : "資訊頁", on ? "資訊頁" : "表情頁", why);
}

bool timeSynced() { return s_synced; }
M5Canvas* canvas() { return &s_cv; }

}  // namespace ui_info
