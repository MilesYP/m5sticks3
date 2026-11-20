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

// 低通後的重力方向（看向傾斜方向用）
float s_gx = 0, s_gy = 0, s_gz = 1;
bool  s_g_init = false;

// 桌寵狀態
uint32_t s_last_active = 0;    // 最近一次有人動它／講話結束
Mood     s_react = Mood::Idle; // 正在做的反應（Idle＝沒有）
uint32_t s_react_until = 0;
bool     s_was_busy = false;
bool     s_listening = false;  // 已為「睡著聽聲音」打開待機收音

const Hist& histAgo(int k) {   // k=0 最新一窗
  return s_hist[(s_hist_pos - 1 - k + HIST * 2) % HIST];
}

void printBattery() {
  Serial.printf("[bat] %d%% %dmV %s｜平滑 %d%% 休息臉%s\n", (int)M5.Power.getBatteryLevel(),
                (int)M5.Power.getBatteryVoltage(), M5.Power.isCharging() ? "充電中" : "未充電",
                battery_ui::level(), battery_ui::tierName());
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
      Serial.printf("[imu] n=%u 間隔≤%.1fms 迴圈≤%ums｜偏離1g≤%.3fg 衝擊≤%.3fg 轉速≤%.1f°/s｜平均(%+.2f,%+.2f,%+.2f)g｜%s\n",
                    (unsigned)s_w.n, s_w.max_gap_us / 1000.0f, (unsigned)s_w.max_loop_ms,
                    s_w.max_da, s_w.max_jerk, s_w.max_gyro, s_w.sx * k, s_w.sy * k, s_w.sz * k,
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
    // 左右看 x 軸；直立時（y 是主要重力方向）前後傾看 z 軸
    const float gx = cfg::PET_GAZE_SIGN_X * s_gx / cfg::PET_GAZE_FULL_G;
    const float gy = (s_gy > 0.5f) ? cfg::PET_GAZE_SIGN_Y * s_gz / cfg::PET_GAZE_FULL_G : 0.0f;
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
  if (!s_tap_pending && s_hist_n >= 4 && s_w.max_jerk >= cfg::PET_TAP_JERK_G) {
    float before = 0;
    for (int i = 1; i <= 3; ++i) if (histAgo(i).jerk > before) before = histAgo(i).jerk;
    if (before < cfg::PET_TAP_QUIET_G) {
      s_tap_pending = true;
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

bool measureImu() { return s_m_imu; }
bool measureSound() { return s_m_snd; }

}  // namespace imu_pet
