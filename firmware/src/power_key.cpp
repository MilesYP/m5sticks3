#include "power_key.h"

#include <M5Unified.h>
#include <math.h>

#include "config.h"
#include "power_save.h"
#include "ui_fx.h"
#include "ui_labels.h"

namespace {
bool     s_ready = false;
bool     s_root = false;
bool     s_armed = false;          // 開機後看到「放開」夠久才開始判斷
uint32_t s_release_since = 0;      // 還沒 armed 時：這次「放開」從什麼時候開始
uint32_t s_last_poll = 0;
uint32_t s_last_feed = 0;
bool     s_down = false;
uint32_t s_down_t = 0;
uint32_t s_last_click_t = 0;
uint8_t  s_clicks = 0;
bool     s_overlay = false;
bool     s_overlay_screen_was_on = true;
int      s_overlay_phase = -1;
uint32_t s_root_msg_until = 0;     // 剛進 root：訊息畫面顯示到這個時刻
uint8_t  s_raw = 0xFF;             // 上次讀到的 BTN_STATUS（印變化用）

bool rd(uint8_t reg, uint8_t* v) {
  return M5.In_I2C.readRegister(cfg::PM1_ADDR, reg, v, 1, cfg::PM1_I2C_HZ);
}
bool wr(uint8_t reg, uint8_t v) {
  return M5.In_I2C.writeRegister8(cfg::PM1_ADDR, reg, v, cfg::PM1_I2C_HZ);
}
// 寫完讀回，最多試 3 次
bool wrVerify(uint8_t reg, uint8_t v) {
  for (int i = 0; i < 3; ++i) {
    uint8_t r = 0;
    if (wr(reg, v) && rd(reg, &r) && r == v) return true;
    delay(2);
  }
  return false;
}
bool setBits(uint8_t reg, uint8_t mask, bool on) {
  uint8_t v = 0;
  if (!rd(reg, &v)) return false;
  return wrVerify(reg, on ? (v | mask) : (v & ~mask));
}

// 新機制／root 的 PM1 設定
bool applyMode(bool root) {
  bool ok = true;
  ok &= setBits(cfg::PM1_REG_BTN_CFG1, 0x01, !root);   // SINGLE_RST_DIS
  ok &= setBits(cfg::PM1_REG_BTN_CFG1, 0x80, !root);   // DL_LOCK
  ok &= setBits(cfg::PM1_REG_BTN_CFG2, 0x01, !root);   // DOUBLE_OFF_DIS
  ok &= wrVerify(cfg::PM1_REG_WDT_CNT, root ? 0 : cfg::PWR_WDT_SEC);
  if (!root) wr(cfg::PM1_REG_WDT_KEY, cfg::PM1_WDT_FEED_KEY);
  return ok;
}

// ── 畫面（直接畫在螢幕上，不經畫布；結束後要 ui_fx::invalidate()）──
void screenOn() {
  M5.Display.wakeup();
  M5.Display.setBrightness(cfg::PWR_BL_AWAKE);
}

void message(const char* line1, const char* line2, uint16_t color) {
  screenOn();
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setFont(&fonts::efontTW_12);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(color, TFT_BLACK);
  const int cx = M5.Display.width() / 2, cy = M5.Display.height() / 2;
  if (line1) M5.Display.drawString(line1, cx, cy - 12);
  if (line2) M5.Display.drawString(line2, cx, cy + 12);
}

// ── 長按畫面（11-22 Miles 定案，模擬圖 C_alt.gif）──
// 四邊警示斜紋（紅黑／黃黑每 0.4 秒交替閃、跟著時間捲動）＋淡六角格底＋中間六角框：
// 外圈＝進度（頂邊起順時針，5 秒一圈；3 秒前琥珀、之後紅；底邊白刻度＝3 秒）、框內大字＋七段秒數（一位小數、置中）、
// 下方實心標籤（放開會怎樣）。畫在自己的畫布、經 ui_fx::present 只送變動的部分；每 100ms 才重畫。
M5Canvas s_cv(&M5.Display);
bool     s_cv_ok = false;
uint32_t s_overlay_tick = 0xFFFFFFFF;
uint16_t C_AMB, C_RED, C_YEL, C_DIM, C_GRID, C_RING, C_GHOST, C_WHT;

constexpr int   FR_T = 7;          // 斜紋寬
constexpr int   HEX_CX = 67, HEX_CY = 104, HEX_R = 46;
constexpr float RING_SPLIT = 6.0f * cfg::PWR_KEY_OFF_MS / cfg::PWR_KEY_ROOT_MS;   // 3 秒＝3.6 邊

bool cvBegin() {
  if (s_cv_ok) return true;
  s_cv.setPsram(true);
  s_cv.setColorDepth(16);
  s_cv_ok = s_cv.createSprite(M5.Display.width(), M5.Display.height()) != nullptr;
  C_AMB = s_cv.color565(0xFF, 0x9B, 0x00);
  C_RED = s_cv.color565(0xFF, 0x29, 0x23);
  C_YEL = s_cv.color565(0xFF, 0xD0, 0x00);   // 同 B 切模式過場的黃
  C_DIM = s_cv.color565(0x5A, 0x37, 0x00);
  C_GRID = s_cv.color565(0x20, 0x14, 0x00);
  C_RING = s_cv.color565(0x3C, 0x23, 0x00);
  C_GHOST = s_cv.color565(0x1E, 0x12, 0x00);
  C_WHT = s_cv.color565(0xFF, 0xEB, 0xC8);
  return s_cv_ok;
}
void cvEnd() {
  if (s_cv_ok) s_cv.deleteSprite();
  s_cv_ok = false;
}

void hexPt(float cx, float cy, float r, int i, float* x, float* y) {   // 平頂六角：i＝0 右、1 右下…（每 60°）
  const float a = i * 1.0471976f;
  *x = cx + r * cosf(a);
  *y = cy + r * sinf(a);
}

// 斜紋：同一條公式畫四邊，轉角自然接上。(x＋y＋off) mod 10 < 5 是條紋色
void stripeBand(int x0, int y0, int w, int h, int off, uint16_t col) {
  s_cv.fillRect(x0, y0, w, h, TFT_BLACK);
  for (int y = y0; y < y0 + h; ++y) {
    int run = -1;
    for (int x = x0; x <= x0 + w; ++x) {
      const bool on = x < x0 + w && ((x + y + off) % 10 + 10) % 10 < 5;
      if (on && run < 0) run = x;
      if (!on && run >= 0) { s_cv.drawFastHLine(run, y, x - run, col); run = -1; }
    }
  }
}

// 七段數字：段長 8、粗 2、段間 1px 縫；格 14×26
void seg7(int x, int y, int digit, uint16_t on, uint16_t off) {
  static const uint8_t kMask[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};   // bit0 a … bit6 g
  constexpr int t = 2, s = 8;
  const uint8_t m = kMask[digit % 10];
  auto seg = [&](int bit, int sx, int sy, int w, int h) { s_cv.fillRect(sx, sy, w, h, (m >> bit) & 1 ? on : off); };
  seg(0, x + t + 1, y, s, t);                          // a
  seg(1, x + t + s + 2, y + t + 1, t, s);              // b
  seg(2, x + t + s + 2, y + 2 * t + s + 3, t, s);      // c
  seg(3, x + t + 1, y + 2 * t + 2 * s + 4, s, t);      // d
  seg(4, x, y + 2 * t + s + 3, t, s);                  // e
  seg(5, x, y + t + 1, t, s);                          // f
  seg(6, x + t + 1, y + t + s + 2, s, t);              // g
}

void overlayDraw(uint32_t held) {
  const uint32_t tick = held / 100;   // 每 100ms 才重畫
  if (tick == s_overlay_tick || !cvBegin()) return;
  s_overlay_tick = tick;
  const int W = s_cv.width(), H = s_cv.height();
  const int phase = held >= cfg::PWR_KEY_ROOT_MS ? 2 : held >= cfg::PWR_KEY_OFF_MS ? 1 : 0;
  const uint16_t col = phase == 2 ? C_RED : C_AMB;

  // 底：黑＋淡六角格（每格畫上半三條邊）
  s_cv.fillScreen(TFT_BLACK);
  constexpr float A = 9.0f, HH = A * 0.8660254f;
  for (int c = -1; c * 1.5f * A < W + A; ++c)
    for (int k = -1; k * 2 * HH < H + HH; ++k) {
      const float cx = c * 1.5f * A, cy = k * 2 * HH + ((c & 1) ? HH : 0);
      float x0, y0, x1, y1;
      for (int i = 3; i < 6; ++i) {
        hexPt(cx, cy, A, i, &x0, &y0);
        hexPt(cx, cy, A, i + 1, &x1, &y1);
        s_cv.drawLine((int)x0, (int)y0, (int)x1, (int)y1, C_GRID);
      }
    }

  // 四邊斜紋：紅黑／黃黑每 0.4 秒交替，跟著時間捲動
  const uint16_t sc = ((held / 400) & 1) ? C_YEL : C_RED;
  const int off = (int)(held / 50);
  stripeBand(0, 0, W, FR_T, off, sc);
  stripeBand(0, H - FR_T, W, FR_T, off, sc);
  stripeBand(0, FR_T, FR_T, H - 2 * FR_T, off, sc);
  stripeBand(W - FR_T, FR_T, FR_T, H - 2 * FR_T, off, sc);
  s_cv.setFont(&fonts::Font0);
  s_cv.setTextDatum(top_left);
  s_cv.setTextColor(C_DIM);
  s_cv.drawString("POWER //", FR_T + 4, FR_T + 4);

  // 六角框：先塗黑蓋掉格線，外圈進度
  float px[6], py[6];
  for (int i = 0; i < 6; ++i) hexPt(HEX_CX, HEX_CY, HEX_R, i, &px[i], &py[i]);
  for (int i = 0; i < 6; ++i)
    s_cv.fillTriangle(HEX_CX, HEX_CY, (int)px[i], (int)py[i], (int)px[(i + 1) % 6], (int)py[(i + 1) % 6], TFT_BLACK);
  const float frac = 6.0f * (held > cfg::PWR_KEY_ROOT_MS ? cfg::PWR_KEY_ROOT_MS : held) / cfg::PWR_KEY_ROOT_MS;
  for (int i = 0; i < 6; ++i) {   // 頂邊（左上頂點→右上頂點）起順時針
    const int ia = (i + 4) % 6, ib = (i + 5) % 6;
    const float ax = px[ia], ay = py[ia], bx = px[ib], by = py[ib];
    auto L = [&](float t0, float t1, uint16_t c) {
      s_cv.drawWideLine((int)(ax + (bx - ax) * t0), (int)(ay + (by - ay) * t0),
                        (int)(ax + (bx - ax) * t1), (int)(ay + (by - ay) * t1), 2.5f, c);
    };
    L(0, 1, C_RING);
    const float f = frac - i < 0 ? 0 : frac - i > 1 ? 1 : frac - i;
    if (f > 0) {
      float cut = RING_SPLIT - i;
      cut = cut < 0 ? 0 : cut > f ? f : cut;
      if (cut > 0) L(0, cut, C_AMB);
      if (f > cut) L(cut, f, C_RED);
    }
    if (i == (int)RING_SPLIT) {   // 3 秒刻度（白）
      const float t = RING_SPLIT - i;
      const int tx = (int)(ax + (bx - ax) * t), ty = (int)(ay + (by - ay) * t);
      s_cv.fillRect(tx - 1, ty - 6, 2, 13, C_WHT);
    }
  }
  float qx[6], qy[6];
  for (int i = 0; i < 6; ++i) hexPt(HEX_CX, HEX_CY, HEX_R - 8, i, &qx[i], &qy[i]);
  for (int i = 0; i < 6; ++i) s_cv.drawLine((int)qx[i], (int)qy[i], (int)qx[(i + 1) % 6], (int)qy[(i + 1) % 6], C_RING);

  // 框內：大字＋秒數（一位小數，數字置中，s 掛在右下）
  if (const ui_labels::Bmp* b = ui_labels::find(phase == 2 ? "pk_root" : phase == 1 ? "pk_stop" : "pk_power"))
    s_cv.drawBitmap(HEX_CX - b->w / 2, HEX_CY - 30, b->bits, b->w, b->h, col);
  const uint32_t ds = (held > cfg::PWR_KEY_ROOT_MS ? cfg::PWR_KEY_ROOT_MS : held) / 100;   // 0.1 秒為單位
  constexpr int DW = 14, GAP = 3, DOT = 2;
  const int x0 = HEX_CX - (DW * 2 + GAP * 2 + DOT) / 2, y0 = HEX_CY - 2;
  seg7(x0, y0, (int)(ds / 10), col, C_GHOST);
  s_cv.fillRect(x0 + DW + GAP, y0 + 24, DOT, 2, col);
  seg7(x0 + DW + GAP * 2 + DOT, y0, (int)(ds % 10), col, C_GHOST);
  s_cv.setTextDatum(bottom_left);
  s_cv.setTextColor(C_DIM);
  s_cv.drawString("s", x0 + DW * 2 + GAP * 2 + DOT + 2, y0 + 26);

  // 下方：實心標籤＋提示
  const char* lab = phase == 2 ? "放開＝進 ROOT" : phase == 1 ? "放開＝關機" : "繼續按住…";
  s_cv.setFont(&fonts::efontTW_12);
  const int tw = s_cv.textWidth(lab);
  s_cv.fillRect(W / 2 - tw / 2 - 6, 176, tw + 12, 18, col);
  s_cv.setTextDatum(middle_center);
  s_cv.setTextColor(TFT_BLACK);
  s_cv.drawString(lab, W / 2, 185);
  s_cv.setFont(&fonts::efontTW_10);
  s_cv.setTextColor(C_DIM);
  s_cv.drawString("3秒關機 / 5秒ROOT", W / 2, 210);

  ui_fx::present(&s_cv);
}

void overlayEnd() {
  s_overlay = false;
  s_overlay_phase = -1;
  s_overlay_tick = 0xFFFFFFFF;
  cvEnd();
  if (!s_overlay_screen_was_on) {   // 原本螢幕是關的：關回去
    M5.Display.setBrightness(0);
    M5.Display.sleep();
  } else {
    power_save::reapplyBacklight();
  }
  ui_fx::invalidate();
}

void onClick(uint32_t now) {
  if (now - s_last_click_t < cfg::PWR_KEY_MULTI_GAP_MS) ++s_clicks;
  else s_clicks = 1;
  s_last_click_t = now;
  if (s_clicks >= cfg::PWR_KEY_FORCE_OFF_CLICKS) {
    power_key::shutdown("連按五下", "強制關機", nullptr);
  }
  power_save::toggleScreen("單按電源鍵");
}

void onRelease(uint32_t now, uint32_t held) {
  if (s_overlay) {
    if (held >= cfg::PWR_KEY_ROOT_MS) {
      s_overlay = false;
      s_overlay_phase = -1;
      s_overlay_tick = 0xFFFFFFFF;
      cvEnd();
      power_key::setRoot(true);
      return;
    }
    if (held >= cfg::PWR_KEY_OFF_MS) power_key::shutdown("長按電源鍵", "關機", nullptr);
    Serial.printf("[pwrkey] 長按 %.1f 秒放開（不到 3 秒）→ 取消\n", held / 1000.0f);
    overlayEnd();
    return;
  }
  if (held < cfg::PWR_KEY_CLICK_MS) onClick(now);
}
}  // namespace

namespace power_key {

void begin() {
  uint8_t c1 = 0, c2 = 0, w = 0;
  const bool rok = rd(cfg::PM1_REG_BTN_CFG1, &c1) && rd(cfg::PM1_REG_BTN_CFG2, &c2) && rd(cfg::PM1_REG_WDT_CNT, &w);
  Serial.printf("[pwrkey] 開機時 PM1：BTN_CFG1=0x%02X BTN_CFG2=0x%02X WDT=%u%s\n", c1, c2, (unsigned)w, rok ? "" : "（讀取失敗）");
  const bool ok = applyMode(false);
  s_last_feed = millis();
  s_ready = true;
  Serial.printf("[pwrkey] 新機制%s：單按＝開關螢幕、長按 3～5 秒放開＝關機、5 秒＝root、連按五下＝強制關機｜看門狗 %u 秒\n",
                ok ? "" : "（PM1 寫入失敗！）", (unsigned)cfg::PWR_WDT_SEC);
}

void loop() {
  if (!s_ready) return;
  const uint32_t now = millis();
  if (!s_root && now - s_last_feed >= cfg::PWR_WDT_FEED_MS) {   // 餵狗：主迴圈卡死就不會餵 → PM1 重置
    s_last_feed = now;
    wr(cfg::PM1_REG_WDT_KEY, cfg::PM1_WDT_FEED_KEY);
  }
  if (s_root_msg_until && (int32_t)(now - s_root_msg_until) >= 0) {   // 「ROOT 模式」訊息顯示完：回一般畫面
    s_root_msg_until = 0;
    if (power_save::screenOn()) power_save::reapplyBacklight();
    else { M5.Display.setBrightness(0); M5.Display.sleep(); }
    ui_fx::invalidate();
  }
  if (s_root) return;   // root：電源鍵交給 PM1 原廠動作
  if (now - s_last_poll < cfg::PWR_KEY_POLL_MS) return;
  s_last_poll = now;
  uint8_t st = 0;
  if (!rd(cfg::PM1_REG_BTN_STATUS, &st)) return;
  if (st != s_raw) {
    if (!s_armed) Serial.printf("[pwrkey] BTN_STATUS=0x%02X\n", st);
    s_raw = st;
  }
  const bool pressed = (st & cfg::PM1_BTN_PRESSED_BIT) != 0;

  if (!s_armed) {   // 開機時手還按著（單按開機）／極性不對：看到放開夠久才開始
    if (pressed) { s_release_since = 0; return; }
    if (!s_release_since) s_release_since = now | 1;
    if (now - s_release_since >= cfg::PWR_KEY_ARM_MS) {
      s_armed = true;
      Serial.println("[pwrkey] 電源鍵就緒");
    }
    return;
  }

  if (pressed && !s_down) {
    s_down = true;
    s_down_t = now;
  } else if (!pressed && s_down) {
    s_down = false;
    onRelease(now, now - s_down_t);
  }
  if (s_down) {
    const uint32_t held = now - s_down_t;
    if (!s_overlay && held >= cfg::PWR_KEY_OVERLAY_MS) {
      s_overlay = true;
      s_overlay_phase = -1;
      s_overlay_screen_was_on = power_save::screenOn();
      ui_fx::cancel();
      ui_fx::invalidate();   // 第一張整張送（螢幕原本可能是關的）
      screenOn();
      Serial.println("[pwrkey] 長按中…（3～5 秒放開＝關機、5 秒＝root）");
    }
    if (s_overlay) overlayDraw(held);
  }
}

bool overlayActive() { return s_overlay || (s_root_msg_until && (int32_t)(millis() - s_root_msg_until) < 0); }

void drawBadge() {
  if (!s_root || !power_save::screenOn() || overlayActive()) return;
  const int W = M5.Display.width();
  M5.Display.fillRect(W - 30, 0, 30, 11, TFT_RED);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextDatum(top_center);
  M5.Display.setTextColor(TFT_BLACK, TFT_RED);
  M5.Display.drawString("ROOT", W - 15, 2);
}

bool rootMode() { return s_root; }

void setRoot(bool root) {
  const bool ok = applyMode(root);
  s_root = root;
  s_down = false;
  s_clicks = 0;
  if (root) {
    Serial.printf("[pwrkey] → root%s：原廠按鍵（單按＝重置、雙擊＝關機、長按＝下載模式），看門狗關；重置後回新機制\n",
                  ok ? "" : "（PM1 寫入失敗！）");
    message("ROOT 模式", "單按重置後回新機制", TFT_RED);
    s_root_msg_until = (millis() + 2000) | 1;
  } else {
    s_armed = false;
    s_release_since = 0;
    s_root_msg_until = 0;
    s_last_feed = millis();
    Serial.printf("[pwrkey] → 新機制%s\n", ok ? "" : "（PM1 寫入失敗！）");
    if (power_save::screenOn()) power_save::reapplyBacklight();
    ui_fx::invalidate();
  }
}

void shutdown(const char* why, const char* line1, const char* line2) {
  Serial.printf("[pwrkey] 關機：%s\n", why);
  message(line1, line2, TFT_WHITE);
  delay(1200);
  M5.Display.setBrightness(0);
  M5.Display.sleep();
  M5.Power.powerOff();
  // 還沒斷電（例如插著 USB 時 PM1 又開回來）：停在這裡，不再餵狗 → 看門狗到時重置
  for (;;) delay(100);
}

void printStatus() {
  uint8_t st = 0, c1 = 0, c2 = 0, w = 0;
  const bool ok = rd(cfg::PM1_REG_BTN_STATUS, &st) && rd(cfg::PM1_REG_BTN_CFG1, &c1) &&
                  rd(cfg::PM1_REG_BTN_CFG2, &c2) && rd(cfg::PM1_REG_WDT_CNT, &w);
  Serial.printf("[pwrkey] %s｜BTN_STATUS=0x%02X（%s）BTN_CFG1=0x%02X（單按重置%s、下載模式%s）BTN_CFG2=0x%02X（雙擊關機%s）"
                "｜看門狗剩 %u 秒｜螢幕%s%s\n",
                s_root ? "root" : "新機制", st, (st & cfg::PM1_BTN_PRESSED_BIT) ? "按著" : "放開", c1,
                (c1 & 0x01) ? "關" : "開", (c1 & 0x80) ? "鎖" : "開", c2, (c2 & 0x01) ? "關" : "開", (unsigned)w,
                power_save::screenOn() ? "開" : "關", ok ? "" : "｜讀取失敗");
}

void hangTest() {
  Serial.printf("[pwrkey] 主迴圈卡死測試：不再餵狗，約 %u 秒後應被 PM1 重置\n", (unsigned)cfg::PWR_WDT_SEC);
  for (;;) delay(100);   // delay 會讓出 CPU（ESP32 自己的看門狗不會叫），只有 PM1 看門狗救得回來
}

}  // namespace power_key
