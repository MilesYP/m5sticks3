#include "battery_ui.h"

#include <M5Unified.h>

#include "config.h"
#include "ui_expression.h"

namespace {

struct Tier { const char* name; int low; uint8_t r, g, b; };
// 由高到低；low＝這一級的下限（%）
const Tier kTiers[] = {
  {"螢光綠", 60,  57, 255,  20},   // 11-20 Miles：霓虹綠 #39FF14（原本藍→#00FF41 偏青不夠綠），比下一級的薄荷綠更亮更飽和
  {"綠",   40,  87, 220, 140},
  {"黃",   20, 255, 209, 102},
  {"琥珀紅閃", 5, 255, 209, 102},   // 11-21 Miles：<20% 臉原本琥珀色↔紅閃爍（顏色由 ui_expression 蓋過）
  {"紅灰閃", 0, 255,  90,  90},   // <5% 臉紅灰閃爍
};
constexpr int kTierCount = sizeof(kTiers) / sizeof(kTiers[0]);
constexpr int TIER_BLINK = kTierCount - 1;      // 紅灰閃
constexpr int TIER_WARN  = kTierCount - 2;      // 原色紅閃

float    s_level = -1.0f;      // 平滑後的電量
int      s_vbat_mv = 0;        // A5-1：上次讀到的電池電壓
bool     s_charging = false;   // A5-1：上次判斷的充電中
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
    ui_expression::setLowBatteryBlink(0);
    return;
  }
  const Tier& t = kTiers[s_tier];
  ui_expression::setRestTint(true, t.r, t.g, t.b);
  ui_expression::setLowBatteryBlink(charging ? 0 : s_tier == TIER_BLINK ? 2 : s_tier == TIER_WARN ? 1 : 0);
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
    const int mv = (int)M5.Power.getBatteryVoltage();   // A5-1：資訊頁顯示電壓（同一輪讀，不另外讀）
    if (mv > 0) s_vbat_mv = mv;
    if (M5.Power.isCharging()) { s_last_charging = now; s_seen_charging = true; }
    charging = s_seen_charging && (now - s_last_charging < cfg::BAT_CHARGE_HOLD_MS);
    if (raw < 0 || raw > 100) {        // 讀不到：維持原狀
      s_charging = charging;
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
  s_charging = charging;
  apply(charging);
}

// ── A4 量測：充電紀錄 ──
struct ChgLog {
  uint32_t sec;       // 開機後秒數
  uint16_t vbus_mv;   // USB 輸入電壓
  uint16_t vbat_mv;   // 電池電壓
  int8_t   level;     // 電量讀數 %（未平滑）
  uint8_t  chg_s;     // 這 10 秒裡 CHG_STAT＝充電中的次數（每秒一次）
  uint8_t  flips;     // 這 10 秒裡 CHG_STAT 切換幾次
  uint8_t  src;       // M5PM1 電源來源位元：bit0 5VIN、bit1 5VINOUT、bit2 VBAT
};
ChgLog*  s_log = nullptr;
int      s_log_head = 0;        // 下一筆寫哪裡
int      s_log_n = 0;
uint32_t s_last_poll = 0;
uint8_t  s_acc_chg = 0, s_acc_flips = 0, s_acc_polls = 0;
int      s_last_state = -99;

uint8_t readSource() {
  m5::M5PM1_Class::pwr_src_t src = m5::M5PM1_Class::none;
  if (!M5.Power.M5pm1.getPowerSource(&src)) return 0xFF;
  return (uint8_t)src;
}

void pollCharge() {
  const uint32_t now = millis();
  if (now - s_last_poll < cfg::BAT_CHG_POLL_MS) return;
  s_last_poll = now;
  const int st = (int)M5.Power.getChargeState();
  if (s_last_state != -99 && st != s_last_state) ++s_acc_flips;
  s_last_state = st;
  if (st == 1) ++s_acc_chg;
  if (++s_acc_polls < cfg::BAT_LOG_EVERY_POLLS) return;
  if (s_log) {
    const int16_t vbus = M5.Power.getVBUSVoltage();
    const int16_t vbat = M5.Power.getBatteryVoltage();
    const int32_t lv = M5.Power.getBatteryLevel();
    ChgLog& e = s_log[s_log_head];
    e.sec = now / 1000;
    e.vbus_mv = vbus > 0 ? (uint16_t)vbus : 0;
    e.vbat_mv = vbat > 0 ? (uint16_t)vbat : 0;
    e.level = (lv >= 0 && lv <= 100) ? (int8_t)lv : -1;
    e.chg_s = s_acc_chg;
    e.flips = s_acc_flips;
    e.src = readSource();
    s_log_head = (s_log_head + 1) % cfg::BAT_LOG_SAMPLES;
    if (s_log_n < cfg::BAT_LOG_SAMPLES) ++s_log_n;
  }
  s_acc_chg = s_acc_flips = s_acc_polls = 0;
}

}  // namespace

namespace battery_ui {

void begin() {
  s_last_read = millis();
  readAndApply(true);
  s_log = (ChgLog*)ps_malloc(sizeof(ChgLog) * cfg::BAT_LOG_SAMPLES);
  Serial.printf("[bat] 充電紀錄 %s（每 10 秒一筆、最多 %d 筆，序列埠 h 印出）\n", s_log ? "就緒" : "記憶體不足",
                cfg::BAT_LOG_SAMPLES);
}

void loop() {
  pollCharge();
  if (millis() - s_last_read < cfg::BAT_READ_MS) return;
  s_last_read = millis();
  readAndApply(false);
}

const char* chargeStateName(int s) {
  switch (s) {
    case -3: return "未初始化";
    case -2: return "讀取失敗";
    case -1: return "不確定";
    case 0:  return "不支援";
    case 1:  return "充電中";
    case 2:  return "未充電";
    case 3:  return "充飽";
    case 4:  return "停用";
    case 5:  return "放電";
    case 6:  return "閒置";
    default: return "?";
  }
}

void printChargeNow() {
  const uint8_t src = readSource();
  Serial.printf("[chg] VBUS %dmV｜電池 %dmV %d%%｜CHG_STAT %s｜電源來源 0x%02X（5VIN%s 5VINOUT%s VBAT%s）\n",
                (int)M5.Power.getVBUSVoltage(), (int)M5.Power.getBatteryVoltage(),
                (int)M5.Power.getBatteryLevel(), chargeStateName((int)M5.Power.getChargeState()), src,
                (src != 0xFF && (src & 1)) ? "✓" : "✗", (src != 0xFF && (src & 2)) ? "✓" : "✗",
                (src != 0xFF && (src & 4)) ? "✓" : "✗");
}

void printLog() {
  printChargeNow();
  if (!s_log || s_log_n == 0) {
    Serial.println("[chg] 還沒有紀錄（每 10 秒存一筆）");
    return;
  }
  Serial.printf("[chg] 紀錄 %d 筆（10 秒一筆），每分鐘一行：時間（開機後 時:分）｜VBUS 最低～最高 mV｜電池平均 mV｜電量 %%｜充電中秒數/總秒數｜CHG_STAT 切換次數｜電源來源\n",
                s_log_n);
  const int start = (s_log_head - s_log_n + cfg::BAT_LOG_SAMPLES) % cfg::BAT_LOG_SAMPLES;
  int i = 0;
  while (i < s_log_n) {
    const ChgLog& f = s_log[(start + i) % cfg::BAT_LOG_SAMPLES];
    const uint32_t minute = f.sec / 60;
    uint16_t vmin = 0xFFFF, vmax = 0;
    uint32_t vbat_sum = 0, chg = 0, flips = 0;
    uint8_t src_or = 0;
    int cnt = 0;
    int8_t lv = -1;
    while (i < s_log_n) {
      const ChgLog& e = s_log[(start + i) % cfg::BAT_LOG_SAMPLES];
      if (e.sec / 60 != minute) break;
      if (e.vbus_mv < vmin) vmin = e.vbus_mv;
      if (e.vbus_mv > vmax) vmax = e.vbus_mv;
      vbat_sum += e.vbat_mv;
      chg += e.chg_s;
      flips += e.flips;
      if (e.src != 0xFF) src_or |= e.src;
      lv = e.level;
      ++cnt;
      ++i;
    }
    Serial.printf("[chg] %02u:%02u｜VBUS %u～%u｜電池 %u｜%d%%｜充電 %u/%u｜切換 %u｜來源 0x%02X\n",
                  (unsigned)(minute / 60), (unsigned)(minute % 60), (unsigned)vmin, (unsigned)vmax,
                  (unsigned)(vbat_sum / cnt), (int)lv, (unsigned)chg,
                  (unsigned)(cnt * cfg::BAT_LOG_EVERY_POLLS), (unsigned)flips, src_or);
  }
  Serial.println("[chg] 結束");
}

void clearLog() {
  s_log_head = 0;
  s_log_n = 0;
  Serial.println("[chg] 充電紀錄已清空");
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
int voltageMv() { return s_vbat_mv; }
bool charging() { return s_charging; }

bool externalPower() {
  const uint8_t source = readSource();
  // PWR_SRC 的 5VIN 是 USB 輸入；5VINOUT 不是 USB 輸入。
  if (source != 0xFF) return (source & m5::M5PM1_Class::vin) != 0;
  return M5.Power.getVBUSVoltage() >= 4500;
}

}  // namespace battery_ui
