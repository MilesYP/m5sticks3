// ─────────────────────────────────────────────────────────────────────────
//  ui_expression：模式 1 離線表情（M1-A）
//
//  A3：IMU 互動由 imu_pet 呼叫 setMood；休息臉顏色跟電量；傾斜看方向。
//  切片 1：Idle 待機臉 — 圓角青色眼睛，上下呼吸＋隨機眨眼。
//  切片 2：六種情緒（固定配色、眼型），切換時顏色與形狀平滑過渡。
//  之後切片再疊 IMU 互動、直橫雙版面。畫面規格見 docs/StickS3 介面規格.html。
//  以 sprite（先畫在記憶體再整張推上螢幕）避免閃爍；非阻塞，每圈畫一幀。
// ─────────────────────────────────────────────────────────────────────────
#pragma once
#include <stdint.h>

enum class Mood : uint8_t { Idle, Listening, Thinking, Speaking, Happy, Angry, Scared, Sleepy, Asleep };

namespace ui_expression {

void begin();    // 建立畫布（需在 ui_status::begin 設好螢幕方向之後呼叫）
void render();   // 畫一張動畫幀；非阻塞

void setMood(Mood m);
Mood mood();
const char* moodName();
void setSpeakLevel(float level01);   // 說話中的嘴巴開合：0～1（M1-B 接喇叭音量）；傳負值＝用內建模擬
// B3b-4（Miles 選定方案 c）：斷線時表情定格（不呼吸、不眨眼、不過渡）、眼睛變灰、右上角 Wi-Fi 加斜線
void setOffline(bool offline);
bool offline();
// B4：對話區（直式：畫面下方最多 5 行，表情往上縮、眼睛在剩下的區域置中；橫式併入 M1-A A4）
void convBegin();                          // 按下 A：清掉上一輪、展開對話區
void convText(bool assistant, const char* utf8);   // 加一段逐字稿（使用者／小S），自動換行、只留最後 5 行
void convEnd();                            // 這輪結束：停留幾秒後收起、表情回滿版
// A3：休息臉（待機／想睡／睡著）的眼睛顏色改用電量色（Miles 11-20 方案一）；enabled=false 回規格固定色
void setRestTint(bool enabled, uint8_t r, uint8_t g, uint8_t b);
// A3：低電量紅灰閃爍（蓋過所有情緒；聆聽／思考／說話時不閃）
void setLowBatteryBlink(bool on);
// A3：眼睛看向傾斜方向，x／y 各 −1～1（＋x 往右、＋y 往下）；平滑移動
void setGaze(float x, float y);

}  // namespace ui_expression
