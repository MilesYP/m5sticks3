// ─────────────────────────────────────────────────────────────────────────
//  imu_pet：M1-A / A3 桌寵互動（IMU 搖晃／拍打／靜置＋睡著時聽聲音）
//
//  每 100ms 一窗，取窗內最大的「偏離 1g／衝擊／轉速」判斷（門檻見 config.h，來自 v22-a3m 實測）：
//    沒互動 6 秒 → 想睡、11 秒 → 睡著；拿起來（有轉動）→ 醒
//    搖晃 → 驚嚇；拍打（清醒）→ 隨機開心／生氣；拍打（想睡／睡著）→ 生氣
//    睡著時聽到聲音 → 驚嚇或生氣（只有睡著時才開待機收音）
//    傾斜 → 眼睛看向傾斜方向
//  語音優先：voice_turn 忙時不改表情、不累計靜置；這輪結束後從頭計時（按 A 講話一律叫醒）。
//  只在 Desktop 模式反應。非阻塞，IMU 在主迴圈讀（內部 I2C 只有主迴圈在用）。
//
//  量測（v22-a3m 留著）：序列埠 j 開關 IMU 量測行、v 開關待機聲音量測行。
//  A4：拿法判斷（直式／橫式 A 左／橫式 A 右，規則與實測見 config.h ORIENT_*）→ ui_expression::setOrientation；
//      看方向依目前拿法換算成畫面的左右上下。序列埠 o 假裝拿法。
// ─────────────────────────────────────────────────────────────────────────
#pragma once
#include <stdint.h>

namespace imu_pet {
void begin();                  // M5.begin() 之後呼叫：確認 IMU 在、印型號
void loop();                   // 每圈呼叫：讀 IMU、每 100ms 判斷一次並驅動表情
void setMeasureImu(bool on);   // 序列埠 j：每 100ms 印 IMU 量測行
void setMeasureSound(bool on); // 序列埠 v：開待機收音，每 100ms 印聲音量測行
void setOrientOverride(int o); // 序列埠 o：0 直式、1 橫式 A 左、2 橫式 A 右；<0＝恢復自動判斷
bool measureImu();
// A5-1 資訊頁：低通後的重力（g），已換算成目前畫面的「右」「上」與螢幕法線 z（≈ 每 100ms 更新）；還沒讀到 IMU 回 false
bool tilt(float* right, float* up, float* z);
bool measureSound();
// P1 省電（資訊頁用）：距離上次有人動多久（ms）；反應中、語音中回 0
uint32_t idleMs();
// P2：開螢幕／長按 A 叫醒：靜置從頭計時、睡著的臉回待機
void wake(const char* why);
}
