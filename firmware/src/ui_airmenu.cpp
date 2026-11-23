#include "ui_airmenu.h"

#include <M5Unified.h>

#include "air_mouse.h"
#include "ble_mouse.h"
#include "config.h"
#include "input_hal.h"
#include "battery_ui.h"
#include "air_labels.h"
#include "kanji_bitmaps.h"
#include "ui_fx.h"
#include "ui_kit.h"

namespace {
enum class Page { Menu, Waiting, Mouse };
constexpr uint32_t kWaitMs = 30000;
constexpr uint32_t kScrollPollMs = 20;
constexpr uint32_t kLockHoldMs = 1000;
constexpr uint32_t kClutchHoldMs = 350;
constexpr uint32_t kExitOverlayMs = 800;
constexpr uint32_t kScrollDebounceMs = 25;
constexpr uint32_t kStaticRefreshMs = 250;
constexpr int kWidth = 135, kHeight = 240;
// 色票一律用 ui_kit（docs/design/README.md 第 1 節）
namespace kc = ui_kit::color;   // 不用 using namespace：Arduino 有全域 RED 等名稱會撞
constexpr uint16_t kAmber = kc::AMBER, kBright = kc::BRIGHT, kDim = kc::DIM, kFrame = kc::FRAME, kUnlit = kc::UNLIT,
                   kRed = kc::RED, kGreen = kc::GREEN, kYellow = kc::YELLOW;

M5Canvas s_canvas(&M5.Display);
bool s_active = false, s_dirty = true;
Page s_page = Page::Menu;
ble_mouse::Peer s_peers[ble_mouse::kMaxPeers];
int s_count = 0, s_selected = 0;
uint32_t s_started = 0, s_last_scroll = 0, s_a_since = 0;
int16_t s_scroll_count = 0;
bool s_scroll_valid = false, s_a_down = false;
bool s_scroll_raw = false, s_scroll_down = false, s_b_down = false;
uint32_t s_scroll_changed = 0, s_last_present = 0;
int s_exit_tenths = -1;
int s_seconds = -1;
const char* s_hint = nullptr;
// 已連線畫面的動態（11-23 Miles）：準星跟游標位移、按鍵時六角亮起
float s_rx = 0, s_ry = 0;        // 準星偏移（px）
int s_rpx = 0, s_rpy = 0;        // 上次畫的偏移
uint32_t s_viz_t = 0, s_flash_left = 0, s_flash_scroll = 0;
uint8_t s_keys = 0;              // 目前亮的六角（bit0 左、1 捲、2 右、3 停）
constexpr uint32_t kVizMs = 66;  // 約 15 張／秒，只在準星有在動或按鍵變化時才重畫
constexpr uint32_t kFlashMs = 200;

int optionCount() {
  return s_count + 1 + (s_count < ble_mouse::kMaxPeers ? 1 : 0);
}

const char* label(int index) {
  if (index < s_count) return s_peers[index].name;
  return index == s_count && s_count < ble_mouse::kMaxPeers ? "新增配對" : "臨時連線";
}

void menu(const char* hint) {
  ble_mouse::end();
  air_mouse::reset();
  s_page = Page::Menu;
  s_hint = hint;
  s_count = ble_mouse::pairedPeers(s_peers);
  if (s_selected >= optionCount()) s_selected = optionCount() - 1;
  s_scroll_valid = false;
  s_scroll_down = s_scroll_raw = s_b_down = false;
  s_exit_tenths = -1;
  s_a_down = false;
  s_rx = s_ry = 0; s_rpx = s_rpy = 0; s_keys = 0;
  s_dirty = true;
  ui_fx::invalidate();
}

void lock() {
  s_page = Page::Waiting;
  s_started = millis();
  s_seconds = -1;
  s_dirty = true;
  using ble_mouse::ConnectMode;
  if (s_selected < s_count)
    ble_mouse::begin(ConnectMode::Saved, &s_peers[s_selected]);
  else if (s_selected == s_count && s_count < ble_mouse::kMaxPeers)
    ble_mouse::begin(ConnectMode::Add);
  else
    ble_mouse::begin(ConnectMode::Temporary);
}

void move(int delta) {
  const int n = optionCount();
  s_selected = ((s_selected + delta) % n + n) % n;
  s_hint = nullptr;
  s_dirty = true;
}

// ── 畫圖（11-23 v4：跟表情頁／介面規格同一套——狀態列「MODE 04・體感」＋電量、雷達、六角徽章、底部斜紋帶）──

void textAt(const char* s, int x, int y, uint16_t color, const lgfx::IFont* font, int maxWidth = 0,
            textdatum_t datum = top_left) {
  ui_kit::textFit(s_canvas, s, x, y, color, font, maxWidth, datum);
}
// 小字混排（ui_kit：英文 Font0、數字粗、中文 efontTW_10）。回傳寬度。
int mixed(const char* s, int x, int y, uint16_t color) { return ui_kit::drawMixed(s_canvas, x, y, s, color); }

// 狀態列（ui_kit，同表情頁）：MODE 04・體感＋右邊電量 %
void statusBar() {
  const int lv = battery_ui::level();
  ui_kit::statusBar(s_canvas, 0, kWidth, "MODE 04", "體感", kAmber, lv, ui_kit::batteryColor(lv));
}

// 固定字：Noto Serif CJK TC Black 點陣圖（air_labels.h，同 B 切模式過場的字型）。查不到就退回 efontTW_12。
// datum：0＝左上、1＝置中（x、y 為中心）、2＝右上。回傳寬度。
int lbl(const char* text, int x, int y, uint16_t color, int datum = 0) {
  if (const air_labels::Bmp* g = air_labels::find(text)) {
    const int gx = datum == 1 ? x - g->w / 2 : datum == 2 ? x - g->w : x;
    const int gy = datum == 1 ? y - g->h / 2 : y;
    s_canvas.drawBitmap(gx, gy, g->bits, g->w, g->h, color);
    return g->w;
  }
  // 提示句與選項名稱走這裡：efontTW_12（11-23 Miles：閱讀用的字不用粗明體），往下 1px 跟 13px 點陣圖置中
  textAt(text, x, datum == 1 ? y : y + 1, color, &fonts::efontTW_12, 0, datum == 1 ? middle_center : datum == 2 ? top_right : top_left);
  s_canvas.setFont(&fonts::efontTW_12);
  return s_canvas.textWidth(text);
}
// 裝置名稱：efontTW_12（同表情頁對話字，11-23 Miles：不加粗），太長加「…」
void plainName(const char* s, int x, int y, uint16_t color, int maxWidth) {
  textAt(s, x, y, color, &fonts::efontTW_12, maxWidth);
}

// 底部斜紋帶（同介面規格「A 發射」）：斜紋＋中間黑底字
constexpr int kFootH = 18, kFootY = kHeight - kFootH;
// 斜紋同切模式（ui_kit::actionBand）；破壞性頁面紅色
void footer(const char* label, uint16_t color) {
  const air_labels::Bmp* g = air_labels::find(label);
  ui_kit::actionBand(s_canvas, kFootY, kWidth, kFootH, color, color == kRed ? kRed : kAmber,
                     g ? g->bits : nullptr, g ? g->w : 0, g ? g->h : 0);
}

void hexShape(int cx, int cy, int a, uint16_t c, bool fill) { ui_kit::hexShape(s_canvas, cx, cy, a, c, fill); }
void seg7(int x, int y, int w, int h, int n, uint16_t on) { ui_kit::seg7(s_canvas, x, y, w, h, n, on); }

// 雷達（同介面規格空中滑鼠）：三圈＋十字＋外圈刻度。progress 0～1000＝外圈亮起比例（順時針、從 12 點）
constexpr int kRadarX = 67, kRadarY = 54, kRadarR = 35;
// locked＝已連線：圈圈綠黃交錯；rx／ry＝準星偏移；dimReticle＝游標暫停中
void radar(uint16_t line, int progress, uint16_t arc, int sweepDeg, bool reticle,
           bool locked = false, int rx = 0, int ry = 0, bool dimReticle = false) {
  const int cx = kRadarX, cy = kRadarY, r = kRadarR;
  s_canvas.drawCircle(cx, cy, r, locked ? kGreen : line);
  s_canvas.drawCircle(cx, cy, r * 6 / 10, locked ? kYellow : line);
  s_canvas.drawCircle(cx, cy, 10, locked ? kGreen : line);
  s_canvas.drawFastHLine(cx - r - 4, cy, 2 * r + 9, line);
  s_canvas.drawFastVLine(cx, cy - r - 4, 2 * r + 9, line);
  for (int d = 0; d < 360; d += 30) {
    const float a = d * 0.0174533f;
    s_canvas.drawLine(cx + int(sinf(a) * (r - 3)), cy - int(cosf(a) * (r - 3)),
                      cx + int(sinf(a) * r), cy - int(cosf(a) * r), locked ? kYellow : line);
  }
  if (progress > 0) s_canvas.fillArc(cx, cy, r + 3, r + 1, 270, 270 + progress * 360 / 1000, arc);   // 0°＝3 點鐘、順時針
  if (sweepDeg >= 0)
    for (int k = 0; k < 4; ++k) {   // 掃描線＋三道殘影
      const float a = (sweepDeg - k * 7) * 0.0174533f;
      s_canvas.drawLine(cx, cy, cx + int(sinf(a) * (r - 2)), cy - int(cosf(a) * (r - 2)),
                        k == 0 ? kAmber : k == 1 ? kDim : kFrame);
    }
  if (reticle) {   // 準星（介面規格的游標框）
    const int q = 6;
    const uint16_t rc = dimReticle ? kDim : kAmber;
    const int ox = cx + rx, oy = cy + ry;
    if (rx || ry) s_canvas.drawLine(cx, cy, ox, oy, kDim);   // 從中心拉一條線＝這一刻往哪動
    for (int sx = -1; sx <= 1; sx += 2)
      for (int sy = -1; sy <= 1; sy += 2) {
        s_canvas.drawFastHLine(sx < 0 ? ox - q : ox + q - 2, oy + sy * q, 3, rc);
        s_canvas.drawFastVLine(ox + sx * q, sy < 0 ? oy - q : oy + q - 2, 3, rc);
      }
    s_canvas.fillRect(ox - 1, oy - 1, 3, 3, rc);
  }
}

// 「BLE ● 狀態」；solid＝綠底黑字（已連線）
void bleLine(int x, int y, const char* state, uint16_t color, bool solid) {
  int w = mixed("BLE", x, y + 3, color);
  s_canvas.fillCircle(x + w + 5, y + 7, 2, color);
  w += 11;
  if (solid) {
    const air_labels::Bmp* g = air_labels::find(state);
    const int tw = (g ? g->w : 40) + 8;
    s_canvas.fillRect(x + w, y - 1, tw, 18, color);
    lbl(state, x + w + 4, y + 1, TFT_BLACK);
  } else {
    lbl(state, x + w, y, color);
  }
}

// ── 選單 ──
constexpr int kRowY = 60, kRowPitch = 31;
void drawMenu() {
  if (const kanji::Bmp* g = kanji::find("air")) s_canvas.drawBitmap(3, 17, g->bits, g->w, g->h, kRed);   // 同 B 切模式過場：紅色明朝大字
  textAt("AIR MOUSE", 78, 17, kDim, &fonts::Font0);
  int w = mixed("BLE", 78, 27, kDim);
  s_canvas.fillCircle(78 + w + 5, 31, 2, kDim);
  lbl("關閉", 78 + w + 11, 24, kDim);
  lbl("選擇裝置", 78, 39, kBright);
  s_canvas.drawFastHLine(4, 57, kWidth - 8, kFrame);

  for (int i = 0; i < optionCount(); ++i) {
    const int y = kRowY + kRowPitch * i;
    const bool on = i == s_selected;
    char num[4], sub[24];
    const char* mark;
    const char* tag;
    if (i < s_count) {
      snprintf(num, sizeof num, "%02d", i + 1);
      mark = num; tag = "PAIRED";
      snprintf(sub, sizeof sub, "已配對");
    } else if (i == s_count && s_count < ble_mouse::kMaxPeers) {
      mark = "+"; tag = "NEW";
      snprintf(sub, sizeof sub, "存成第 %d 台", s_count + 1);
    } else {
      mark = "~"; tag = "TEMP";
      snprintf(sub, sizeof sub, s_count >= ble_mouse::kMaxPeers ? "已滿 不存" : "不存配對");
    }
    if (on) s_canvas.fillRect(1, y - 1, kWidth - 2, kRowPitch - 2, kUnlit);
    hexShape(14, y + 14, 11, on ? kAmber : kDim, on);
    textAt(mark, 15, y + 15, on ? TFT_BLACK : kDim, &fonts::Font0, 0, middle_center);
    if (i < s_count) plainName(label(i), 30, y + 1, on ? kBright : kAmber, kWidth - 33);
    else lbl(label(i), 30, y, on ? kBright : kAmber);
    const int tw = mixed(tag, 30, y + 16, on ? kDim : kFrame);
    lbl(sub, 30 + tw + 4, y + 14, on ? kDim : kFrame);
  }
  const bool hasScroll = input_hal::device() == GroveDevice::Scroll;
  lbl(hasScroll ? "滾輪 選擇" : "A 短按 下一項", 4, 186, kDim);
  if (s_hint) {   // 錯誤暫時蓋掉「B 切下一模式」，換項後就消失
    hexShape(10, 210, 7, kRed, true);
    textAt("!", 11, 211, TFT_BLACK, &fonts::Font0, 0, middle_center);
    lbl(s_hint, 21, 203, kRed);
  } else {
    lbl("B 切下一模式", 4, 203, kDim);
  }
  footer(hasScroll ? "A 連線" : "A 按住 1 秒 連線", kYellow);
}

// 下方共用：左邊 SEC＋兩位七段（tenths＝中間加小數點）
void secDigits(int value, bool tenths, uint16_t color) {
  textAt("SEC", 4, 105, kDim, &fonts::Font0);
  seg7(4, 115, 12, 22, (value / 10) % 10, color);
  if (tenths) s_canvas.fillRect(18, 135, 2, 2, color);
  seg7(tenths ? 22 : 18, 115, 12, 22, value % 10, color);
}

// ── 等待連線 ──
void drawWaiting() {
  const int left = s_seconds < 0 ? kWaitMs / 1000 : s_seconds;
  radar(kFrame, left * 1000 / (kWaitMs / 1000), kAmber, (30 - left) * 36 % 360, false);
  secDigits(left, false, kAmber);
  bleLine(38, 103, "廣播中", kYellow, false);
  const bool saved = s_selected < s_count;
  const bool add = s_selected == s_count && s_count < ble_mouse::kMaxPeers;
  if (saved) plainName(s_peers[s_selected].name, 38, 122, kBright, kWidth - 41);
  else lbl(label(s_selected), 38, 120, kBright);
  lbl(saved ? "已配對" : add ? "存成新配對" : "不存配對", 38, 138, kDim);
  s_canvas.drawFastHLine(4, 157, kWidth - 8, kFrame);
  if (saved) {
    lbl("等待主機自動回連", 4, 163, kAmber);
    lbl("或在主機按連線", 4, 182, kDim);
  } else {
    const int w = lbl("主機藍牙選", 4, 163, kAmber);
    textAt(cfg::BLE_DEVICE_NAME, 4 + w + 4, 167, kGreen, &fonts::Font0);
    textAt(cfg::BLE_DEVICE_NAME, 5 + w + 4, 167, kGreen, &fonts::Font0);
    lbl("30 秒內沒連上回選單", 4, 182, kDim);
  }
  lbl("B 切下一模式", 4, 201, kDim);
  footer("等待連線", kYellow);
}

// ── 已連線：按鍵說明 ──
// 六角按鍵（介面規格 左／捲／右）：大字＋下面標按哪顆；沒有的功能畫暗框
constexpr int kKeyY = 152;
// pressed＝正在按：亮白底＋外圈黃框（11-23 Miles：按鍵時六角要有反應）
void keyHex(int cx, const char* word, const char* key, bool on, bool pressed = false) {
  hexShape(cx, kKeyY, 15, pressed ? kBright : on ? kAmber : kFrame, on || pressed);
  if (pressed) hexShape(cx, kKeyY, 17, kYellow, false);
  lbl(word, cx + 1, kKeyY + 1, on || pressed ? TFT_BLACK : kFrame, 1);
  lbl(key, cx, kKeyY + 23, on ? kDim : kFrame, 1);
}

void drawMouse() {
  radar(kFrame, 0, 0, -1, true, true, s_rpx, s_rpy, s_keys & 8);
  bleLine(4, 99, "已連線", kGreen, true);
  if (s_selected < s_count) plainName(label(s_selected), 4, 121, kBright, kWidth - 8);
  else lbl(label(s_selected), 4, 119, kBright);
  const bool hasScroll = input_hal::device() == GroveDevice::Scroll;
  // 下標要放在六角下方：lbl 置中模式以 y 為中心 → 傳中心 y
  if (hasScroll) {
    keyHex(18, "左", "按輪", true, s_keys & 1);
    keyHex(51, "捲", "轉輪", true, s_keys & 2);
    keyHex(84, "右", "B", true, s_keys & 4);
    keyHex(117, "停", "A長", true, s_keys & 8);
    lbl("按住滾輪移動＝拖曳", 4, 186, kDim);
  } else {
    keyHex(18, "左", "A短", true, s_keys & 1);
    keyHex(51, "捲", "無", false);
    keyHex(84, "右", "B", true, s_keys & 4);
    keyHex(117, "停", "A長", true, s_keys & 8);
    lbl("A 短按左鍵 按住暫停", 4, 186, kDim);
  }
  lbl("放開 A 重設方向", 4, 203, kDim);
  footer("B 按住 3 秒 回選單", kYellow);
}

// ── B 長按倒數 ──
void drawExit() {
  constexpr int total = cfg::BTN_B_KILL_MS / 100;
  radar(kFrame, s_exit_tenths * 1000 / total, kRed, -1, true);
  secDigits(total - s_exit_tenths, true, kRed);
  lbl("按住 B", 42, 103, kBright);
  lbl("中斷連線", 42, 121, kAmber);
  textAt(label(s_selected), 42, 140, kDim, &fonts::efontTW_10, kWidth - 45);
  s_canvas.drawFastHLine(4, 157, kWidth - 8, kFrame);
  lbl("放開取消 不送右鍵", 4, 165, kDim);
  lbl("斷線後回選單", 4, 185, kDim);
  footer("放開 取消", kRed);
}

void drawLink() {
  if (s_page == Page::Waiting) drawWaiting();
  else drawMouse();
}

}

namespace ui_airmenu {
void enter() {
  s_active = true;
  s_selected = 0;
  menu(nullptr);
}

void exit() {
  s_active = false;
  s_a_down = false;
  ble_mouse::end();
  s_canvas.deleteSprite();
  ui_fx::invalidate();
}

bool connected() { return s_active && s_page == Page::Mouse; }

void onA(bool down) {
  if (!s_active) return;
  if (connected()) {
    if (down) {
      s_a_down = true;
      s_a_since = millis();
      air_mouse::setClutch(true);
    } else if (s_a_down) {
      s_a_down = false;
      const bool recenter = input_hal::device() == GroveDevice::Scroll ||
                            millis() - s_a_since >= kClutchHoldMs;
      air_mouse::setClutch(false, recenter);
      if (!recenter) {
        air_mouse::click(ble_mouse::BTN_LEFT);
        s_flash_left = millis() + kFlashMs;
      }
    }
    return;
  }
  if (s_page != Page::Menu) return;
  const bool hasScroll = input_hal::device() == GroveDevice::Scroll;
  if (down) {
    if (hasScroll) lock();
    else {
      s_a_down = true;
      s_a_since = millis();
    }
  } else if (s_a_down) {
    s_a_down = false;
    if (millis() - s_a_since >= kLockHoldMs) lock();
    else move(1);
  }
}

bool onBRelease() {
  if (!connected()) return false;
  air_mouse::freezeMovement();
  if (input_hal::bReleaseDuration() < kExitOverlayMs) air_mouse::click(ble_mouse::BTN_RIGHT);
  return true;
}

bool returnFromStop() {
  if (!s_active || s_page == Page::Menu) return false;
  menu("已斷線");
  return true;
}

void loop() {
  if (!s_active) return;
  const uint32_t now = millis();
  if (s_page == Page::Mouse && !ble_mouse::secured()) {
    menu("連線已中斷");
    return;
  }
  if (s_page == Page::Mouse) {
    if (s_a_down && !M5.BtnA.isPressed()) onA(false);
    const uint32_t held = input_hal::bHeldMs();
    const bool down = M5.BtnB.isPressed();
    if (down != s_b_down) {
      s_b_down = down;
      air_mouse::freezeMovement();
    }
    const int tenths = down && held >= kExitOverlayMs ? min(held, cfg::BTN_B_KILL_MS) / 100 : -1;
    if (tenths != s_exit_tenths) {
      if (tenths < 0 || s_exit_tenths < 0) ui_fx::invalidate();
      s_exit_tenths = tenths;
      s_dirty = true;
    }
  }
  if (s_page == Page::Mouse && now - s_viz_t >= kVizMs) {
    s_viz_t = now;
    int dx, dy;
    air_mouse::takeMotion(dx, dy);
    // 準星：每張先往中心收（彈簧），再加上這段時間的位移；畫面距離＝游標點數 × 0.15，限制在雷達內圈外緣
    constexpr float kLimit = kRadarR - 10;
    s_rx = constrain(s_rx * 0.7f + dx * 0.15f, -kLimit, kLimit);
    s_ry = constrain(s_ry * 0.7f + dy * 0.15f, -kLimit, kLimit);
    const int px = lroundf(s_rx), py = lroundf(s_ry);
    const bool hasScroll = input_hal::device() == GroveDevice::Scroll;
    uint8_t keys = 0;
    if (hasScroll ? s_scroll_down : int32_t(s_flash_left - now) > 0) keys |= 1;
    if (int32_t(s_flash_scroll - now) > 0) keys |= 2;
    if (s_b_down) keys |= 4;
    if (s_a_down && (hasScroll || now - s_a_since >= kClutchHoldMs)) keys |= 8;
    if (px != s_rpx || py != s_rpy || keys != s_keys) {
      s_rpx = px; s_rpy = py; s_keys = keys;
      s_dirty = true;
    }
  }
  if (s_page == Page::Waiting) {
    if (ble_mouse::secured()) {
      s_page = Page::Mouse;
      s_scroll_valid = false;
      s_dirty = true;
    } else if (!ble_mouse::starting() && ble_mouse::state() == ble_mouse::State::Off) {
      menu("藍牙啟動失敗");
    } else if (now - s_started >= kWaitMs) {
      menu("連線逾時");
    } else {
      const int seconds = (kWaitMs - (now - s_started) + 999) / 1000;
      if (s_seconds != seconds) { s_seconds = seconds; s_dirty = true; }
    }
    return;
  }
  if (s_page == Page::Menu && s_a_down && M5.BtnA.isPressed() && now - s_a_since >= kLockHoldMs) {
    s_a_down = false;
    lock();
    return;
  }
  if (input_hal::device() == GroveDevice::Scroll && now - s_last_scroll >= kScrollPollMs) {
    s_last_scroll = now;
    int16_t count;
    bool pressed;
    if (!input_hal::readScroll(count, pressed)) {
      s_scroll_valid = false;
      if (s_scroll_down) {
        s_scroll_down = false;
        air_mouse::setButton(ble_mouse::BTN_LEFT, false);
      }
      return;
    }
    const int delta = s_scroll_valid ? static_cast<int16_t>(count - s_scroll_count) : 0;
    s_scroll_count = count;
    s_scroll_valid = true;
    if (s_page == Page::Menu) {
      if (delta) move(-delta);
    } else {
      if (pressed != s_scroll_raw) {
        s_scroll_raw = pressed;
        s_scroll_changed = now;
        air_mouse::freezeMovement();
      }
      if (s_scroll_down != s_scroll_raw && now - s_scroll_changed >= kScrollDebounceMs) {
        s_scroll_down = s_scroll_raw;
        air_mouse::setButton(ble_mouse::BTN_LEFT, s_scroll_down);
      }
      if (delta && pressed == s_scroll_down) {
        air_mouse::scroll(delta);
        s_flash_scroll = now + kFlashMs;
      }
    }
  }
}

bool render() {
  if (!s_active) return false;
  if (!s_canvas.getBuffer()) {
    s_canvas.setPsram(true);
    s_canvas.setColorDepth(16);
    if (!s_canvas.createSprite(kWidth, kHeight)) return false;
  }
  const uint32_t now = millis();
  // 靜態畫面不必每圈比對整張 PSRAM 畫布。定期重送以恢復電源鍵蓋過的畫面。
  if (!s_dirty && now - s_last_present < kStaticRefreshMs) return true;
  if (s_dirty) {
    s_dirty = false;
    s_canvas.fillScreen(TFT_BLACK);
    statusBar();
    if (s_exit_tenths >= 0) drawExit();
    else if (s_page == Page::Menu) drawMenu();
    else drawLink();
  }
  s_last_present = now;
  ui_fx::present(&s_canvas);
  return true;
}
}
