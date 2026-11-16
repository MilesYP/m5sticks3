// ─────────────────────────────────────────────────────────────────────────
//  ui_expression：模式 1 離線表情（M1-A）
//
//  切片 1：Idle 待機臉 — 圓角青色眼睛，上下呼吸＋隨機眨眼。
//  切片 2：六種情緒（固定配色、眼型），切換時顏色與形狀平滑過渡。
//  之後切片再疊 IMU 互動、直橫雙版面。畫面規格見 docs/StickS3 介面規格.html。
//  以 sprite（先畫在記憶體再整張推上螢幕）避免閃爍；非阻塞，每圈畫一幀。
// ─────────────────────────────────────────────────────────────────────────
#pragma once
#include <stdint.h>

enum class Mood : uint8_t { Idle, Happy, Angry, Scared, Sleepy, Asleep };

namespace ui_expression {

void begin();    // 建立畫布（需在 ui_status::begin 設好螢幕方向之後呼叫）
void render();   // 畫一張動畫幀；非阻塞

void setMood(Mood m);
Mood mood();
const char* moodName();
void debugNextMood();   // 測試用：循環切到下一種情緒（切片 3 接上 IMU 後移除）

}  // namespace ui_expression
