// ─────────────────────────────────────────────────────────────────────────
//  全韌體共用的具名常數：腳位、I2C 位址、按鍵門檻、UI 時序。
//  腳位來源見 CLAUDE.md「已驗證腳位」與按鍵表，不要在其他檔案寫魔術數字。
// ─────────────────────────────────────────────────────────────────────────
#pragma once
#include <stdint.h>

namespace cfg {

// ── 按鍵（2026-11-12 實機確認；按下 = LOW，M5Unified 已設為 input）──
constexpr uint8_t PIN_BTN_A = 11;   // 正面藍色大鍵：模式內主動作／模式 2 急停
constexpr uint8_t PIN_BTN_B = 12;   // 側鍵：系統鍵（切模式／重印／急停備援）

// ── Grove I2C 配件位址（鐵則一：兩者二選一，可能都沒插）──
constexpr uint8_t I2C_ADDR_CARDKB = 0x5F;
constexpr uint8_t I2C_ADDR_SCROLL = 0x40;
constexpr uint32_t GROVE_POWER_SETTLE_MS = 200;   // 開 EXT 5V 後等配件 MCU 開機

// ── 側鍵 B 的時間門檻（按鍵決議 2026-11-13）──
constexpr uint32_t BTN_B_REPORT_MS = 1000;   // 按住 1～3 秒後放開 = 重印狀態報告
constexpr uint32_t BTN_B_KILL_MS   = 3000;   // 按住滿 3 秒（不等放開）= 全域急停備援

// ── 急停 task ──
constexpr uint32_t KILL_TASK_STACK = 3072;
constexpr int      KILL_TASK_CORE  = 0;      // 與 Arduino loop（core 1）分開，避免被主迴圈拖住

// ── 網路 task（B3b-5）：WebSocket 收送、重連都在這裡，不卡主迴圈 ──
// core 0：主迴圈在 core 1，兩邊互不卡；Wi-Fi／TCP 底層本來就在 core 0。
// 優先權 1：比急停 task（最高）與 M5Unified 的 Mic／Speaker task（2）低，錄放音與急停永遠先跑。
// 堆疊 10KB：TLS 交握在這裡跑（原本在 8KB 的 loopTask 跑得動）；[perf] 會印剩餘量。
constexpr uint32_t NET_TASK_STACK = 10240;
constexpr int      NET_TASK_CORE  = 0;
constexpr uint32_t NET_TASK_PRIO  = 1;

// ── UI ──
constexpr uint32_t UI_REFRESH_MS = 500;      // 狀態沒變時的定期重畫間隔
constexpr uint32_t LOOP_YIELD_MS = 1;        // loop 結尾讓出 CPU 的時間（非阻塞）
// B3b-4：中控斷線超過這麼久才顯示斷線提示（重連常在 1～2 秒內完成，避免圖示一閃一閃）
constexpr uint32_t OFFLINE_SHOW_MS = 2000;

}  // namespace cfg
