// ─────────────────────────────────────────────────────────────────────────
//  input_hal：輸入硬體抽象層（HAL = 把硬體細節包起來，對上層只給統一介面）
//
//  - 開機掃描 Grove I2C，辨識 CardKB(0x5F) / Scroll(0x40) / 皆無（鐵則一）
//  - 把 A/B 實體按鍵轉成統一的 InputEvent（按鍵決議 2026-11-13，方案 1）
//  M0 只做偵測；CardKB 字元與 Scroll 滾動的讀取留到使用它們的模式再加。
// ─────────────────────────────────────────────────────────────────────────
#pragma once
#include <stdint.h>

enum class GroveDevice : uint8_t { None, CardKB, Scroll };

enum class InputEvent : uint8_t {
  None,
  BtnA_Press,    // A 按下瞬間（模式 2 的急停另走硬體中斷，見 killswitch）
  BtnB_Click,    // B 短按放開（< 1 秒）：切下一模式／從 SAFE 恢復
  BtnB_Hold1s,   // B 按住 1～3 秒後放開：重印狀態報告
  BtnB_Hold3s,   // B 按住滿 3 秒（立即觸發）：全域急停備援
};

namespace input_hal {

void begin();                 // 開 Grove 供電 → 初始化 Ex_I2C → 掃描
GroveDevice device();
const char* deviceName();     // "CardKB" / "Scroll" / "None"
const char* scanLine();       // Grove 掃描到的所有位址（例 "5F" 或 "(none)"），給報告用
InputEvent poll();            // 每圈 loop 呼叫一次；內含 M5.update()

}  // namespace input_hal
