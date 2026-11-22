// ─────────────────────────────────────────────────────────────────────────
//  air_mouse：空中滑鼠的游標計算（M4-1，2026-11-23）
//
//  游標跟陀螺儀「轉速」走，不跟角度走（鐵則十二第 2 點）：
//    轉速（度/秒）− 零點偏差 → 死區 → 加速曲線 → 每秒幾點 × 時間 → 累積小數 → 送 HID 報告
//  零點自動校正：靜止（轉速小、加速度≈1g）時慢慢更新偏差，陀螺儀不會越用越歪。
//  放下靜止就停：靜止滿 AIR_STILL_MS 不送移動；拿起來一動就恢復。
//  A 鍵＝左鍵：按下送「按下」、放開送「放開」→ 短按＝點擊、按住移動＝拖曳。
//    按下／放開那一下會震到機身，前後 AIR_CLICK_FREEZE_MS 凍結游標。
//  IMU 只有 imu_pet 在讀（M5.Imu.update），每筆新資料轉交 onImu()，不重複讀。
//  只在 AirMouse 模式、藍牙已加密、沒在跑 @1 畫圓測試時送。急停閘門由 ble_mouse::report 檢查。
//  序列埠：@m 量測開關（每 100ms 印轉速／偏差／送出量）｜@g<數字> 靈敏度｜@d<數字> 死區｜@x／@y 翻轉方向
// ─────────────────────────────────────────────────────────────────────────
#pragma once
#include <stdint.h>

namespace air_mouse {

void reset();                                  // 進出 AirMouse 時清狀態（偏差保留）
void onImu(float ax, float ay, float az,        // imu_pet 每拿到一筆新 IMU 資料呼叫（單位 g、度/秒）
           float gx, float gy, float gz, uint32_t us);
void onButton(bool down);                      // A 按下／放開（main.cpp 在 AirMouse 模式呼叫）
void loop();                                   // 每圈主迴圈：送報告、量測輸出

void setMeasure(bool on);                      // @m
bool measure();
void setGain(float g);                         // @g<數字>
void setDeadzone(float d);                     // @d<數字>（度/秒）
void setExponent(float e);                     // @n<數字>：加速曲線指數（1＝線性）
void flipX();                                  // @x
void flipY();                                  // @y
void printStatus();

}  // namespace air_mouse
