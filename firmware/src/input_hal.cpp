#include "input_hal.h"

#include <M5Unified.h>

#include "config.h"

namespace {

GroveDevice s_device = GroveDevice::None;
char s_scan_line[64] = "(none)";

// B 鍵長按狀態：按下時刻、是否已在按住期間觸發過 3 秒急停
uint32_t s_b_press_ms = 0;
bool s_b_hold3_fired = false;

// Grove 供電（2026-11-19 實測：只開 5V 時 CardKB 有電但 I2C 掃不到）
//  - 5V 升壓：M5PM1 PWR_CFG 的 BOOST_EN（M5Unified 的 setExtOutput 只做這一步）
//  - L3B：M5PM1 的 GPIO2 = L3B_EN（官方 StickS3 腳位表），M5Unified 0.3.0 不會打開它。
//    做法與 HA 論壇 StickS3 Grove I2C 的解法相同。是否必要尚未證實（真正的問題是 Ex_I2C 沒 begin），
//    官方腳位表標示為電源致能腳，打開無害，先保留。
void powerGrove() {
  auto& pm1 = M5.Power.M5pm1;
  const bool ok5v = M5.Power.setExtOutput(true);
  bool okL3b = pm1.setGPIOFunction(m5::M5PM1_Class::gpio2, m5::M5PM1_Class::gpio);
  okL3b &= pm1.setGPIOMode(m5::M5PM1_Class::gpio2, m5::M5PM1_Class::output);
  okL3b &= pm1.setGPIODrive(m5::M5PM1_Class::gpio2, m5::M5PM1_Class::push_pull);
  okL3b &= pm1.setGPIOOutput(m5::M5PM1_Class::gpio2, true);
  Serial.printf("[input] power: 5V set=%s  L3B set=%s\n", ok5v ? "ok" : "FAIL", okL3b ? "ok" : "FAIL");
}

void printPowerState() {
  auto& pm1 = M5.Power.M5pm1;
  bool l3b = false;
  const bool l3bRead = pm1.getGPIOOutputLatch(m5::M5PM1_Class::gpio2, &l3b);
  Serial.printf("[input] power state: 5V boost=%s  L3B=%s  5Vout=%u mV  battery=%d mV  usb=%s\n",
                M5.Power.getExtOutput() ? "ON" : "off",
                l3bRead ? (l3b ? "ON" : "off") : "read-fail",
                (unsigned)pm1.get5VoutVoltage(),
                (int)M5.Power.getBatteryVoltage(),
                M5.Power.isCharging() ? "charging" : "-");
}

void scanGrove(uint32_t freq = 100000) {
  bool found[128] = {};
  M5.Ex_I2C.scanID(found, freq);

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
  // Grove 孔的 5V 與 L3B 都由 M5PM1 控制，M5Unified 預設只會（在呼叫時）開 5V
  powerGrove();
  delay(cfg::GROVE_POWER_SETTLE_MS);   // 只在開機時等一次，不在 loop 內
  printPowerState();

  // M5Unified 開機只對 Ex_I2C 做 setPort（設埠號與腳位 G9/G10），沒有 begin。
  // 注意：isEnabled() 只檢查「有沒有設過埠號」，setPort 之後就回 true，
  // 不能拿來判斷要不要 begin（11-19 踩坑：因此跳過 begin，Grove 永遠掃不到）→ 一律 begin。
  const bool exOk = M5.Ex_I2C.begin();
  Serial.printf("[input] Ex_I2C begin=%s  port=%d SDA=G%d SCL=G%d\n", exOk ? "ok" : "FAIL",
                (int)M5.Ex_I2C.getPort(), (int)M5.Ex_I2C.getSDA(), (int)M5.Ex_I2C.getSCL());

  scanGrove();
  Serial.printf("[input] Grove I2C: %s -> %s\n", s_scan_line, deviceName());
  // P1 省電：沒插配件就關 5V 升壓（之後插上要用序列埠 e 重開供電並重掃；原本就只在開機掃描）
  if (s_device == GroveDevice::None) {
    M5.Power.setExtOutput(false);
    Serial.println("[input] Grove 沒有配件 → 關 5V 升壓（省電；插上後打 e 重掃）");
  }
}

void rescan() {
  // 診斷用：會阻塞約 0.5 秒（等配件開機），只由序列埠指令觸發，不在正常流程中呼叫
  powerGrove();
  delay(500);
  printPowerState();
  const bool exOk = M5.Ex_I2C.begin();   // 見 begin() 的說明：不可用 isEnabled() 判斷
  Serial.printf("[input] Ex_I2C begin=%s  port=%d SDA=G%d SCL=G%d\n", exOk ? "ok" : "FAIL",
                (int)M5.Ex_I2C.getPort(), (int)M5.Ex_I2C.getSDA(), (int)M5.Ex_I2C.getSCL());
  scanGrove(400000);
  Serial.printf("[input] rescan @400kHz: %s\n", s_scan_line);
  scanGrove(100000);   // 以 100kHz 的結果為準（CardKB 的 ATmega 用 100kHz 最保險）
  Serial.printf("[input] rescan @100kHz: %s -> %s\n", s_scan_line, deviceName());
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

  // ── A：模式內主動作（Desktop 長按＝講話，按下/放開各一事件）──
  if (M5.BtnA.wasPressed()) return InputEvent::BtnA_Press;
  if (M5.BtnA.wasReleased()) return InputEvent::BtnA_Release;

  return InputEvent::None;
}

}  // namespace input_hal
