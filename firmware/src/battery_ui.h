// ─────────────────────────────────────────────────────────────────────────
//  battery_ui：休息臉顏色跟電量（M1-A / A3，Miles 11-20 選方案一）
//
//  每 5 秒讀一次電量（M5PM1，經內部 I2C；只在主迴圈呼叫），指數平滑後分級：
//    ≥60% 藍、40–59 綠、20–39 黃、10–19 紅、<10 紅灰閃爍（充電中不閃）
//  升級要比門檻多 3%（遲滯），避免在邊界來回跳色。
//  結果交給 ui_expression：setRestTint（待機／想睡／睡著的眼睛顏色）、setLowBatteryBlink。
//  序列埠 c<百分比> 假裝電量（驗收各色用），c 單獨一個字恢復真實讀數。
//  A4 量測：每秒讀 CHG_STAT、每 10 秒存一筆充電紀錄（PSRAM 環形 6 小時），序列埠 h 印出。
// ─────────────────────────────────────────────────────────────────────────
#pragma once
#include <stdint.h>

namespace battery_ui {
void begin();                 // 讀第一次並套用
void loop();                  // 每圈呼叫；每 BAT_READ_MS 才真的讀
void setOverride(int pct);    // 假裝電量（0～100）；<0＝恢復真實讀數
int  level();                 // 平滑後的電量 %（讀不到＝−1）
const char* tierName();
// A4 量測（v23-a4m）：充電紀錄。序列埠 h 印出（每分鐘一行）、h0 清空。
const char* chargeStateName(int state);   // M5.Power.getChargeState() 的整數值 → 中文
void printChargeNow();                     // 一行：VBUS、電池電壓、CHG_STAT、電源來源
void printLog();
void clearLog();
}
