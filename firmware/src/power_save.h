// ─────────────────────────────────────────────────────────────────────────
//  power_save：P1 省電（2026-11-20，Miles：充飽拔線 21 分鐘掉到 67%）
//
//  只在 Desktop（表情頁與資訊頁）依桌寵狀態分四級（其他模式、語音中一律不降級）：
//    Active  語音中／情緒反應／換頁或過場中：全速畫、背光正常、CPU 240MHz（跟 P1 之前一樣）
//    Calm    待機臉（Idle）：每秒約 30 張
//    Drowsy  想睡（靜置 6 秒）：每秒 10 張、背光變暗、Wi-Fi 深度省電
//    Asleep  睡著（靜置 11 秒）：每秒 5 張、背光微光、Wi-Fi 深度省電、CPU 80MHz
//  兩張畫面之間主迴圈睡到下一張（最多 10ms 一圈，IMU／按鍵／待機收音照常每 10ms 處理）。
//  被拍、拿起、按 A → 表情一變就立刻回 Active（背光立刻亮、CPU 立刻 240MHz）。
//  資訊頁同一套背光／CPU／Wi-Fi 分級（11-20 Miles）；它本來就每秒才重畫一次，主迴圈固定 10ms 一圈。
//  急停不受影響（ISR＋最高優先 task）。序列埠：z 狀態、z0 關掉省電（對照用）、z1 開、l<0～255> 正常背光。
// ─────────────────────────────────────────────────────────────────────────
#pragma once
#include <stdint.h>

namespace power_save {
enum class Level : uint8_t { Active, Calm, Drowsy, Asleep };

void begin();                    // ui 與 net_hub 都 begin 之後
void loop();                     // 每圈、畫圖之前：決定等級，套用背光／CPU／Wi-Fi
bool frameDue();                 // Desktop 表情頁這一圈要不要畫（非強制重畫時才問）
void frameDrawn();               // 表情頁畫完一張
uint32_t yieldMs();              // 這一圈結尾讓出 CPU 多久（取代 LOOP_YIELD_MS）

Level level();
const char* levelName(Level l);
void setEnabled(bool on);        // z0／z1
bool enabled();
void setAwakeBrightness(uint8_t v);   // l<n>
void printStatus();              // z

// ── P2（2026-11-22）螢幕開關、Wi-Fi 斷線 ──
// 螢幕關：背光 0＋面板睡眠，主迴圈不畫；模式照常運作。Desktop 關螢幕時拍、拿起、聲音都不叫醒（imu_pet 不反應）。
// Desktop 睡著（表情頁睡著／資訊頁靜置 30 秒）或關螢幕滿 3 分鐘 → 斷 Wi-Fi；叫醒、按 A、離開 Desktop → 重連。
// 插著線：桌寵不睡（imu_pet），只有手動關螢幕才算睡。
void setScreenOn(bool on, const char* why);
void toggleScreen(const char* why);
bool screenOn();
void reapplyBacklight();         // 別的畫面直接改過背光（電源鍵長按畫面）→ 下一圈重設
}
