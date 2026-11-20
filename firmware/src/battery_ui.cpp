#include "battery_ui.h"

#include <M5Unified.h>

#include "config.h"
#include "ui_expression.h"

namespace {

struct Tier { const char* name; int low; uint8_t r, g, b; };
// 由高到低；low＝這一級的下限（%）
const Tier kTiers[] = {
  {"藍",   60,  70, 150, 255},
  {"綠",   40,  87, 220, 140},
  {"黃",   20, 255, 209, 102},
  {"紅",   10, 255,  90,  90},
  {"閃爍",  0, 255,  90,  90},   // <10%：顏色由 ui_expression 的紅灰閃爍蓋過
};
constexpr int kTierCount = sizeof(kTiers) / sizeof(kTiers[0]);
constexpr int TIER_BLINK = kTierCount - 1;

float    s_level = -1.0f;      // 平滑後的電量
int      s_tier = -1;
int      s_override = -1;
uint32_t s_last_read = 0;
uint32_t s_last_charging = 0;  // 最近一次讀到「充電中」的時刻
bool     s_seen_charging = false;

int rawTier(float pct) {
  for (int i = 0; i < kTierCount; ++i) if (pct >= kTiers[i].low) return i;
  return TIER_BLINK;
}

// 遲滯：往低一級＝跌破這一級下限；往高一級＝超過上一級下限＋HYST
int nextTier(int cur, float pct) {
  if (cur < 0) return rawTier(pct);
  int t = cur;
  while (t < TIER_BLINK && pct < kTiers[t].low) ++t;
  while (t > 0 && pct >= kTiers[t - 1].low + cfg::BAT_HYST_PCT) --t;
  return t;
}

void apply(bool charging) {
  if (s_tier < 0) {
    ui_expression::setRestTint(false, 0, 0, 0);
    ui_expression::setLowBatteryBlink(false);
    return;
  }
  const Tier& t = kTiers[s_tier];
  ui_expression::setRestTint(true, t.r, t.g, t.b);
  ui_expression::setLowBatteryBlink(s_tier == TIER_BLINK && !charging);
}

void readAndApply(bool first) {
  const uint32_t now = millis();
  bool charging = false;
  float pct;
  if (s_override >= 0) {
    pct = (float)s_override;
    s_level = pct;                     // 假裝電量：不平滑、不遲滯，立即換色
    const int before = s_tier;
    s_tier = rawTier(pct);
    if (s_tier != before) Serial.printf("[bat] 假裝 %d%% → %s\n", s_override, kTiers[s_tier].name);
  } else {
    const int raw = (int)M5.Power.getBatteryLevel();
    if (M5.Power.isCharging()) { s_last_charging = now; s_seen_charging = true; }
    charging = s_seen_charging && (now - s_last_charging < cfg::BAT_CHARGE_HOLD_MS);
    if (raw < 0 || raw > 100) {        // 讀不到：維持原狀
      apply(charging);
      return;
    }
    s_level = (first || s_level < 0) ? (float)raw : s_level + ((float)raw - s_level) * cfg::BAT_SMOOTH;
    const int before = s_tier;
    s_tier = nextTier(s_tier, s_level);
    if (s_tier != before)
      Serial.printf("[bat] 電量 %.0f%%（讀數 %d%%）%s → 休息臉%s\n", s_level, raw,
                    charging ? "充電中" : "", kTiers[s_tier].name);
  }
  apply(charging);
}

}  // namespace

namespace battery_ui {

void begin() {
  s_last_read = millis();
  readAndApply(true);
}

void loop() {
  if (millis() - s_last_read < cfg::BAT_READ_MS) return;
  s_last_read = millis();
  readAndApply(false);
}

void setOverride(int pct) {
  s_override = (pct > 100) ? 100 : pct;
  if (s_override < 0) {
    Serial.println("[bat] 恢復真實電量");
    s_tier = -1;                        // 回到真實讀數時重新分級
    readAndApply(true);
  } else {
    readAndApply(false);
  }
}

int level() { return s_level < 0 ? -1 : (int)(s_level + 0.5f); }
const char* tierName() { return s_tier < 0 ? "-" : kTiers[s_tier].name; }

}  // namespace battery_ui
