#include "power_key.h"

#include <M5Unified.h>

#include "config.h"
#include "power_save.h"
#include "ui_fx.h"

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
int      s_overlay_fill = -1;
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

constexpr int BAR_X = 12, BAR_Y = 118, BAR_H = 12;

void overlayDraw(uint32_t held) {
  const int W = M5.Display.width();
  const int bw = W - BAR_X * 2;
  if (s_overlay_phase < 0) {   // 第一次：底與刻度
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setFont(&fonts::efontTW_12);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.drawString("電源鍵", W / 2, 60);
    M5.Display.setTextColor(0x8410, TFT_BLACK);
    M5.Display.drawString("3 秒關機", W / 2, 84);
    M5.Display.drawString("5 秒 ROOT", W / 2, 100);
    M5.Display.drawRect(BAR_X - 1, BAR_Y - 1, bw + 2, BAR_H + 2, 0x8410);
    const int t3 = BAR_X + bw * (int)cfg::PWR_KEY_OFF_MS / (int)cfg::PWR_KEY_ROOT_MS;
    M5.Display.drawFastVLine(t3, BAR_Y - 4, BAR_H + 8, TFT_WHITE);
    s_overlay_fill = 0;
  }
  const int phase = held >= cfg::PWR_KEY_ROOT_MS ? 2 : held >= cfg::PWR_KEY_OFF_MS ? 1 : 0;
  const uint16_t col = phase == 2 ? TFT_RED : phase == 1 ? 0xFCE0 /* 琥珀 */ : 0xC618;
  int fill = (int)((uint64_t)bw * (held > cfg::PWR_KEY_ROOT_MS ? cfg::PWR_KEY_ROOT_MS : held) / cfg::PWR_KEY_ROOT_MS);
  if (phase != s_overlay_phase) {
    M5.Display.fillRect(BAR_X, BAR_Y, fill, BAR_H, col);   // 換階段整條換色
    M5.Display.fillRect(0, 148, W, 30, TFT_BLACK);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextColor(col, TFT_BLACK);
    M5.Display.drawString(phase == 2 ? "放開＝進 ROOT" : phase == 1 ? "放開＝關機" : "繼續按住…", W / 2, 162);
    s_overlay_phase = phase;
  } else if (fill > s_overlay_fill) {
    M5.Display.fillRect(BAR_X + s_overlay_fill, BAR_Y, fill - s_overlay_fill, BAR_H, col);
  }
  if (fill > s_overlay_fill) s_overlay_fill = fill;
}

void overlayEnd() {
  s_overlay = false;
  s_overlay_phase = -1;
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
