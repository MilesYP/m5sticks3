#include "mode_manager.h"

#include <Arduino.h>

#include "audio_io.h"
#include "ble_mouse.h"
#include "killswitch.h"
#include "net_hub.h"

namespace {

// ── 各模式的 enter()/exit() 空殼：M0 只印 log，之後各里程碑在這裡開關資源 ──
void desktopEnter()  { Serial.println("  enter(Desktop)"); }
void desktopExit()   { Serial.println("  exit(Desktop)"); }
void hidAgentEnter() { Serial.println("  enter(HidAgent)"); }
void hidAgentExit()  { Serial.println("  exit(HidAgent)"); }
void rfToolEnter()   { Serial.println("  enter(RfTool)"); }
void rfToolExit()    { Serial.println("  exit(RfTool)"); }
// M4（鐵則二）：AirMouse 開 BLE HID＋BMI270；關 Wi-Fi、ES8311（Friday 在這個模式不能用）
void airMouseEnter() {
  Serial.println("  enter(AirMouse)");
  net_hub::setRadioOff(true, "AirMouse");   // 網路 task 斷 Wi-Fi（非阻塞）；power_save 在 AirMouse 不會自動重連
  audio_io::suspend();                       // 關麥克風／喇叭
  ble_mouse::begin();
}
void airMouseExit() {
  Serial.println("  exit(AirMouse)");
  ble_mouse::end();
  audio_io::resume();                        // 回麥克風待機
  net_hub::setRadioOff(false, "離開 AirMouse");
}
void safeEnter()     { Serial.println("  enter(Safe)"); }
void safeExit()      { Serial.println("  exit(Safe)"); }

struct ModeOps {
  const char* name;
  void (*enter)();
  void (*exit)();
  bool kill_by_btn_a;   // 這個模式中 A 鍵是否為急停（按鍵決議：只有模式 2）
};

// 順序必須與 enum class Mode 一致
const ModeOps kModes[] = {
  {"Desktop",  desktopEnter,  desktopExit,  false},
  {"HidAgent", hidAgentEnter, hidAgentExit, true},
  {"RfTool",   rfToolEnter,   rfToolExit,   false},
  {"AirMouse", airMouseEnter, airMouseExit, false},
  {"Safe",     safeEnter,     safeExit,     false},
};
constexpr uint8_t kCycleCount = 4;   // 參與循環的模式數（不含 Safe）

Mode s_current = Mode::Desktop;
Mode s_previous = Mode::Desktop;

const ModeOps& ops(Mode m) { return kModes[static_cast<uint8_t>(m)]; }

}  // namespace

namespace mode_manager {

void begin(Mode initial) {
  s_current = initial;
  s_previous = initial;
  Serial.printf("[mode] start -> %s\n", name(initial));
  killswitch::setArmedByBtnA(ops(initial).kill_by_btn_a);
  ops(initial).enter();
}

Mode current() { return s_current; }
Mode previous() { return s_previous; }
const char* name(Mode m) { return ops(m).name; }
const char* currentName() { return name(s_current); }

void switchTo(Mode m) {
  if (m == s_current) return;
  Serial.printf("[mode] %s -> %s\n", name(s_current), name(m));
  ops(s_current).exit();
  // 舊模式 exit() 完成後才換急停設定：離開模式 2 的過程中 A 仍可急停
  killswitch::setArmedByBtnA(ops(m).kill_by_btn_a);
  s_current = m;
  ops(m).enter();
}

Mode peekNext() {
  if (s_current == Mode::Safe) return s_current;
  return static_cast<Mode>((static_cast<uint8_t>(s_current) + 1) % kCycleCount);
}

void next() {
  if (s_current == Mode::Safe) return;
  switchTo(static_cast<Mode>((static_cast<uint8_t>(s_current) + 1) % kCycleCount));
}

void emergencyStop() {
  if (s_current == Mode::Safe) return;
  Serial.printf("[mode] EMERGENCY STOP (source=%s, gate closed in %ld us)\n",
                killswitch::sourceName(), (long)killswitch::lastLatencyUs());
  s_previous = s_current;
  switchTo(Mode::Safe);
}

void resumeFromSafe() {
  if (s_current != Mode::Safe) return;
  killswitch::clear();
  Serial.println("[mode] kill cleared, resuming");
  switchTo(s_previous);
}

}  // namespace mode_manager
