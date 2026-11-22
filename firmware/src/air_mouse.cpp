// air_mouse：空中滑鼠的游標計算（M4-1）——說明見 air_mouse.h
#include "air_mouse.h"

#include <Arduino.h>
#include <math.h>

#include "ble_mouse.h"
#include "config.h"
#include "mode_manager.h"

namespace air_mouse {
namespace {

// 零點偏差（度/秒），三軸
float s_bias[3] = {0, 0, 0};
bool  s_bias_ok = false;          // 靜止滿一次後才算校正過（之前用 0）
float s_bias_sum[3] = {0, 0, 0};  // 第一次校正：靜止期間取平均
uint32_t s_bias_n = 0;

// 靜止判定
uint32_t s_still_since_us = 0;
bool     s_still_run = false;     // 正在連續靜止中
bool     s_stopped = true;        // 靜止滿 AIR_STILL_MS → 不送移動

uint32_t s_prev_us = 0;
float    s_acc_x = 0, s_acc_y = 0;   // 還沒送出的游標點數（含小數）

float s_gain = cfg::AIR_GAIN;
float s_dead = cfg::AIR_DEADZONE_DPS;
float s_exp  = cfg::AIR_ACCEL_EXP;      // 加速曲線指數（@n 調）
int   s_sign_x = cfg::AIR_SIGN_X;
int   s_sign_y = cfg::AIR_SIGN_Y;

uint8_t  s_btn = 0;
bool     s_btn_dirty = false;
uint32_t s_freeze_until = 0;      // 點擊凍結（millis）
uint32_t s_last_send = 0;

// 量測（@m）
bool     s_meas = false;
uint32_t s_meas_t0 = 0;
uint32_t s_meas_n = 0;
float    s_pk[3] = {0, 0, 0};     // 這一窗的轉速峰值（扣偏差，帶正負號）
int      s_sent_x = 0, s_sent_y = 0;
uint32_t s_sent_n = 0;

bool active() {
  return mode_manager::current() == Mode::AirMouse && ble_mouse::secured() && !ble_mouse::circleTest();
}

// 死區＋加速曲線：轉速（度/秒，已扣偏差）→ 每秒幾點
float curve(float v) {
  const float a = fabsf(v);
  if (a < s_dead) return 0.0f;
  const float e = a - s_dead;          // 扣掉死區，起步不會跳
  const float pps = s_gain * powf(e, s_exp);
  return v > 0 ? pps : -pps;
}

}  // namespace

void reset() {
  s_acc_x = s_acc_y = 0;
  s_stopped = true;
  s_still_run = false;
  s_prev_us = 0;
  s_btn = 0;
  s_btn_dirty = false;
  s_freeze_until = 0;
}

void onImu(float ax, float ay, float az, float gx, float gy, float gz, uint32_t us) {
  const float g[3] = {gx, gy, gz};
  const float dt = s_prev_us ? (float)(us - s_prev_us) * 1e-6f : 0.0f;
  s_prev_us = us;

  // ── 靜止判定（用扣偏差後的轉速＋加速度是否接近 1g）──
  float r[3], rm2 = 0;
  for (int i = 0; i < 3; ++i) {
    r[i] = g[i] - s_bias[i];
    rm2 += r[i] * r[i];
  }
  const float am = sqrtf(ax * ax + ay * ay + az * az);
  // 還沒校正過時偏差是 0，用寬一點的門檻才進得了第一次校正
  const float still_dps = s_bias_ok ? cfg::AIR_STILL_DPS : cfg::AIR_STILL_DPS * 3.0f;
  const bool quiet = rm2 < still_dps * still_dps && fabsf(am - 1.0f) < cfg::AIR_STILL_G;
  if (quiet) {
    if (!s_still_run) { s_still_run = true; s_still_since_us = us; s_bias_n = 0; s_bias_sum[0] = s_bias_sum[1] = s_bias_sum[2] = 0; }
    const uint32_t still_ms = (us - s_still_since_us) / 1000;
    if (still_ms >= cfg::AIR_STILL_MS && !s_stopped) { s_stopped = true; s_acc_x = s_acc_y = 0; }
    // 零點：第一次用平均（靜止 0.5 秒），之後每筆小權重跟
    if (!s_bias_ok) {
      for (int i = 0; i < 3; ++i) s_bias_sum[i] += g[i];
      if (++s_bias_n >= 50 && still_ms >= cfg::AIR_BIAS_FIRST_MS) {
        for (int i = 0; i < 3; ++i) s_bias[i] = s_bias_sum[i] / (float)s_bias_n;
        s_bias_ok = true;
        Serial.printf("[air] 零點校正完成：%.2f／%.2f／%.2f 度/秒\n", s_bias[0], s_bias[1], s_bias[2]);
      }
    } else if (still_ms >= cfg::AIR_STILL_MS) {
      for (int i = 0; i < 3; ++i) s_bias[i] += (g[i] - s_bias[i]) * cfg::AIR_BIAS_LPF;
    }
  } else {
    s_still_run = false;
    s_stopped = false;
  }

  if (s_meas) {
    ++s_meas_n;
    for (int i = 0; i < 3; ++i) if (fabsf(r[i]) > fabsf(s_pk[i])) s_pk[i] = r[i];
  }

  if (!active() || s_stopped || dt <= 0.0f || dt > 0.1f) return;
  if (s_freeze_until && (int32_t)(millis() - s_freeze_until) < 0) { s_acc_x = s_acc_y = 0; return; }

  // 拿法（31-m4q 預設，@m 實測後定）：像遙控器，螢幕朝上、頂端指向電腦
  //   左右轉（繞 z 軸）→ X；上下點（繞 x 軸）→ Y
  s_acc_x += s_sign_x * curve(r[cfg::AIR_AXIS_X]) * dt;
  s_acc_y += s_sign_y * curve(r[cfg::AIR_AXIS_Y]) * dt;
}

void onButton(bool down) {
  const uint8_t b = down ? ble_mouse::BTN_LEFT : 0;
  if (b == s_btn) return;
  s_btn = b;
  s_btn_dirty = true;
  s_freeze_until = millis() + cfg::AIR_CLICK_FREEZE_MS;
  s_acc_x = s_acc_y = 0;
}

void loop() {
  const uint32_t now = millis();
  if (mode_manager::current() != Mode::AirMouse) {
    if (s_btn) reset();
    return;
  }

  if (active() && now - s_last_send >= cfg::AIR_SEND_MS) {
    int dx = (int)s_acc_x, dy = (int)s_acc_y;   // 往 0 取整，小數留著下次
    if (dx > 127) dx = 127; else if (dx < -127) dx = -127;
    if (dy > 127) dy = 127; else if (dy < -127) dy = -127;
    if (dx || dy || s_btn_dirty) {
      if (ble_mouse::report(dx, dy, 0, s_btn)) {
        s_acc_x -= dx;
        s_acc_y -= dy;
        s_btn_dirty = false;
        s_sent_x += dx; s_sent_y += dy; ++s_sent_n;
      } else {
        s_acc_x = s_acc_y = 0;   // 送不出去（斷線／急停）就丟掉，不要累積成一次大跳
      }
      s_last_send = now;
    }
  }

  if (s_meas && now - s_meas_t0 >= 100) {
    Serial.printf("[air] n=%lu｜轉速 x%+6.1f y%+6.1f z%+6.1f｜偏差 %+.2f/%+.2f/%+.2f%s｜%s｜送 %lu 筆 dx%+d dy%+d\n",
                  (unsigned long)s_meas_n, s_pk[0], s_pk[1], s_pk[2], s_bias[0], s_bias[1], s_bias[2],
                  s_bias_ok ? "" : "（未校正）", s_stopped ? "靜止" : "移動", (unsigned long)s_sent_n, s_sent_x, s_sent_y);
    s_meas_t0 = now;
    s_meas_n = 0;
    s_pk[0] = s_pk[1] = s_pk[2] = 0;
    s_sent_x = s_sent_y = 0;
    s_sent_n = 0;
  }
}

void setMeasure(bool on) {
  s_meas = on;
  s_meas_t0 = millis();
  Serial.printf("[air] 量測 %s（每 100ms：轉速峰值、零點偏差、靜止／移動、送出量）\n", on ? "開" : "關");
}
bool measure() { return s_meas; }

void setGain(float g) {
  if (g < 0.1f) g = 0.1f;
  if (g > 50.0f) g = 50.0f;
  s_gain = g;
  Serial.printf("[air] 靈敏度 %.2f\n", s_gain);
}
void setDeadzone(float d) {
  if (d < 0.0f) d = 0.0f;
  if (d > 30.0f) d = 30.0f;
  s_dead = d;
  Serial.printf("[air] 死區 %.1f 度/秒\n", s_dead);
}
void setExponent(float e) {
  if (e < 0.5f) e = 0.5f;
  if (e > 3.0f) e = 3.0f;
  s_exp = e;
  Serial.printf("[air] 曲線指數 %.2f（1＝線性）\n", s_exp);
}
void flipX() { s_sign_x = -s_sign_x; Serial.printf("[air] X 方向 %+d\n", s_sign_x); }
void flipY() { s_sign_y = -s_sign_y; Serial.printf("[air] Y 方向 %+d\n", s_sign_y); }

void printStatus() {
  Serial.printf("[air] 靈敏度 %.2f｜曲線指數 %.2f｜死區 %.1f 度/秒｜X 軸 %d 方向 %+d｜Y 軸 %d 方向 %+d｜偏差 %+.2f/%+.2f/%+.2f%s｜%s\n",
                s_gain, s_exp, s_dead, cfg::AIR_AXIS_X, s_sign_x, cfg::AIR_AXIS_Y, s_sign_y,
                s_bias[0], s_bias[1], s_bias[2], s_bias_ok ? "" : "（未校正）", s_stopped ? "靜止" : "移動");
}

}  // namespace air_mouse
