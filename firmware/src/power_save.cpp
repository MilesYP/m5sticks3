#include "power_save.h"

#include <M5Unified.h>

#include "battery_ui.h"
#include "config.h"
#include "imu_pet.h"
#include "mode_manager.h"
#include "net_hub.h"
#include "ui_expression.h"
#include "ui_fx.h"
#include "ui_info.h"
#include "ui_modeswitch.h"
#include "voice_turn.h"

namespace {
bool     s_enabled = true;
bool     s_ready = false;
power_save::Level s_level = power_save::Level::Active;
uint8_t  s_bl_awake = cfg::PWR_BL_AWAKE;
uint8_t  s_bl_now = 0;           // 目前實際背光
uint32_t s_bl_step_t = 0;
uint32_t s_cpu_mhz = 0;
bool     s_wifi_deep = false;
uint32_t s_last_frame = 0;
bool     s_force_frame = true;
bool     s_screen_on = true;     // P2：螢幕開關（電源鍵單按）
uint32_t s_sleep_t = 0;          // P2：Desktop 開始「睡」（睡著或關螢幕）的時刻；0＝醒著
uint32_t s_asleep_t = 0;         // P2：表情睡著（沒插線）的時刻，滿 30 秒自動關螢幕

uint32_t frameMs(power_save::Level l) {
  switch (l) {
    case power_save::Level::Calm:   return cfg::PWR_FRAME_CALM_MS;
    case power_save::Level::Drowsy: return cfg::PWR_FRAME_DROWSY_MS;
    case power_save::Level::Asleep: return cfg::PWR_FRAME_ASLEEP_MS;
    default:                        return 0;
  }
}

uint8_t blTarget(power_save::Level l) {
  if (l == power_save::Level::Drowsy) return s_bl_awake < cfg::PWR_BL_DROWSY ? s_bl_awake : cfg::PWR_BL_DROWSY;
  if (l == power_save::Level::Asleep) return s_bl_awake < cfg::PWR_BL_ASLEEP ? s_bl_awake : cfg::PWR_BL_ASLEEP;
  return s_bl_awake;
}

power_save::Level decide() {
  using L = power_save::Level;
  if (!s_enabled || mode_manager::current() != Mode::Desktop) return L::Active;
  if (!s_screen_on) return voice_turn::busy() ? L::Active : L::Asleep;   // P2：Desktop 關螢幕＝睡（Friday 還在講就全速）
  // 資訊頁跟表情頁同一套（11-20 Miles）：依桌寵心情分級；資訊頁本來就每秒才重畫一次，所以只影響背光／CPU／Wi-Fi
  if (voice_turn::busy() || ui_modeswitch::active() || ui_fx::active()) return L::Active;
  if (ui_info::shown()) {   // 資訊頁：看靜置時間（10 秒變暗、30 秒微光），被拍／搖的反應中全速
    const Mood m = ui_expression::mood();
    if (m != Mood::Idle && m != Mood::Sleepy && m != Mood::Asleep) return L::Active;
    const uint32_t idle = imu_pet::idleMs();
    if (idle >= cfg::PWR_INFO_ASLEEP_MS) return L::Asleep;
    if (idle >= cfg::PWR_INFO_DROWSY_MS) return L::Drowsy;
    return L::Calm;
  }
  switch (ui_expression::mood()) {
    case Mood::Idle:   return L::Calm;
    case Mood::Sleepy: return L::Drowsy;
    case Mood::Asleep: return L::Asleep;
    default:           return L::Active;   // 聆聽／思考／說話／開心／生氣／驚嚇：全速
  }
}

void applyCpu(uint32_t mhz) {
  if (!cfg::PWR_CPU_SCALE || mhz == s_cpu_mhz) return;
  setCpuFrequencyMhz(mhz);
  s_cpu_mhz = getCpuFrequencyMhz();
}

void applyBacklight(uint32_t now) {
  const uint8_t tgt = blTarget(s_level);
  if (s_bl_now == tgt) return;
  if (tgt > s_bl_now) {
    s_bl_now = tgt;                     // 變亮：立刻（被拍、拿起要馬上看得到）
  } else {
    if (now - s_bl_step_t < cfg::PWR_BL_DIM_STEP_MS) return;   // 變暗：慢慢降，不要突然一黑
    s_bl_step_t = now;
    --s_bl_now;
  }
  M5.Display.setBrightness(s_bl_now);
}
}  // namespace

namespace power_save {

void begin() {
  const uint8_t def = M5.Display.getBrightness();
  s_bl_now = s_bl_awake;
  M5.Display.setBrightness(s_bl_now);
  s_cpu_mhz = getCpuFrequencyMhz();
  s_ready = true;
  Serial.printf("[pwr] 省電 %s｜背光 原本 %u → 正常 %u／想睡 %u／睡著 %u｜CPU %lu MHz（睡著 %lu）\n",
                s_enabled ? "開" : "關", (unsigned)def, (unsigned)s_bl_awake, (unsigned)cfg::PWR_BL_DROWSY,
                (unsigned)cfg::PWR_BL_ASLEEP, (unsigned long)s_cpu_mhz,
                (unsigned long)(cfg::PWR_CPU_SCALE ? cfg::PWR_CPU_ASLEEP_MHZ : s_cpu_mhz));
}

void loop() {
  if (!s_ready) return;
  const uint32_t now = millis();
  const Level want = decide();
  if (want != s_level) {
    if (static_cast<uint8_t>(want) < static_cast<uint8_t>(s_level)) s_force_frame = true;   // 醒過來：這一圈就畫
    s_level = want;
    applyCpu(want == Level::Asleep ? cfg::PWR_CPU_ASLEEP_MHZ : cfg::PWR_CPU_AWAKE_MHZ);
    const bool deep = (want == Level::Drowsy || want == Level::Asleep);
    if (deep != s_wifi_deep) { s_wifi_deep = deep; net_hub::setIdleDeepSleep(deep); }
    Serial.printf("[pwr] → %s｜背光 %u｜CPU %lu MHz｜Wi-Fi %s｜畫面 %s\n", levelName(want), (unsigned)blTarget(want),
                  (unsigned long)getCpuFrequencyMhz(), deep ? "深度省電" : "一般",
                  frameMs(want) ? (want == Level::Calm ? "30fps" : want == Level::Drowsy ? "10fps" : "5fps") : "全速");
  }
  if (s_screen_on) applyBacklight(now);

  // P2（11-22 Miles）：沒插線時進入睡眠滿 30 秒 → 自動關螢幕（之後只有長按 A、單按電源鍵會亮）。
  // 「睡眠」＝省電等級 Asleep：表情頁是睡著臉、資訊頁是靜置 30 秒（微光）——兩頁一樣適用
  const bool asleep_face = mode_manager::current() == Mode::Desktop && s_screen_on && !voice_turn::busy() &&
                           s_level == Level::Asleep && battery_ui::chargeState() == 0;
  if (!asleep_face) s_asleep_t = 0;
  else if (!s_asleep_t) s_asleep_t = now | 1;
  else if (s_enabled && now - s_asleep_t >= cfg::PWR_AUTO_SCREEN_OFF_MS) {
    s_asleep_t = 0;
    setScreenOn(false, "睡著滿 30 秒");
  }

  // P2：Desktop 睡著或關螢幕滿 3 分鐘 → 斷 Wi-Fi；醒來就重連（語音中、上傳沒送完一律算醒著）
  const bool sleeping = mode_manager::current() == Mode::Desktop && !voice_turn::busy() &&
                        (!s_screen_on || s_level == Level::Asleep);
  if (sleeping) {
    if (!s_sleep_t) s_sleep_t = now | 1;
    if (!net_hub::radioOff() && now - s_sleep_t >= cfg::PWR_RADIO_OFF_MS)
      net_hub::setRadioOff(true, s_screen_on ? "睡著滿 3 分鐘" : "關螢幕滿 3 分鐘");
  } else {
    s_sleep_t = 0;
    if (net_hub::radioOff()) net_hub::setRadioOff(false, "醒來");
  }
}

bool frameDue() {
  if (s_force_frame) return true;
  const uint32_t ms = frameMs(s_level);
  return ms == 0 || millis() - s_last_frame >= ms;
}

void frameDrawn() {
  s_last_frame = millis();
  s_force_frame = false;
}

uint32_t yieldMs() {
  if (!s_screen_on && mode_manager::current() == Mode::Desktop && !voice_turn::busy()) return cfg::PWR_YIELD_MAX_MS;
  if (!s_enabled || mode_manager::current() != Mode::Desktop) return cfg::LOOP_YIELD_MS;
  if (voice_turn::busy() || ui_modeswitch::active() || ui_fx::active()) return cfg::LOOP_YIELD_MS;
  if (ui_info::shown()) return cfg::PWR_YIELD_MAX_MS;   // 資訊頁每秒才重畫一次
  const uint32_t ms = frameMs(s_level);
  if (ms == 0) return cfg::LOOP_YIELD_MS;
  const uint32_t since = millis() - s_last_frame;
  uint32_t left = since >= ms ? 1 : ms - since;
  if (left > cfg::PWR_YIELD_MAX_MS) left = cfg::PWR_YIELD_MAX_MS;
  return left;
}

Level level() { return s_level; }

const char* levelName(Level l) {
  switch (l) {
    case Level::Active: return "全速";
    case Level::Calm:   return "待機";
    case Level::Drowsy: return "想睡";
    case Level::Asleep: return "睡著";
  }
  return "?";
}

void setEnabled(bool on) {
  s_enabled = on;
  Serial.printf("[pwr] 省電 → %s%s\n", on ? "開" : "關", on ? "" : "（全速、背光正常、CPU 240MHz、Wi-Fi 一般，對照用）");
}

bool enabled() { return s_enabled; }

void setAwakeBrightness(uint8_t v) {
  s_bl_awake = v;
  Serial.printf("[pwr] 正常背光 → %u（想睡 %u、睡著 %u，不會比正常亮）\n", (unsigned)v,
                (unsigned)blTarget(Level::Drowsy), (unsigned)blTarget(Level::Asleep));
}

void setScreenOn(bool on, const char* why) {
  if (on == s_screen_on) return;
  s_screen_on = on;
  if (on) {
    M5.Display.wakeup();
    s_bl_now = 0;                 // 下一圈 applyBacklight 立刻亮到目標
    s_force_frame = true;
    ui_fx::invalidate();          // 螢幕睡著時內容不可信 → 下一張整張送
    imu_pet::wake(why);           // 醒來：靜置從頭計時、回待機臉
  } else {
    ui_fx::cancel();
    M5.Display.setBrightness(0);
    M5.Display.sleep();
  }
  Serial.printf("[pwr] 螢幕 → %s（%s）\n", on ? "開" : "關", why);
}

void toggleScreen(const char* why) { setScreenOn(!s_screen_on, why); }
bool screenOn() { return s_screen_on; }
void reapplyBacklight() { s_bl_now = 0; }

void printStatus() {
  Serial.printf("[pwr] 省電 %s｜等級 %s｜背光 %u（正常 %u）｜CPU %lu MHz｜Wi-Fi %s｜電池 %d mV\n",
                s_enabled ? "開" : "關", levelName(s_level), (unsigned)s_bl_now, (unsigned)s_bl_awake,
                (unsigned long)getCpuFrequencyMhz(), s_wifi_deep ? "深度省電" : "一般",
                (int)M5.Power.getBatteryVoltage());
}

}  // namespace power_save
