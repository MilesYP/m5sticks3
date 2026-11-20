#include "imu_pet.h"

#include <M5Unified.h>
#include <math.h>

#include "audio_io.h"
#include "battery_ui.h"
#include "config.h"
#include "mode_manager.h"
#include "ui_expression.h"
#include "voice_turn.h"

namespace {

constexpr uint32_t BATTERY_PRINT_MS = 5000;   // 量測中每 5 秒印一次電量
constexpr int      HIST = 10;                 // 最近 10 窗（1 秒）
constexpr float    GAZE_LPF = 0.3f;           // 每窗平均加速度的低通權重（約 0.3 秒）

bool s_imu_ok = false;
bool s_m_imu = false;
bool s_m_snd = false;

// 上一筆加速度（算衝擊量用）
bool  s_have_prev = false;
float s_px = 0, s_py = 0, s_pz = 0;
uint32_t s_prev_us = 0;

// 量測窗（每 PET_WINDOW_MS 判斷＋印一次後歸零）
struct Window {
  uint32_t n = 0;              // 這個窗收到幾筆新的 IMU 資料
  uint32_t max_gap_us = 0;     // 相鄰兩筆 IMU 資料最長間隔
  uint32_t max_loop_ms = 0;    // 主迴圈相鄰兩次呼叫最長間隔（含畫表情）
  float max_da = 0;            // |加速度大小 − 1g| 最大值
  float max_jerk = 0;          // 相鄰兩筆加速度向量差的大小（衝擊量）最大值
  float max_gyro = 0;          // 角速度大小最大值（°/s）
  float sx = 0, sy = 0, sz = 0;  // 加速度加總（算平均＝傾斜方向）
  float mot_r = 0, mot_d = 0;    // A4：這一窗絕對值最大的線性加速度（畫面右／下，g，帶正負號）
};
Window s_w;
uint32_t s_win_start = 0;
uint32_t s_last_call = 0;
uint32_t s_last_bat = 0;

// 最近 10 窗的最大值（環形）
struct Hist { float da, jerk; };
Hist s_hist[HIST];
int  s_hist_pos = 0;
int  s_hist_n = 0;
bool s_tap_pending = false;    // 上一窗是拍打候選，等這一窗確認「拍完馬上停」
bool s_tap_was_asleep = false;
uint32_t s_win_idx = 0;        // A4：窗的流水號（拍打間隔用）
uint32_t s_last_tap_win = 0;
// A4 彈珠台：每筆 IMU 的重力低通（扣掉後＝線性加速度）
float s_fgx = 0, s_fgy = 0, s_fgz = 1;
bool  s_fg_init = false;

// 低通後的重力方向（看向傾斜方向用）
float s_gx = 0, s_gy = 0, s_gz = 1;
bool  s_g_init = false;

// 桌寵狀態
uint32_t s_last_active = 0;    // 最近一次有人動它／講話結束
Mood     s_react = Mood::Idle; // 正在做的反應（Idle＝沒有）
uint32_t s_react_until = 0;
bool     s_was_busy = false;
bool     s_listening = false;  // 已為「睡著聽聲音」打開待機收音

// A4 拿法
Orient s_orient = Orient::Portrait;
Orient s_cand = Orient::Portrait;
int    s_cand_n = 0;
int    s_orient_override = -1;

// 這一窗屬於哪個拿法區；平放、斜放、死區、倒拿、用力搖 → false（維持原版面）
bool zoneOf(float ax, float ay, float da, Orient* out) {
  if (sqrtf(ax * ax + ay * ay) < cfg::ORIENT_PLANE_MIN || da >= cfg::ORIENT_SHAKE_DEV_G) return false;
  const float a = atan2f(ax, ay) * 57.29578f;
  if (fabsf(a) <= cfg::ORIENT_PORTRAIT_DEG) { *out = Orient::Portrait; return true; }
  if (a >= cfg::ORIENT_LAND_MIN_DEG && a <= cfg::ORIENT_LAND_MAX_DEG) { *out = Orient::LandARight; return true; }
  if (a <= -cfg::ORIENT_LAND_MIN_DEG && a >= -cfg::ORIENT_LAND_MAX_DEG) { *out = Orient::LandALeft; return true; }
  return false;
}

void applyOrient(Orient o, const char* why) {
  if (o == s_orient) return;
  Serial.printf("[orient] %s → %s%s\n", ui_expression::orientName(s_orient), ui_expression::orientName(o), why);
  s_orient = o;
  ui_expression::setOrientation(o);
}

void updateOrient(float ax, float ay, float da) {
  if (s_orient_override >= 0) return;
  Orient z;
  if (zoneOf(ax, ay, da, &z) && z != s_orient) {
    if (z == s_cand) ++s_cand_n;
    else { s_cand = z; s_cand_n = 1; }
    if (s_cand_n >= cfg::ORIENT_DWELL_WINDOWS) { applyOrient(z, ""); s_cand_n = 0; }
  } else {
    s_cand_n = 0;
  }
}

const Hist& histAgo(int k) {   // k=0 最新一窗
  return s_hist[(s_hist_pos - 1 - k + HIST * 2) % HIST];
}

void printBattery() {
  Serial.printf("[bat] %d%% %dmV %s｜平滑 %d%% 休息臉%s\n", (int)M5.Power.getBatteryLevel(),
                (int)M5.Power.getBatteryVoltage(), M5.Power.isCharging() ? "充電中" : "未充電",
                battery_ui::level(), battery_ui::tierName());
  battery_ui::printChargeNow();   // A4 量測：VBUS、CHG_STAT、電源來源
}

void printWindow(uint32_t now, float snd_lo, float snd_hi, uint32_t snd_n) {
  if (s_m_imu) {
    if (!s_imu_ok) {
      Serial.println("[imu] IMU 未啟用（M5.Imu.isEnabled()=false）");
    } else if (s_w.n == 0) {
      Serial.printf("[imu] 這 %ums 沒有新資料｜迴圈最長 %ums\n", (unsigned)cfg::PET_WINDOW_MS,
                    (unsigned)s_w.max_loop_ms);
    } else {
      const float k = 1.0f / s_w.n;
      // A4 量測：重力在螢幕平面上的分量（平放≈0、直拿／橫拿≈1）與角度（直拿≈0°、橫拿≈±90°）
      const float ax = s_w.sx * k, ay = s_w.sy * k;
      const float plane = sqrtf(ax * ax + ay * ay);
      const float ang = atan2f(ax, ay) * 57.29578f;
      char cand[48] = "";
      if (s_cand_n > 0 && s_orient_override < 0)
        snprintf(cand, sizeof(cand), "（候選 %s %d/%d）", ui_expression::orientName(s_cand), s_cand_n,
                 (int)cfg::ORIENT_DWELL_WINDOWS);
      Serial.printf("[imu] n=%u 間隔≤%.1fms 迴圈≤%ums｜偏離1g≤%.3fg 衝擊≤%.3fg 轉速≤%.1f°/s｜平均(%+.2f,%+.2f,%+.2f)g｜平面 %.2fg 角 %+4.0f°｜甩 右%+.1f 下%+.1f｜%s%s%s｜%s\n",
                    (unsigned)s_w.n, s_w.max_gap_us / 1000.0f, (unsigned)s_w.max_loop_ms,
                    s_w.max_da, s_w.max_jerk, s_w.max_gyro, ax, ay, s_w.sz * k, plane, ang, s_w.mot_r, s_w.mot_d,
                    ui_expression::orientName(s_orient), s_orient_override >= 0 ? "（手動）" : "", cand,
                    ui_expression::moodName());
    }
  }
  if (s_m_snd) {
    if (snd_n == 0) Serial.println("[snd] 這 100ms 沒有聲音資料（錄音／播放中，或麥克風未開）");
    else Serial.printf("[snd] 塊=%u 最低 %.1f 最高 %.1f dBFS\n", (unsigned)snd_n, snd_lo, snd_hi);
  }
  if ((s_m_imu || s_m_snd) && now - s_last_bat >= BATTERY_PRINT_MS) {
    s_last_bat = now;
    printBattery();
  }
}

void setMoodIfChanged(Mood m, const char* why) {
  if (ui_expression::mood() == m) return;
  ui_expression::setMood(m);
  Serial.printf("[pet] %s → %s\n", why, ui_expression::moodName());
}

void react(Mood m, uint32_t now, const char* why) {
  s_react = m;
  s_react_until = now + cfg::PET_REACT_MS;
  s_last_active = now;
  setMoodIfChanged(m, why);
}

// 低通重力換算成目前畫面的右方向／上方向（裝置軸：直拿 右＝+x 上＝+y；A 右 上＝+x 右＝−y；A 左 上＝−x 右＝+y）
void screenAxes(float* right, float* up) {
  switch (s_orient) {
    case Orient::LandARight: *right = -s_gy; *up = s_gx; break;
    case Orient::LandALeft:  *right = s_gy;  *up = -s_gx; break;
    default:                 *right = s_gx;  *up = s_gy; break;
  }
}

// 睡著時才開待機收音（量測 v 開著時由量測決定）
void updateListening(bool want) {
  if (s_m_snd) return;
  if (want == s_listening) return;
  s_listening = want;
  audio_io::setStandbyListen(want);
}

// 每窗結束時：判斷搖晃／拍打／有人在動，驅動表情
void evaluate(uint32_t now, float snd_hi, uint32_t snd_n) {
  // 這一窗的最大值進歷史
  s_hist[s_hist_pos] = {s_w.max_da, s_w.max_jerk};
  s_hist_pos = (s_hist_pos + 1) % HIST;
  if (s_hist_n < HIST) ++s_hist_n;

  // 傾斜方向（低通）→ 眼睛偏移
  if (s_w.n > 0) {
    const float k = 1.0f / s_w.n;
    const float ax = s_w.sx * k, ay = s_w.sy * k, az = s_w.sz * k;
    if (!s_g_init) { s_gx = ax; s_gy = ay; s_gz = az; s_g_init = true; }
    s_gx += (ax - s_gx) * GAZE_LPF;
    s_gy += (ay - s_gy) * GAZE_LPF;
    s_gz += (az - s_gz) * GAZE_LPF;
    // A4：拿法判斷（每個模式都追蹤；只有 Desktop 表情頁會轉）
    updateOrient(ax, ay, s_w.max_da);
    // 看方向：先換算成「目前畫面」的右方向與上方向（裝置軸：直拿 右＝+x 上＝+y；A 右 上＝+x 右＝−y；A 左 上＝−x 右＝+y）
    float right, up;
    screenAxes(&right, &up);
    // 左右看「畫面右方向」的分量；畫面正立時（上方向是主要重力方向）前後傾看 z 軸
    const float gx = cfg::PET_GAZE_SIGN_X * right / cfg::PET_GAZE_FULL_G;
    const float gy = (up > 0.5f) ? cfg::PET_GAZE_SIGN_Y * s_gz / cfg::PET_GAZE_FULL_G : 0.0f;
    ui_expression::setGaze(gx, gy);
  }

  // 特徵
  const bool active = s_w.max_gyro > cfg::PET_ACTIVE_DPS || s_w.max_jerk > cfg::PET_ACTIVE_JERK_G;
  int shake_n = 0;
  for (int i = 0; i < s_hist_n; ++i) if (histAgo(i).da >= cfg::PET_SHAKE_DEV_G) ++shake_n;
  const bool shake = shake_n >= cfg::PET_SHAKE_WINDOWS;
  bool tap = false;
  if (s_tap_pending) {                        // 上一窗是候選：這一窗要「拍完馬上停」
    s_tap_pending = false;
    tap = !shake && s_w.max_jerk < cfg::PET_TAP_AFTER_G;
  }
  ++s_win_idx;
  if (!s_tap_pending && s_hist_n >= 4 && s_w.max_jerk >= cfg::PET_TAP_JERK_G &&
      s_w.max_gyro < cfg::PET_TAP_MAX_DPS && histAgo(1).jerk < s_w.max_jerk &&
      s_win_idx - s_last_tap_win >= cfg::PET_TAP_GAP_WINDOWS) {
    // A4：跳過緊鄰的上一窗（一下拍打可能分在兩窗），再往前 2 窗只要「相對安靜」
    const float before = fmaxf(histAgo(2).jerk, histAgo(3).jerk);
    if (before < fmaxf(cfg::PET_TAP_QUIET_G, s_w.max_jerk * cfg::PET_TAP_QUIET_RATIO)) {
      s_tap_pending = true;
      s_last_tap_win = s_win_idx;
      const Mood m = ui_expression::mood();
      s_tap_was_asleep = (m == Mood::Sleepy || m == Mood::Asleep);
    }
  }
  const bool sound = s_listening && snd_n > 0 && snd_hi >= cfg::PET_SOUND_DBFS;

  // 只在 Desktop 模式反應
  if (mode_manager::current() != Mode::Desktop) {
    s_last_active = now;
    s_react = Mood::Idle;
    updateListening(false);
    return;
  }

  // 語音優先：忙的時候不碰表情、不累計靜置；結束那一刻從頭計時（voice_turn 已把表情設回 Idle）
  const bool busy = voice_turn::busy();
  if (busy || s_was_busy) {
    s_was_busy = busy;
    s_last_active = now;
    s_react = Mood::Idle;
    updateListening(false);
    return;
  }

  const Mood cur = ui_expression::mood();
  const bool sleeping = (cur == Mood::Sleepy || cur == Mood::Asleep);

  if (shake) {
    if (s_react != Mood::Scared) react(Mood::Scared, now, "搖晃");
    else { s_react_until = now + cfg::PET_REACT_MS; s_last_active = now; }   // 一直搖就一直怕
  } else if (tap) {
    if (s_tap_was_asleep) react(Mood::Angry, now, "睡覺被拍醒");
    else react((esp_random() & 1) ? Mood::Happy : Mood::Angry, now, "拍打");
  } else if (sound && cur == Mood::Asleep) {
    react((esp_random() & 1) ? Mood::Scared : Mood::Angry, now, "睡著聽到聲音");
    Serial.printf("[pet] 聲音 %.1f dBFS（門檻 %.0f）\n", snd_hi, cfg::PET_SOUND_DBFS);
  } else if (s_react != Mood::Idle) {
    if (active) s_last_active = now;
    if ((int32_t)(now - s_react_until) >= 0) {
      s_react = Mood::Idle;
      s_last_active = now;
      setMoodIfChanged(Mood::Idle, "反應結束");
    }
  } else if (s_tap_pending) {
    s_last_active = now;           // 拍打候選：等下一窗確認，先別用「被拿起來」叫醒（避免閃一下待機臉）
  } else if (active) {
    s_last_active = now;
    if (sleeping) setMoodIfChanged(Mood::Idle, "被拿起來");
  } else {
    const uint32_t idle = now - s_last_active;
    if (idle >= cfg::PET_ASLEEP_MS) setMoodIfChanged(Mood::Asleep, "靜置 11 秒");
    else if (idle >= cfg::PET_SLEEPY_MS) setMoodIfChanged(Mood::Sleepy, "靜置 6 秒");
    else if (cur != Mood::Idle && !sleeping) setMoodIfChanged(Mood::Idle, "回待機");
  }

  updateListening(ui_expression::mood() == Mood::Asleep);
}

}  // namespace

namespace imu_pet {

void begin() {
  s_imu_ok = M5.Imu.isEnabled();
  Serial.printf("[imu] %s（type=%d）\n", s_imu_ok ? "BMI270 就緒" : "IMU 未啟用", (int)M5.Imu.getType());
  s_win_start = s_last_call = s_last_active = millis();
}

void loop() {
  const uint32_t now = millis();
  const uint32_t gap = now - s_last_call;
  s_last_call = now;
  if (gap > s_w.max_loop_ms) s_w.max_loop_ms = gap;

  if (s_imu_ok && M5.Imu.update()) {
    const auto& d = M5.Imu.getImuData();
    const float ax = d.accel.x, ay = d.accel.y, az = d.accel.z;
    const float gx = d.gyro.x, gy = d.gyro.y, gz = d.gyro.z;
    const uint32_t us = micros();
    ++s_w.n;
    const float da = fabsf(sqrtf(ax * ax + ay * ay + az * az) - 1.0f);
    if (da > s_w.max_da) s_w.max_da = da;
    const float gm = sqrtf(gx * gx + gy * gy + gz * gz);
    if (gm > s_w.max_gyro) s_w.max_gyro = gm;
    if (s_have_prev) {
      const float jx = ax - s_px, jy = ay - s_py, jz = az - s_pz;
      const float j = sqrtf(jx * jx + jy * jy + jz * jz);
      if (j > s_w.max_jerk) s_w.max_jerk = j;
      const uint32_t g = us - s_prev_us;
      if (g > s_w.max_gap_us) s_w.max_gap_us = g;
    }
    s_px = ax; s_py = ay; s_pz = az;
    s_prev_us = us;
    s_have_prev = true;
    s_w.sx += ax; s_w.sy += ay; s_w.sz += az;

    // A4 彈珠台：線性加速度（讀數 − 低通重力）換算成目前畫面的右／下方向，每筆交給畫面
    if (!s_fg_init) { s_fgx = ax; s_fgy = ay; s_fgz = az; s_fg_init = true; }
    s_fgx += (ax - s_fgx) * cfg::PIN_GRAVITY_LPF;
    s_fgy += (ay - s_fgy) * cfg::PIN_GRAVITY_LPF;
    s_fgz += (az - s_fgz) * cfg::PIN_GRAVITY_LPF;
    const float lx = ax - s_fgx, ly = ay - s_fgy;
    float right, up;
    switch (s_orient) {
      case Orient::LandARight: right = -ly; up = lx; break;
      case Orient::LandALeft:  right = ly;  up = -lx; break;
      default:                 right = lx;  up = ly; break;
    }
    if (fabsf(right) > fabsf(s_w.mot_r)) s_w.mot_r = right;
    if (fabsf(up) > fabsf(s_w.mot_d)) s_w.mot_d = -up;
    const bool on = mode_manager::current() == Mode::Desktop && !voice_turn::busy();   // 語音優先：講話時不甩
    ui_expression::setMotion(on ? right : 0.0f, on ? -up : 0.0f);
  }

  if (now - s_win_start >= cfg::PET_WINDOW_MS) {
    float lo = -120.0f, hi = -120.0f;
    uint32_t n = 0;
    if (s_m_snd || s_listening) audio_io::standbyStats(&lo, &hi, &n);
    if (s_imu_ok) evaluate(now, hi, n);
    printWindow(now, lo, hi, n);
    s_w = Window();
    s_win_start = now;
  }
}

void setMeasureImu(bool on) {
  s_m_imu = on;
  Serial.printf("[imu] IMU 量測 %s\n", on ? "開（每 100ms 一行，j 關閉）" : "關");
  if (on) printBattery();
}

void setMeasureSound(bool on) {
  s_m_snd = on;
  audio_io::setStandbyListen(on || s_listening);
  Serial.printf("[snd] 待機聲音量測 %s\n", on ? "開（每 100ms 一行，v 關閉）" : "關");
}

void setOrientOverride(int o) {
  if (o < 0 || o > 2) {
    s_orient_override = -1;
    s_cand_n = 0;
    Serial.println("[orient] 恢復自動判斷");
    return;
  }
  s_orient_override = o;
  applyOrient(static_cast<Orient>(o), "（手動）");
}

bool measureImu() { return s_m_imu; }
bool tilt(float* right, float* up, float* z) {
  if (!s_g_init) return false;
  screenAxes(right, up);
  *z = s_gz;
  return true;
}
bool measureSound() { return s_m_snd; }

uint32_t idleMs() {
  if (s_react != Mood::Idle || voice_turn::busy()) return 0;
  const uint32_t now = millis();
  return now >= s_last_active ? now - s_last_active : 0;
}

}  // namespace imu_pet
