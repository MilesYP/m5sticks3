#include "input_hal.h"

#include <M5Unified.h>

#include "config.h"

namespace {

GroveDevice s_device = GroveDevice::None;
char s_scan_line[64] = "(none)";

// B 鍵長按狀態：按下時刻、是否已在按住期間觸發過 3 秒急停
uint32_t s_b_press_ms = 0;
bool s_b_hold3_fired = false;

void scanGrove() {
  bool found[128] = {};
  M5.Ex_I2C.scanID(found);

  size_t pos = 0;
  s_scan_line[0] = '\0';
  for (int addr = 0x08; addr < 0x78; ++addr) {
    if (found[addr] && pos + 4 < sizeof(s_scan_line)) {
      pos += snprintf(s_scan_line + pos, sizeof(s_scan_line) - pos, "%02X ", addr);
    }
  }
  if (pos == 0) snprintf(s_scan_line, sizeof(s_scan_line), "(none)");

  const bool has_kb = found[cfg::I2C_ADDR_CARDKB];
  const bool has_scroll = found[cfg::I2C_ADDR_SCROLL];
  if (has_kb && has_scroll) {
    // 單一 Grove 孔不該同時出現兩者；以 CardKB 為準並留下警告
    Serial.println("[input] WARN: both CardKB and Scroll detected, using CardKB");
  }
  s_device = has_kb ? GroveDevice::CardKB
           : has_scroll ? GroveDevice::Scroll
           : GroveDevice::None;
}

}  // namespace

namespace input_hal {

void begin() {
  // Grove 孔的 5V 由 M5PM1 控制，M5Unified 預設關閉
  M5.Power.setExtOutput(true);
  delay(cfg::GROVE_POWER_SETTLE_MS);   // 只在開機時等一次，不在 loop 內

  // M5Unified 只有在 cfg.external_imu/rtc 時才會初始化 Ex_I2C，這裡明確開啟
  if (!M5.Ex_I2C.isEnabled()) M5.Ex_I2C.begin();

  scanGrove();
  Serial.printf("[input] Grove I2C: %s -> %s\n", s_scan_line, deviceName());
}

GroveDevice device() { return s_device; }

const char* deviceName() {
  switch (s_device) {
    case GroveDevice::CardKB: return "CardKB";
    case GroveDevice::Scroll: return "Scroll";
    default:                  return "None";
  }
}

const char* scanLine() { return s_scan_line; }

InputEvent poll() {
  M5.update();
  const uint32_t now = millis();

  // ── B：系統鍵。先處理按下，再處理按住/放開（同一圈可能同時發生）──
  if (M5.BtnB.wasPressed()) {
    s_b_press_ms = now;
    s_b_hold3_fired = false;
  }
  if (M5.BtnB.isPressed() && !s_b_hold3_fired &&
      now - s_b_press_ms >= cfg::BTN_B_KILL_MS) {
    s_b_hold3_fired = true;   // 放開時不再產生其他事件
    return InputEvent::BtnB_Hold3s;
  }
  if (M5.BtnB.wasReleased()) {
    if (s_b_hold3_fired) return InputEvent::None;
    return (now - s_b_press_ms >= cfg::BTN_B_REPORT_MS) ? InputEvent::BtnB_Hold1s
                                                         : InputEvent::BtnB_Click;
  }

  // ── A：模式內主動作 ──
  if (M5.BtnA.wasPressed()) return InputEvent::BtnA_Press;

  return InputEvent::None;
}

}  // namespace input_hal
