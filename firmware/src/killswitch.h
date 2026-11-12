// ─────────────────────────────────────────────────────────────────────────
//  killswitch：實體急停（鐵則三），兩段式
//
//  第一段（微秒級，不經主迴圈）：
//    A 鍵(G11) 硬體中斷 ISR（中斷服務程式 = 腳位電位一變就被 CPU 立刻執行的
//    小函式）→ 關閉「輸出閘門」並喚醒最高優先權的急停 task。日後 HID/RF 送出
//    任何東西前都必須先查 outputAllowed()，閘門關了就一個封包都不送。
//  第二段（毫秒級，在 loop）：
//    主迴圈看到 isTriggered() → mode_manager::emergencyStop() 呼叫 exit() 回收
//    資源、進入 SAFE。
//
//  A 鍵只在「armed」時觸發急停（目前只有模式 2）；B 長按 3 秒走 triggerSoftware()。
// ─────────────────────────────────────────────────────────────────────────
#pragma once
#include <stdint.h>

enum class KillSource : uint8_t { None, BtnA, BtnBHold };

namespace killswitch {

void begin();                     // 建立急停 task、掛 G11 中斷
bool isTriggered();
bool outputAllowed();             // false = 禁止送出任何 HID/RF 輸出
void triggerSoftware();           // B 長按 3 秒的備援觸發
void setArmedByBtnA(bool armed);  // 由 mode_manager 依模式設定
bool armedByBtnA();
void clear();                     // 解除急停（僅限 mode_manager::resumeFromSafe）

KillSource source();
const char* sourceName();
int32_t lastLatencyUs();          // 觸發 → 急停 task 完成第一段的耗時；-1 = 尚未觸發

}  // namespace killswitch
