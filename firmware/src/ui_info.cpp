#include "ui_info.h"

#include <M5Unified.h>
#include <WiFi.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <initializer_list>

#include "audio_io.h"
#include "battery_ui.h"
#include "config.h"
#include "ui_fx.h"
#include "info_data.h"
#include "input_hal.h"
#include "mode_manager.h"
#include "net_hub.h"
#include "ui_expression.h"
#include "voice_turn.h"
#include "ui_wxicon.h"
#include "ui_kit.h"
#include "ui_labels.h"   // A7：「台北時間」點陣圖（不連結整套 efontTW_16_b）

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
bool s_drawn_eva = false;


// ── A7 外觀統一（橘黑 EVA 系）：亮度分層＋三個狀態色；邊緣警示線紅黑、功能 icon 黑紅（跟 B 切模式過場呼應）──
struct C3 { uint8_t r, g, b; };
constexpr C3 K_BG    = {0x00, 0x00, 0x00};
constexpr C3 K_FRAME = {0x6A, 0x3A, 0x00};   // 磚塊細框（暗橘）
constexpr C3 K_UNLIT = {0x24, 0x13, 0x00};   // 七段數字沒亮的段、空格子
constexpr C3 K_DIM   = {0xB0, 0x6A, 0x10};   // 次要文字（暗橘）
constexpr C3 K_TXT   = {0xFF, 0x9B, 0x00};   // 主文字（琥珀橘）
constexpr C3 K_HI    = {0xFF, 0xD0, 0x80};   // 強調（亮橘白）
constexpr C3 K_RED   = {0xFF, 0x29, 0x23};   // 錯誤／警示線／啟用中的 icon
constexpr C3 K_YEL   = {0xFF, 0xD0, 0x00};   // 注意
constexpr C3 K_GRN   = {0x39, 0xFF, 0x14};   // 正常（電量 ≥60%、額度 <70%）；MEM 最低線（11-22 Miles）
constexpr C3 K_PUR   = {0xB0, 0x4D, 0xFF};   // MEM 記憶體長條（11-22 Miles：改紫色）
inline uint16_t col(const C3& c) { return s_cv.color565(c.r, c.g, c.b); }

// ── 版面（實際像素；直式 135×240，橫式 240×135、A 鍵在右；A 鍵在左＝左右鏡像）──
struct Tile { const char* name; const char* label; int16_t x, y, w, h; };
enum { T_CLOCK, T_WX, T_PWR, T_SYS, T_ICON, T_NEWS, T_N };   // A7：不放 IMU（Miles）
// 第三版（Miles 11-20）：大時鐘＋右側秒數與額度；天氣縮窄、跟 LINK icon 放在一起
const Tile kPortrait[T_N] = {
  {"clock", "TIME", 2, 5, 131, 50},
  {"wx",    "WX",   2, 143, 76, 52},
  {"pwr",   "PWR",  2, 57, 131, 40},
  {"sys",   "SYS",  2, 99, 131, 42},
  {"icon",  "LINK", 80, 143, 53, 52},
  {"news",  "NEWS", 2, 197, 131, 38},
};
const Tile kLandRight[T_N] = {   // 第四版：電量（含額度）移到寬的那側；A 鍵側放天氣＋LINK＋快訊
  {"clock", "TIME", 2, 5, 146, 44},
  {"wx",    "WX",   150, 5, 88, 52},
  {"pwr",   "PWR",  2, 51, 146, 40},
  {"sys",   "SYS",  150, 59, 88, 72},      // 第六版：橫式晶片／記憶體上下疊在 A 鍵側
  {"icon",  "LINK", -1, 0, 0, 0},          // 橫式不顯示 icon（Miles）
  {"news",  "NEWS", 2, 93, 146, 38},
};
Tile s_t[T_N];
bool s_land = false;
int s_W = 135, s_H = 240;
constexpr int PAD = 3;
const lgfx::IFont* const THIN = &fonts::Font0;
const lgfx::IFont* const BOLD = &fonts::efontTW_10_b;
constexpr int THIN_DY = 1;

void layout(Orient o) {
  s_land = (o != Orient::Portrait);
  const Tile* src = s_land ? kLandRight : kPortrait;
  s_W = s_land ? 240 : 135; s_H = s_land ? 135 : 240;
  for (int i = 0; i < T_N; ++i) {
    s_t[i] = src[i];
    if (o == Orient::LandALeft && s_t[i].w) s_t[i].x = s_W - src[i].x - src[i].w;
  }
}

#ifdef INFO_SIM
void simCheck(int ti, const char* s, int x, int y, int w, int h) {
  const Tile& t = s_t[ti];
  if (x < t.x + 1 || x + w > t.x + t.w - 1 || y < t.y || y + h > t.y + t.h)
    infoSimReport("超出磚塊", t.name, s, x, y, w, h);
}
#endif

// 小字混排在 ui_kit（英文 Font0、數字 efontTW_10_b、中文 efontTW_10）
int mixedWidth(const char* s) { return ui_kit::mixedWidth(s_cv, s); }
void txt(int ti, int dx, int dy, const char* s, const C3& c, bool right = false) {
  const Tile& t = s_t[ti];
  const int w = mixedWidth(s);
  int x = right ? t.x + t.w - PAD - w : t.x + PAD + dx;
  const int y = t.y + dy;
#ifdef INFO_SIM
  simCheck(ti, s, x, y, w, 10);
#endif
  ui_kit::drawMixed(s_cv, x, y, s, col(c));
}
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

// ── EVA 元件 ──
// 邊緣警示線：紅黑斜紋（Miles A7：跟 B 切模式過場呼應）
void hazard(int y, int h) { ui_kit::hazardRed(s_cv, y, s_W, h); }
// 磚塊標題：六角實心標籤（琥珀底黑字），寬依文字
int hexLabel(int ti, const char* s) { return ui_kit::hexLabel(s_cv, s_t[ti].x + 1, s_t[ti].y + 1, s, col(K_TXT), col(K_BG)); }
void frame(int ti) {
  const Tile& t = s_t[ti];
  if (!t.w) return;
  ui_kit::tile(s_cv, t.x, t.y, t.w, t.h, col(K_FRAME), col(K_DIM));   // 四角亮一點（EVA 取景框感）
}
// 七段數字（A7 Miles：段兩端削尖、段間 1px 縫，沒亮的段更暗）→ ui_kit::seg7
void seg7(int x, int y, int w, int h, int n, const C3& on) { ui_kit::seg7(s_cv, x, y, w, h, n, col(on), col(K_UNLIT)); }
void seg7pair(int x, int y, int w, int h, int v, const C3& on) {
  seg7(x, y, w, h, v < 0 ? 10 : (v / 10) % 10, on);
  seg7(x + w + 2, y, w, h, v < 0 ? 10 : v % 10, on);
}
const C3& batColor(int lv) { return lv < 0 ? K_DIM : lv >= 60 ? K_GRN : lv >= 20 ? K_YEL : K_RED; }

// ── 歷史資料 ──
uint8_t  s_loop_hist[cfg::INFO_HIST_SEC];    // 每秒主迴圈最長一圈（ms，封頂 255）
int      s_loop_pos = 0, s_loop_n = 0;
uint32_t s_loop_sec_max_us = 0, s_loop_last_us = 0, s_loop_sec_t0 = 0;
uint8_t  s_loop_now_ms = 0;
uint8_t s_drain_hist[cfg::INFO_DRAIN_BARS] = {};
int s_drain_pos = 0, s_drain_n = 0, s_drain_baseline = -1;
uint8_t s_drain_sum[cfg::INFO_DRAIN_BARS] = {};
bool s_drain_start[cfg::INFO_DRAIN_BARS] = {};
bool s_drain_new_session = true;
int s_drain_session_total = 0;
uint32_t s_drain_t = 0;

int cpuPercent() {
  return getCpuFrequencyMhz() * 100 / cfg::PWR_CPU_AWAKE_MHZ;
}

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
  } else {
    return;
  }
  const int level = battery_ui::level();
  if (battery_ui::externalPower()) {
    s_drain_baseline = -1;
    s_drain_new_session = true;
  } else if (level >= 0) {
    if (s_drain_baseline < 0) {
      s_drain_baseline = level;
      s_drain_session_total = 0;
      s_drain_t = now;
    } else if (now - s_drain_t >= cfg::INFO_DRAIN_EVERY_MS) {
      const int loss = s_drain_baseline > level ? s_drain_baseline - level : 0;
      s_drain_baseline -= loss;  // 電量短暫回彈時不重複計算同一段下降
      s_drain_session_total += loss;
      s_drain_hist[s_drain_pos] = loss;
      s_drain_sum[s_drain_pos] = s_drain_session_total;
      s_drain_start[s_drain_pos] = s_drain_new_session;
      s_drain_new_session = false;
      s_drain_pos = (s_drain_pos + 1) % cfg::INFO_DRAIN_BARS;
      if (s_drain_n < cfg::INFO_DRAIN_BARS) ++s_drain_n;
      s_drain_t = now;
    }
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

// ── 時鐘：七段數字 HH:MM＋小秒數；右邊日期、開機時間；最下一行語音額度 ──
const char* kWday[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
// 語音額度兩行（放在秒數下面）：「$0.42/2」＋「餘8.37」；今日花費依比例綠／黃／紅，斷線變暗
void drawUsd(int ti, int dx, int dy) {
  const info_data::Usd& u = info_data::usd();
  const lgfx::IFont* F = &fonts::efontTW_12;   // 第五版（Miles）：額度字放大到 12px
  if (!u.valid) { txtF(ti, dx, dy, "GPT -", K_DIM, F); return; }
  char head[16], cap[12], left[16];
  snprintf(head, sizeof head, "$%.2f", u.today);
  snprintf(cap, sizeof cap, "/%d", (int)(u.cap + 0.5f));
  const int lv = info_data::usdLevel();
  const C3& tc = !net_hub::linkUp() ? K_DIM : lv == 2 ? K_RED : lv == 1 ? K_YEL : K_GRN;
  txtF(ti, dx, dy, head, tc, F);
  const int hw = s_cv.textWidth(head);
  txtF(ti, dx + hw, dy, cap, K_DIM, F);
  if (u.left >= 0) { snprintf(left, sizeof left, u.left >= 100 ? "餘%.0f" : "餘%.2f", u.left); txtF(ti, dx, dy + 12, left, K_RED, F); }   // 11-21 Miles：餘額紅字
}
// ── 斜體七段數字（第六版：參考 EVA「活動限界」計時器——字往右斜、段兩端削尖、段間留縫）──
// 每 SLANT_STEP 列往右偏 1px（整數斜率：階梯每階一樣高，邊緣乾淨；原本 0.17 四捨五入，階梯高低不一看起來糊）
constexpr int SLANT_STEP = 6;
constexpr float SLANT = 1.0f / SLANT_STEP;
inline void shRow(int x, int y, int len, int base, uint16_t c) {   // base＝數字底部 y
  if (len > 0) s_cv.drawFastHLine(x + (base - 1 - y) / SLANT_STEP, y, len, c);
}
void hsegI(int x, int y, int len, int t, int base, uint16_t c) {
  for (int r = 0; r < t; ++r) {
    const int in = r < (t - 1) / 2 ? (t - 1) / 2 - r : r - t / 2 > 0 ? r - t / 2 : 0;
    shRow(x + in, y + r, len - 2 * in, base, c);
  }
}
void vsegI(int x, int y, int len, int t, int base, uint16_t c) {
  const int tip = (t - 1) / 2;
  for (int r = 0; r < len; ++r) {
    const int in = r < tip ? tip - r : (len - 1 - r) < tip ? tip - (len - 1 - r) : 0;
    shRow(x + in, y + r, t - 2 * in, base, c);
  }
}
void seg7I(int x, int y, int w, int h, int n, const C3& on) {
  constexpr uint8_t mask[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f, 0x40};
  const int t = w >= 11 ? 3 : 2, base = y + h;
  const int mid = (h - t) / 2, hl = w - 2, vu = mid - 1, vl = h - t - mid - 1;
  const uint16_t c1 = col(on), c0 = s_cv.color565(0x1A, 0x0E, 0x00);   // 時鐘的暗段再暗一點，亮的「1」才不會被吃掉
  auto c = [&](int i) { return (n >= 0 && (mask[n] & (1 << i))) ? c1 : c0; };
  hsegI(x + 1, y, hl, t, base, c(0));
  vsegI(x + w - t, y + 1, vu, t, base, c(1));
  vsegI(x + w - t, y + mid + 1, vl + t - 1, t, base, c(2));
  hsegI(x + 1, y + h - t, hl, t, base, c(3));
  vsegI(x, y + mid + 1, vl + t - 1, t, base, c(4));
  vsegI(x, y + 1, vu, t, base, c(5));
  hsegI(x + 1, y + mid, hl, t, base, c(6));
}
// 兩位數；lead＝十位是 0 時不亮（EVA 計時器的暗 8）
void seg7IPair(int x, int y, int w, int h, int v, const C3& on, bool blankLead = false) {
  const int tens = v < 0 ? 10 : (v / 10) % 10;
  seg7I(x, y, w, h, (blankLead && tens == 0) ? -1 : tens, on);
  seg7I(x + w + 3, y, w, h, v < 0 ? 10 : v % 10, on);
}
void colonI(int x, int y, int h, const C3& on) {   // 斜體冒號：兩個小方塊跟著斜
  const int base = y + h;
  for (int k : {h / 3, h * 2 / 3}) {
    const int yy = y + k - 1;
    for (int r = 0; r < 3; ++r) shRow(x, yy + r, 3, base, col(on));
  }
}
// NERV 樣式狀態框（跟內部／外部一樣）：亮＝實心底黑字，暗＝細框暗字
void nervBox(int x, int y, int w, int h, const char* s, bool active, const C3& on) {
  if (active) s_cv.fillRect(x, y, w, h, col(on));
  else s_cv.drawRect(x, y, w, h, col(K_FRAME));
  s_cv.setFont(&fonts::Font2);
  s_cv.setTextColor(active ? col(K_BG) : col(K_DIM));
  s_cv.drawString(s, x + (w - s_cv.textWidth(s)) / 2, y + (h - 14) / 2);
}
// 時鐘（第六版，Miles 11-20：參考 EVA「活動限界まで」）
//   左上：「台北時間」粗體大字＋小字 TAIPEI LOCAL TIME；右上：日期
//   下面：最左 AM／PM 兩個 NERV 框上下疊，斜體七段 12 小時制時:分＋小秒數，整組靠右
void drawClock(bool ok, const struct tm& lt) {
  const Tile& t = s_t[T_CLOCK];
  const ui_labels::Bmp* tp = ui_labels::find("taipei");
  if (tp) s_cv.drawBitmap(t.x + PAD, t.y + 2, tp->bits, tp->w, tp->h, col(K_HI));
  const int lw = tp ? tp->w : 0;
  s_cv.setFont(&fonts::TomThumb);
  s_cv.setTextColor(col(K_DIM));
  s_cv.drawString("TAIPEI", t.x + PAD + lw + 3, t.y + 4);
  s_cv.drawString(s_land ? "LOCAL TIME" : "TIME", t.x + PAD + lw + 3, t.y + 11);   // 直式太窄：只寫 TIME，避免撞到星期
  char date[16];
  if (ok) snprintf(date, sizeof date, "%02d.%02d", lt.tm_mon + 1, lt.tm_mday);
  else strcpy(date, "--.--");
  txt(T_CLOCK, 0, 1, date, K_HI, true);
  if (ok) txt(T_CLOCK, 0, 10, kWday[lt.tm_wday % 7], K_DIM, true);
  // 12 小時制
  const int h12 = !ok ? -1 : (lt.tm_hour % 12 == 0 ? 12 : lt.tm_hour % 12);
  const bool pm = ok && lt.tm_hour >= 12;
  const int dw = s_land ? 14 : 15, dh = s_land ? 24 : 28;
  const int sw = 8, sh = 14;                                   // 秒數
  const int y = t.y + t.h - dh - 3;
  // 從右往左排
  const int slantPad = (int)(dh * SLANT) + 1;
  const int right = t.x + t.w - PAD - slantPad;
  const int sx = right - (2 * sw + 3);
  seg7IPair(sx, y + dh - sh, sw, sh, ok ? lt.tm_sec : -1, K_DIM);
  const int mx = sx - 5 - (2 * dw + 3);
  seg7IPair(mx, y, dw, dh, ok ? lt.tm_min : -1, K_TXT);
  const int cx = mx - 7;
  colonI(cx + 1, y, dh, K_TXT);
  const int hx = cx - 3 - (2 * dw + 3);
  seg7IPair(hx, y, dw, dh, h12, K_TXT, true);
  // AM／PM 框：最左、上下疊，高度跟數字一樣
  const int bw = 22, bh = (dh - 2) / 2;
  const int bx = t.x + 2;
  nervBox(bx, y, bw, bh, "AM", ok && !pm, K_TXT);
  nervBox(bx, y + bh + 2, bw, bh, "PM", pm, K_TXT);
}

// ── 天氣（A7 第三版：縮窄，跟 LINK icon 並排）：標題列地點／資料時間；圖示＋溫度；描述；濕度、降雨機率 ──
void drawWeather() {
  const Tile& t = s_t[T_WX];
  hexLabel(T_WX, "WX");
  const info_data::Weather& w = info_data::weather();
  if (!w.valid) { txt(T_WX, 0, 14, "--", K_DIM); return; }
  const bool live = net_hub::linkUp();
  char hdr[20];
  if (live) snprintf(hdr, sizeof hdr, "%s", w.loc); else info_data::formatAt(hdr, sizeof hdr, w.at);
  txt(T_WX, 0, 1, hdr, live ? K_HI : K_DIM, true);
  // 11-21 Miles（27-a7h）：圖示、氣溫、描述集中在左邊；右邊一小欄上下放濕度、降雨機率，中間一條細分隔線
  char rb[12] = "--", pb[12] = "--";
  if (w.rh >= 0) snprintf(rb, sizeof rb, "%d%%", w.rh);
  if (w.pop >= 0) snprintf(pb, sizeof pb, "%d%%", w.pop);
  auto thinW = [&](const char* v) { s_cv.setFont(THIN); return (int)s_cv.textWidth(v); };
  const int rw0 = thinW(rb), pw0 = thinW(pb);
  const int RC = (rw0 > pw0 ? rw0 : pw0) + 8;                  // 右欄寬：圖示 6＋間距 2＋數字（照實際數字寬）
  const int rx = t.w - 2 * PAD - RC;                           // 右欄左緣（磚內座標）
  s_cv.drawFastVLine(t.x + PAD + rx - 3, t.y + 14, 34, col(K_FRAME));
  const C3& pc = w.pop >= 50 ? K_YEL : K_DIM;
  ui_wxicon::drop(s_cv, t.x + PAD + rx, t.y + 19, col(K_DIM));
  txtF(T_WX, rx + 8, 18 + THIN_DY, rb, K_DIM, THIN);
  ui_wxicon::umbrella(s_cv, t.x + PAD + rx, t.y + 35, col(pc));
  txtF(T_WX, rx + 8, 34 + THIN_DY, pb, pc, THIN);
  // 左邊：圖示＋氣溫一列，描述一列
  // 11-21 Miles：天氣圖示全彩（黃太陽、白雲；陰／雨／雷雨的雲灰白、雨藍）
  {
    const bool grey = (w.icon == info_data::WxIcon::Cloud || w.icon == info_data::WxIcon::Rain || w.icon == info_data::WxIcon::Storm);
    ui_wxicon::draw(s_cv, t.x + PAD + 7, t.y + 21, 7, w.icon, s_cv.color565(0xFF, 0xD0, 0x00),
                    grey ? s_cv.color565(0xC4, 0xC8, 0xD0) : s_cv.color565(0xFF, 0xFF, 0xFF), s_cv.color565(0x3F, 0xA9, 0xFF));
  }
  char tb[12] = "--";
  if (w.temp > -100) snprintf(tb, sizeof tb, "%d", w.temp);
  txtF(T_WX, 18, 13, tb, K_HI, &fonts::Font2);
  const int tw = s_cv.textWidth(tb);
  s_cv.drawCircle(t.x + PAD + 18 + tw + 2, t.y + 16, 1, col(K_HI));
  char d[40];
  info_data::utf8Prefix(d, sizeof d, w.desc, (rx - 4) / 12);
  txtF(T_WX, 0, 33, d, K_TXT, &fonts::efontTW_12);
}

// efontTW_16_b 的四個原生字形，32×16、每列 MSB first。
constexpr uint8_t kInternalLabel[] = {
  0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x18, 0x7E,
  0x1F, 0x00, 0xFF, 0xA6, 0x03, 0x00, 0x63, 0x66,
  0x7F, 0xFC, 0x36, 0x6C, 0x63, 0x0C, 0x36, 0x6C,
  0x63, 0x8C, 0x34, 0x68, 0x63, 0x8C, 0xFF, 0xAC,
  0x66, 0xCC, 0x00, 0x66, 0x66, 0xCC, 0x00, 0x66,
  0x6C, 0x6C, 0x7F, 0x63, 0x68, 0x34, 0x63, 0x73,
  0x60, 0x0C, 0x63, 0x6B, 0x60, 0x0C, 0x63, 0x6E,
  0x60, 0x3C, 0x7F, 0x60, 0x00, 0x00, 0x00, 0x60,
};
constexpr uint8_t kExternalLabel[] = {
  0x0C, 0x60, 0x18, 0x00, 0x0C, 0x60, 0x18, 0x7E,
  0x0F, 0xA0, 0xFF, 0xA6, 0x19, 0xA0, 0x63, 0x66,
  0x19, 0xA0, 0x36, 0x6C, 0x31, 0xA0, 0x36, 0x6C,
  0x3B, 0x70, 0x34, 0x68, 0x6D, 0x6C, 0xFF, 0xAC,
  0xC7, 0x66, 0x00, 0x66, 0x06, 0x63, 0x00, 0x66,
  0x06, 0x60, 0x7F, 0x63, 0x0C, 0x60, 0x63, 0x73,
  0x0C, 0x60, 0x63, 0x6B, 0x18, 0x60, 0x63, 0x6E,
  0x30, 0x60, 0x7F, 0x60, 0x60, 0x60, 0x00, 0x60,
};

// ── 電量（A7：沿用 EVA 版——內部／外部框上下疊、七段大百分比、十格電量條、電壓）──
void powerBox(int x, int y, int w, const uint8_t* label, bool active) {
  constexpr int h = 18;
  if (active) s_cv.fillRect(x, y, w, h, col(K_GRN));
  else s_cv.drawRect(x, y, w, h, col(K_FRAME));
  s_cv.drawBitmap(x + (w - 32) / 2, y + 1, label, 32, 16, active ? col(K_BG) : col(K_DIM));
}
void drawPwr() {
  const Tile& t = s_t[T_PWR];
  const int lv = battery_ui::level(), mv = battery_ui::voltageMv();
  const bool ext = battery_ui::externalPower();
  // 11-21 Miles：內部／外部移到最右邊，電量、額度、十格條在左
  const int bw = 36, bx = t.x + t.w - 2 - bw, by = t.y + 2;
  powerBox(bx, by, bw, kInternalLabel, !ext);
  powerBox(bx, by + 18, bw, kExternalLabel, ext);
  const C3& pc = batColor(lv);
  const int rx = t.x + PAD, rw = bx - 5 - rx;              // 左邊（內部／外部框的左側）
  // 七段百分比：兩位數 13×22；100 用三位 10×22
  const int dh = 22, dy = t.y + 3;
  int x = rx;
  if (lv >= 100) {
    for (int k = 0; k < 3; ++k) { seg7(x, dy, 9, dh, k == 0 ? 1 : 0, pc); x += 11; }
  } else {
    seg7pair(x, dy, 13, dh, lv, pc);
    x += 13 * 2 + 2 + 2;
  }
  txt(T_PWR, x - t.x - PAD + 1, dy - t.y + dh - 10, "%", pc);
  // 右欄（第四版，Miles：額度跟電量放一起）：電壓、今日花費／上限、餘額
  const int cx = x + 8 - t.x - PAD;                  // 相對磚塊內容左緣
  char volt[10];
  if (mv > 0) snprintf(volt, sizeof volt, "%d.%02dV", mv / 1000, (mv % 1000) / 10); else strcpy(volt, "-.--V");
  txtF(T_PWR, cx, 2, volt, K_DIM, &fonts::TomThumb);   // 第五版：電壓改小字（TomThumb 3×5）
  drawUsd(T_PWR, cx, 8);
  // 十格電量條（左邊最下面）
  const int gy = t.y + t.h - 6;
  const int lit = lv < 0 ? 0 : (lv * 10 + 50) / 100;
  for (int i = 0; i < 10; ++i) {
    const int l = rx + i * rw / 10;
    s_cv.fillRect(l, gy, rx + (i + 1) * rw / 10 - l - 1, 4, i < lit ? col(pc) : col(K_UNLIT));
  }
}

int hexLabelAt(int x, int y, const char* s) { return ui_kit::hexLabel(s_cv, x, y, s, col(K_TXT), col(K_BG), 1); }
// 不綁磚塊的小字（模擬時檢查超出整個 SYS 磚）
int txtAt(int x, int y, const char* str, const C3& c, bool draw = true) {
  const int w = mixedWidth(str);
  if (!draw) return w;
#ifdef INFO_SIM
  simCheck(T_SYS, str, x, y, w, 10);
#endif
  ui_kit::drawMixed(s_cv, x, y, str, col(c));
  return w;
}
// 時脈相對於全速的比例。下方保留實際 MHz 與主迴圈耗時。
void chipPart(int x, int y, int w, int h) {
  hexLabelAt(x - 2, y, "CHIP");
  char pct[8], fb[10], ms[8];
  snprintf(pct, sizeof pct, "%d%%", cpuPercent());
  txtAt(x + w - mixedWidth(pct), y, pct, K_TXT);
  const int gy = y + 12, gh = h - 12 - 11;
  s_cv.drawRect(x, gy, w, gh, col(K_UNLIT));
  const float full = cfg::INFO_LOOP_FULL_MS;
  const int ly = gy + gh - (int)(gh * cfg::INFO_LOOP_LIMIT_MS / full);
  for (int k = 0; k < w; k += 4) s_cv.drawFastHLine(x + k, ly, 2, col(K_RED));
  const int n = s_loop_n < w - 2 ? s_loop_n : w - 2;
  int px = -1, py = 0;
  for (int k = 0; k < n; ++k) {
    const int idx = (s_loop_pos - n + k + cfg::INFO_HIST_SEC * 2) % cfg::INFO_HIST_SEC;
    const float v = s_loop_hist[idx] > full ? full : s_loop_hist[idx];
    const int xx = x + w - 1 - n + k, yy = gy + gh - 2 - (int)((gh - 3) * v / full);
    if (px >= 0) s_cv.drawLine(px, py, xx, yy, col(s_loop_hist[idx] > cfg::INFO_LOOP_LIMIT_MS ? K_RED : K_TXT));
    px = xx; py = yy;
  }
  snprintf(fb, sizeof fb, "%luM", (unsigned long)getCpuFrequencyMhz());
  snprintf(ms, sizeof ms, "%ums", (unsigned)s_loop_now_ms);
  txtAt(x, y + h - 10, fb, K_DIM);
  txtAt(x + w - mixedWidth(ms), y + h - 10, ms, s_loop_now_ms > cfg::INFO_LOOP_LIMIT_MS ? K_RED : K_TXT);
}
// 紫柱是每分鐘掉電百分點，綠線依每次拔線重新累計。
void drainPart(int x, int y, int w, int h) {
  hexLabelAt(x - 2, y, "耗電");
  const int gy = y + 12, gh = h - 12 - 11;
  s_cv.drawRect(x, gy, w, gh, col(K_UNLIT));
  const int nb = s_drain_n;
  const int start = (s_drain_pos + cfg::INFO_DRAIN_BARS - nb) % cfg::INFO_DRAIN_BARS;
  int scale = 1;
  for (int k = 0; k < nb; ++k) {
    const int sum = s_drain_sum[(start + k) % cfg::INFO_DRAIN_BARS];
    if (sum > scale) scale = sum;
  }
  const int bottom = gy + gh - 2;
  const int plotW = w - 2;
  for (int k = 0; k < nb; ++k) {
    const int left = x + 1 + k * plotW / nb;
    const int right = x + 1 + (k + 1) * plotW / nb;
    const int height = s_drain_hist[(start + k) % cfg::INFO_DRAIN_BARS] * (gh - 3) / scale;
    if (height > 0) s_cv.fillRect(left, bottom - height + 1, right - left > 1 ? right - left - 1 : 1, height, col(K_PUR));
  }
  int px = x + 1, py = bottom;
  if (nb && !s_drain_start[start])
    py -= (s_drain_sum[start] - s_drain_hist[start]) * (gh - 3) / scale;
  for (int k = 0; k < nb; ++k) {
    const int idx = (start + k) % cfg::INFO_DRAIN_BARS;
    if (s_drain_start[idx]) py = bottom;
    const int xx = x + 1 + (k + 1) * (plotW - 1) / nb;
    const int yy = bottom - s_drain_sum[idx] * (gh - 3) / scale;
    s_cv.drawLine(px, py, xx, yy, col(K_GRN));
    px = xx;
    py = yy;
  }
  if (!nb) s_cv.drawFastHLine(x + 1, bottom, plotW, col(K_GRN));
  char span[8], amount[8];
  snprintf(span, sizeof span, "%dm", nb);
  snprintf(amount, sizeof amount, "%d%%", s_drain_session_total);
  txtAt(x, y + h - 10, span, K_DIM);
  txtAt(x + w - mixedWidth(amount), y + h - 10, amount, K_GRN);
}

void drawSys() {
  const Tile& t = s_t[T_SYS];
  const int x = t.x + PAD, w = t.w - 2 * PAD;
  if (!s_land) {
    const int gap = 6, half = (w - gap) / 2;
    chipPart(x, t.y + 1, half, t.h - 2);
    drainPart(x + half + gap, t.y + 1, w - half - gap, t.h - 2);
  } else {
    const int hh = (t.h - 2) / 2;
    chipPart(x, t.y + 1, w, hh);
    s_cv.drawFastHLine(t.x + 2, t.y + 1 + hh, t.w - 4, col(K_FRAME));
    drainPart(x, t.y + 2 + hh, w, t.h - 3 - hh);
  }
}

// ── LINK：只有 icon（Miles A7：不顯示 Wi-Fi 文字）。黑紅：啟用＝紅底黑圖，未啟用＝暗橘框 ──
enum { L_WIFI, L_CLOUD, L_BT, L_RF, L_NRF, L_IR, L_USB, L_MOUSE, L_GROVE, L_N, L_DBM = L_N };
void glyphWifi(int cx, int cy, uint16_t c, int bars) {
  for (int i = 0; i < 4; ++i) { const int h = 2 + i * 2; s_cv.fillRect(cx - 5 + i * 3, cy + 4 - h, 2, h, i < bars ? c : col(K_UNLIT)); }
}
void glyphCloud(int cx, int cy, uint16_t c) {
  s_cv.fillCircle(cx - 2, cy + 1, 2, c); s_cv.fillCircle(cx + 1, cy - 1, 3, c); s_cv.fillRect(cx - 4, cy + 1, 9, 3, c);
}
void glyphGrove(int cx, int cy, uint16_t c) {   // 插頭
  s_cv.fillRect(cx - 3, cy - 1, 7, 5, c); s_cv.drawFastVLine(cx - 2, cy - 4, 3, c); s_cv.drawFastVLine(cx + 2, cy - 4, 3, c);
}
void drawLink() {
  const Tile& t = s_t[T_ICON];
  if (!t.w) return;   // 橫式不顯示
  const bool up = net_hub::wifiUp();
  const int rssi = up ? (int)WiFi.RSSI() : 0;
  const int bars = !up ? 0 : rssi >= cfg::WIFI_BARS_4 ? 4 : rssi >= cfg::WIFI_BARS_3 ? 3 : rssi >= cfg::WIFI_BARS_2 ? 2 : 1;
  const bool on[L_N] = {up, net_hub::linkUp(), iconActive(IC_BT), iconActive(IC_RF), iconActive(IC_NRF), iconActive(IC_IR),
                        iconActive(IC_USB), iconActive(IC_MOUSE), input_hal::device() != GroveDevice::None};
  // 橫式（第五版）：Wi-Fi 後面多一格訊號強度 dBm，湊成 2 排×5
  const int nCells = s_land ? L_N + 1 : L_N;
  const int perRow = s_land ? 5 : 3, cell = 14, sz = cell - 2;
  const int rows = (nCells + perRow - 1) / perRow;
  const int x0 = t.x + (t.w - perRow * cell) / 2 + 1;
  const int y0 = t.y + (t.h - rows * cell) / 2 + 1;
  for (int c = 0; c < nCells; ++c) {
    const int bx = x0 + (c % perRow) * cell, by = y0 + (c / perRow) * cell;
    if (s_land && c == 1) {   // dBm 格：小字
      char db[8];
      if (up) snprintf(db, sizeof db, "%d", rssi); else strcpy(db, "--");
      s_cv.setFont(&fonts::TomThumb);
      s_cv.setTextColor(col(up ? K_TXT : K_DIM));
      s_cv.drawString(db, bx + (sz - s_cv.textWidth(db)) / 2, by + 4);
      continue;
    }
    const int i = s_land && c > 1 ? c - 1 : c;
    const int cx = bx + sz / 2, cy = by + sz / 2;
    uint16_t g;
    if (on[i]) { s_cv.fillRoundRect(bx, by, sz, sz, 2, col(K_RED)); g = col(K_BG); }
    else { s_cv.drawRoundRect(bx, by, sz, sz, 2, col(K_FRAME)); g = col(K_DIM); }
    switch (i) {
      case L_WIFI:  glyphWifi(cx, cy, g, up ? bars : 4); break;
      case L_CLOUD: glyphCloud(cx, cy, g); break;
      case L_GROVE: glyphGrove(cx, cy, g); break;
      default: drawIcon(i - L_BT, cx, cy, g); break;   // IC_BT..IC_MOUSE 順序相同
    }
  }
}

// ── 快訊 ──
void newsLine(char* out, size_t cap, const char* s, int maxChars) {
  const size_t n = info_data::utf8Prefix(out, cap, s, maxChars);
  if (s[n] && cap > 4) { const size_t m = info_data::utf8Prefix(out, cap - 3, s, maxChars - 1); snprintf(out + m, cap - m, "…"); }
}
void drawNews() {
  const Tile& t = s_t[T_NEWS];
  const int lw = hexLabel(T_NEWS, "NEWS");
  const info_data::News& nw = info_data::news();
  if (nw.n <= 0) { txt(T_NEWS, lw + 2, 1, "-", K_DIM); return; }
  const bool live = net_hub::linkUp();
  const int per = 2;
  const int pages = (nw.n + per - 1) / per;
  const long now = synced() ? (long)time(nullptr) : (long)(millis() / 1000);
  const int page = (int)((now / cfg::INFO_NEWS_PAGE_SEC) % pages);
  char at[8], pg[16], line[80];
  info_data::formatAt(at, sizeof at, nw.at);
  snprintf(pg, sizeof pg, "%d/%d", page + 1, pages);
  txt(T_NEWS, lw + 2, 1, at, live ? K_DIM : K_RED);
  txt(T_NEWS, 0, 1, pg, K_DIM, true);
  const int chars = (t.w - 2 * PAD) / 12;
  for (int k = 0; k < per; ++k) {
    const int i = page * per + k;
    if (i >= nw.n) break;
    newsLine(line, sizeof line, nw.item[i], chars);
    txtF(T_NEWS, 0, 12 + k * 13, line, live ? (k == 0 ? K_HI : K_TXT) : K_DIM, &fonts::efontTW_12);
  }
}

void drawAll(bool eva) {
  (void)eva;   // A7：資訊頁只剩一版
  const Orient o = ui_expression::orientation();
  const uint8_t rot = o == Orient::Portrait ? 0 : o == Orient::LandALeft ? cfg::ORIENT_ROT_A_LEFT : cfg::ORIENT_ROT_A_RIGHT;
  s_cv.setRotation(rot);
  s_drawn_orient = o;
  s_drawn_eva = eva;
  struct tm lt = {};
  const bool ok = synced();
  if (ok) { const time_t now = time(nullptr); localtime_r(&now, &lt); }
  layout(o);
  s_cv.fillScreen(col(K_BG));
  hazard(0, s_land ? 3 : 4);
  hazard(s_H - (s_land ? 3 : 4), s_land ? 3 : 4);
  for (int i = 0; i < T_N; ++i) frame(i);
  s_cv.setTextDatum(textdatum_t::top_left);
  drawClock(ok, lt);
  drawWeather();
  drawPwr();
  drawSys();
  drawLink();
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
  const bool eva = false;   // 11-21：只剩一版（EVA 角色刪除）
  const bool themeChanged = false;
  const bool turned = ui_expression::orientation() != s_drawn_orient;
  if (!s_dirty && !turned && !themeChanged && sec == s_last_sec) return;
  const uint32_t t0 = micros();
  drawAll(eva);                       // 畫在記憶體裡的畫布
  const uint32_t t1 = micros();
  ui_fx::present(&s_cv);           // 整張 64KB 經 SPI（40MHz）送上螢幕（換整頁時由 ui_fx 交叉淡化）
  const uint32_t t2 = micros();
  if (s_dirty || turned || themeChanged)
    Serial.printf("[ui] 資訊頁重畫（%s，%s）：畫 %.1f ms＋送上螢幕 %.1f ms\n",
                  themeChanged ? (eva ? "EVA" : "合成波") : s_dirty ? "切過來" : "轉向",
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
