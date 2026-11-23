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
int      s_viz_x = 0, s_viz_y = 0;   // 已送出、畫面還沒取走的位移（雷達準星用）

struct MotionSample {
  uint32_t at;
  float x, y;
};
constexpr uint8_t kMotionQueue = 32;
MotionSample s_motion[kMotionQueue];
uint8_t s_motion_head = 0, s_motion_count = 0;
constexpr float kMotionCap = 127.0f * cfg::AIR_ACC_CAP_REPORTS;

float s_gain = cfg::AIR_GAIN;
float s_dead = cfg::AIR_DEADZONE_DPS;
float s_exp  = cfg::AIR_ACCEL_EXP;      // 加速曲線指數（@n 調）
int   s_sign_x = cfg::AIR_SIGN_X;
int   s_sign_y = cfg::AIR_SIGN_Y;

struct Vec {
  float x, y, z;
};
Vec s_up{0, 0, 1}, s_right{1, 0, 0};
bool s_gravity_ready = false;
bool s_clutch = false;
bool s_recenter = false;
bool s_world_axes = false;
constexpr uint8_t kButtonQueue = 8;
uint8_t s_buttons = 0, s_sent_buttons = 0;
uint32_t s_left_down_ms = 0;
uint8_t s_button_queue[kButtonQueue];
uint8_t s_button_head = 0, s_button_count = 0;
int s_wheel = 0;
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

float dot(Vec a, Vec b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec cross(Vec a, Vec b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec normalized(Vec v) {
  const float n = sqrtf(dot(v, v));
  return {v.x / n, v.y / n, v.z / n};
}

void updateFrame(Vec a, float magnitude, Vec rotation, float dt) {
  const bool validGravity = fabsf(magnitude - 1.0f) < 0.15f;
  if (!s_gravity_ready && !validGravity) return;
  if (validGravity && (!s_gravity_ready || s_recenter)) {
    s_up = normalized(a);
    s_gravity_ready = true;
    s_recenter = false;
  } else if (dt > 0 && dt < 0.1f) {
    const float rad = dt * 0.01745329252f;
    const Vec delta = cross(s_up, rotation);
    s_up = normalized({s_up.x + delta.x * rad, s_up.y + delta.y * rad, s_up.z + delta.z * rad});
    if (validGravity) {
      const Vec g = normalized(a);
      const float mix = dt * 2.0f;
      s_up = normalized({s_up.x + (g.x - s_up.x) * mix, s_up.y + (g.y - s_up.y) * mix,
                         s_up.z + (g.z - s_up.z) * mix});
    }
  }
  // 裝置頂端是前方 (+Y)。前方與重力平行時，改用機身右側在水平面的投影。
  Vec right = cross({0, 1, 0}, s_up);
  if (dot(right, right) < 0.04f) {
    const float projection = s_up.x;
    right = {1.0f - s_up.x * projection, -s_up.y * projection, -s_up.z * projection};
    if (dot(right, s_right) < 0) right = {-right.x, -right.y, -right.z};
  }
  s_right = normalized(right);
}

void clearMotion() {
  s_acc_x = s_acc_y = 0;
  s_motion_head = s_motion_count = 0;
}

void queueMotion(uint32_t now, float x, float y) {
  if (!x && !y) return;
  if (s_motion_count == kMotionQueue) {
    // 異常高取樣率也維持回看時間。滿時捨棄最早的一筆待送位移。
    s_motion_head = (s_motion_head + 1) % kMotionQueue;
    --s_motion_count;
  }
  s_motion[(s_motion_head + s_motion_count) % kMotionQueue] = {now, x, y};
  ++s_motion_count;
}

void releaseMotion(uint32_t now) {
  while (s_motion_count) {
    const MotionSample& sample = s_motion[s_motion_head];
    if (now - sample.at < cfg::AIR_CLICK_LOOKBACK_MS) return;
    s_acc_x = constrain(s_acc_x + sample.x, -kMotionCap, kMotionCap);
    s_acc_y = constrain(s_acc_y + sample.y, -kMotionCap, kMotionCap);
    s_motion_head = (s_motion_head + 1) % kMotionQueue;
    --s_motion_count;
  }
}

void queueButtons(uint8_t buttons) {
  if (s_button_count == kButtonQueue) {
    s_button_queue[(s_button_head + kButtonQueue - 1) % kButtonQueue] = buttons;
    return;
  }
  s_button_queue[(s_button_head + s_button_count) % kButtonQueue] = buttons;
  ++s_button_count;
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
  clearMotion();
  s_stopped = true;
  s_still_run = false;
  s_prev_us = 0;
  s_buttons = s_sent_buttons = 0;
  s_left_down_ms = 0;
  s_button_head = s_button_count = 0;
  s_wheel = 0;
  s_clutch = false;
  s_world_axes = false;
  s_recenter = false;
  s_gravity_ready = false;
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
    if (still_ms >= cfg::AIR_STILL_MS && !s_stopped) { s_stopped = true; clearMotion(); }
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

  const Vec rotation{r[0], r[1], r[2]};
  if (!active()) return;
  if (s_world_axes) updateFrame({ax, ay, az}, am, rotation, dt);
  if (s_clutch || (s_world_axes && !s_gravity_ready) || s_stopped || dt <= 0.0f || dt > 0.1f) return;
  const uint32_t now = millis();
  if (s_freeze_until && (int32_t)(now - s_freeze_until) < 0) return;
  // 左鍵已送給主機，但短按期間不接受位移，避免點選被解讀成拖曳。
  if ((s_buttons & ble_mouse::BTN_LEFT) && now - s_left_down_ms < cfg::AIR_DRAG_HOLD_MS) return;
  // 每次連線先沿用 M4-1 的手感。主動放開離合器重校後，才切換到重力座標。
  const float yaw = s_world_axes ? dot(rotation, s_up) : rotation.z;
  const float pitch = s_world_axes ? dot(rotation, s_right) : rotation.x;
  queueMotion(now, s_sign_x * curve(yaw) * dt, s_sign_y * curve(pitch) * dt);
}

void freezeMovement() {
  s_freeze_until = millis() + cfg::AIR_CLICK_FREEZE_MS;
  clearMotion();
}

void setClutch(bool held, bool recenter) {
  s_clutch = held;
  if (recenter) {
    s_recenter = true;
    s_world_axes = true;
  }
  freezeMovement();
}

void setButton(uint8_t mask, bool down) {
  const uint8_t next = down ? s_buttons | mask : s_buttons & ~mask;
  if (next == s_buttons) return;
  if (down && (mask & ble_mouse::BTN_LEFT)) s_left_down_ms = millis();
  s_buttons = next;
  queueButtons(next);
  freezeMovement();
}

void click(uint8_t mask) {
  queueButtons(s_buttons | mask);
  queueButtons(s_buttons);
  freezeMovement();
}

void takeMotion(int& dx, int& dy) {
  dx = s_viz_x; dy = s_viz_y;
  s_viz_x = s_viz_y = 0;
}

void scroll(int delta) {
  s_wheel = constrain(s_wheel + delta, -254, 254);
}

void loop() {
  const uint32_t now = millis();
  if (mode_manager::current() != Mode::AirMouse) {
    if (s_buttons || s_button_count || s_wheel || s_clutch) reset();
    return;
  }

  if (active() && now - s_last_send >= cfg::AIR_SEND_MS) {
    releaseMotion(now);
    int dx = (int)s_acc_x, dy = (int)s_acc_y;   // 往 0 取整，小數留著下次
    if (dx > 127) dx = 127; else if (dx < -127) dx = -127;
    if (dy > 127) dy = 127; else if (dy < -127) dy = -127;
    const int wheel = constrain(s_wheel, -127, 127);
    const uint8_t buttons = s_button_count ? s_button_queue[s_button_head] : s_sent_buttons;
    if (dx || dy || wheel || s_button_count) {
      if (ble_mouse::report(dx, dy, wheel, buttons)) {
        s_acc_x -= dx;
        s_acc_y -= dy;
        s_wheel -= wheel;
        s_sent_buttons = buttons;
        if (s_button_count) {
          s_button_head = (s_button_head + 1) % kButtonQueue;
          --s_button_count;
        }
        s_sent_x += dx; s_sent_y += dy; ++s_sent_n;
        s_viz_x = constrain(s_viz_x + dx, -2000, 2000);
        s_viz_y = constrain(s_viz_y + dy, -2000, 2000);
      } else {
        clearMotion();   // 送不出去（斷線／急停）就丟掉，不要累積成一次大跳
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
  Serial.printf("[air] 靈敏度 %.2f｜曲線指數 %.2f｜死區 %.1f 度/秒｜座標 %s｜左右方向 %+d｜上下方向 %+d｜偏差 %+.2f/%+.2f/%+.2f%s｜%s\n",
                s_gain, s_exp, s_dead, s_world_axes ? "重力" : "原始機身", s_sign_x, s_sign_y,
                s_bias[0], s_bias[1], s_bias[2], s_bias_ok ? "" : "（未校正）", s_stopped ? "靜止" : "移動");
}

}  // namespace air_mouse
