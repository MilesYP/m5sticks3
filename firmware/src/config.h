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

// ── UI ──
constexpr uint32_t UI_REFRESH_MS = 500;      // 狀態沒變時的定期重畫間隔
constexpr uint32_t LOOP_YIELD_MS = 1;        // loop 結尾讓出 CPU 的時間（非阻塞）

}  // namespace cfg
