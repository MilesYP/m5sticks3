// ─────────────────────────────────────────────────────────────────────────
//  ui_expression：模式 1 表情（M1-A）
//
//  A4（11-20，Miles 選定）：
//   - 合成波風格：深藍紫底、眼睛上下雙色漸層＋光暈、下方霓虹透視網格（速度跟情緒）
//   - 每種表情都有嘴巴；聆聽改「側耳傾聽」（只有耳朵，聲波和 o 嘴跟你的音量）
//   - 彈簧動畫（衝過頭再彈回）、跟節奏點頭、眼神亂瞄、不對稱（wink／單邊挑眉／歪嘴）、冒小符號
//   - 直式／橫式（A 鍵在左）／橫式（A 鍵在右）：畫布自己轉（M5Canvas::setRotation），螢幕維持 rotation 0、
//     不重新配置記憶體；對話區永遠在 A 鍵側（直式＝下方 5 行；橫式＝直欄，寬＝直式對話列高）
//  A3：休息臉顏色跟電量、低電量閃爍、看向傾斜方向。B4：對話區。B3b-4：斷線定格。
//  以 sprite（先畫在記憶體再整張推上螢幕）避免閃爍；非阻塞，每圈畫一幀。
// ─────────────────────────────────────────────────────────────────────────
#pragma once
#include <stdint.h>

// M5GFX 裡 M5Canvas 是 m5gfx::M5Canvas 的別名（using），只能前置宣告真正的類別
namespace m5gfx { class M5Canvas; }

enum class Mood : uint8_t { Idle, Listening, Thinking, Speaking, Happy, Angry, Scared, Sleepy, Asleep };
// A4：拿法（對話區永遠靠 A 鍵側）
enum class Orient : uint8_t { Portrait, LandALeft, LandARight };

namespace ui_expression {

void begin();    // 建立畫布（需在 ui_status::begin 設好螢幕方向之後呼叫）
void render();   // 畫一張動畫幀；非阻塞

void setMood(Mood m);
Mood mood();                 // 邏輯上的表情（imu_pet／voice_turn 用）；不受 y 預覽影響
const char* moodName();
const char* moodNameOf(Mood m);
void setSpeakLevel(float level01);   // 說話中的嘴巴開合：0～1（喇叭播放音量）；負值＝用內建模擬
void setListenLevel(float level01);  // A4：聆聽中你的音量 0～1（每 100ms 人聲指標）；負值＝用內建模擬
// B3b-4：斷線時表情定格（不呼吸、不眨眼、不過渡）、眼睛變灰、右上角 Wi-Fi 加斜線
void setOffline(bool offline);
bool offline();
// B4：對話區（直式＝下方最多 5 行；橫式＝A 鍵側直欄，約 9 行）
void convBegin();                          // 按下 A：清掉上一輪、展開對話區
void convText(bool assistant, const char* utf8);   // 加一段逐字稿（使用者／Friday），自動換行、往下滾
void convEnd();                            // 這輪結束：停留幾秒後收起、表情回滿版
// A3：休息臉（待機／想睡／睡著）的眼睛顏色改用電量色；enabled=false 回情緒固定色
void setRestTint(bool enabled, uint8_t r, uint8_t g, uint8_t b);
// A3：低電量紅灰閃爍（蓋過所有情緒；聆聽／思考／說話時不閃）
void setLowBatteryBlink(int mode);   // 0 不閃、1 原色紅閃（<20%）、2 紅灰閃（<5%）
// A3：眼睛看向傾斜方向，x／y 各 −1～1（＋x 往畫面右、＋y 往畫面下；已依拿法換算）
void setGaze(float x, float y);
// A4 驚嚇彈珠台：每筆 IMU 的線性加速度（g，已換算成畫面 ＋右／＋下）；夠大就讓眼睛和嘴巴在螢幕裡撞來撞去
void setMotion(float right_g, float down_g);
// A8：視重力（g，畫面 ＋右／＋下；靜置直拿約 (0, 1)，搖晃時含慣性）。碎片（與 A4b 充電液體）往這個方向掉
void setGravity(float right_g, float down_g);
bool shattered();            // A8：五官目前是碎片狀態（甩碎中或拼回中）
// A4b：充電狀態 0 沒充電、1 充電中、2 充飽（battery_ui 判斷後呼叫）。插著線背景一直有液體（量跟電量、跟傾斜流）；休息臉（待機／想睡／睡著）再加：
// 「充電／充飽」標籤＋閃電／星芒徽章、充飽時笑眼。語音、情緒反應的標籤與五官優先
void setCharge(int state);
// A4：版面方向（imu_pet 判斷後呼叫）
void setOrientation(Orient o);
Orient orientation();
const char* orientName(Orient o);
// A4 驗收：序列埠 y<0～8> 只改「畫面上」的表情（不影響互動邏輯），y 單獨＝結束預覽
void setPreview(int moodIndex);
int  preview();
// A5-1：畫布（換整頁時 ui_fx 要存舊畫面）
m5gfx::M5Canvas* canvas();

}  // namespace ui_expression
