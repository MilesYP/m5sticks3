// ─────────────────────────────────────────────────────────────────────────
//  power_key：P2 電源鍵新機制（2026-11-22，規格 docs/KICKOFF_P2.md）
//
//  電源鍵由 M5PM1 管。韌體改寫 PM1 設定，讓按鍵改由韌體判斷：
//    新機制（預設，每次開機都重寫一次）：
//      關掉 PM1 的「單按重置」「雙擊關機」、鎖住下載模式，開 PM1 看門狗（主迴圈每秒餵）
//      單按＝開關螢幕；連按五下＝強制關機；
//      長按：按住時畫面顯示進度，3～5 秒之間放開＝關機、按滿 5 秒放開＝進 root
//    root（原廠）：恢復 PM1 原廠按鍵（單按重置、雙擊關機、長按下載模式）、看門狗關；
//      韌體不再判斷電源鍵。單按（PM1 重置）重新開機後回到新機制。
//  PM1 的設定重置 ESP32 也不會清，換燒其他韌體前一定先進 root。
//  輪詢在主迴圈（內部 I2C 只有主迴圈在用）。急停不經過這裡。
//  序列埠：P 印 PM1 設定與按鍵狀態；P0 進 root；P1 回新機制；P9 讓主迴圈卡死（測看門狗）。
// ─────────────────────────────────────────────────────────────────────────
#pragma once
#include <stdint.h>

namespace power_key {
void begin();                        // M5.begin() 之後：寫入新機制設定、開看門狗
void loop();                         // 主迴圈每圈：餵狗、輪詢電源鍵、長按畫面
bool overlayActive();                // 長按畫面中（主迴圈這時不畫一般畫面）
void drawBadge();                    // root 模式時在畫面角落蓋「ROOT」（畫完一般畫面之後呼叫）
bool rootMode();
void setRoot(bool root);             // 序列埠 P0／P1
[[noreturn]] void shutdown(const char* why, const char* line1, const char* line2);   // 顯示訊息後關機
void printStatus();                  // 序列埠 P
void hangTest();                     // 序列埠 P9：主迴圈卡死（只有看門狗救得回來）
}
