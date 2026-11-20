// ─────────────────────────────────────────────────────────────────────────
//  ui_info：模式 1 資訊頁（M1-A / A5-1，Miles 11-20 選方案一「磚塊儀表板」＋合成波風格）
//
//  Desktop 短按 A（< 0.6 秒，沿用語音誤觸防護的分界）在表情頁／資訊頁之間切換；按住 A 說話會自動回表情頁。
//  欄位：時鐘（NTP 對時）＋日期＋開機時間＋語音額度（A5-2）｜電量＋電壓＋功能 icon（藍牙、RF、nRF24、紅外線、USB、滑鼠）
//        IMU 氣泡｜天氣（A5-2）｜CHIP：溫度、頻率、主迴圈負載波形｜MEM：heap 長條＋最低值、PSRAM
//        NET LINK：Wi-Fi 訊號格＋dBm、中控、語音狀態｜MODULES：Grove 配件、韌體版本｜快訊（A5-2）
//  直式／橫式（A 左／A 右）：自己的畫布（PSRAM 約 64KB），跟表情頁一樣用 M5Canvas::setRotation 轉向；
//  橫式時電量、連線、配件、快訊放在 A 鍵側。
//  每秒重畫一次（時間跳秒時）；歷史資料（負載、heap）不論在哪一頁都在收。
// ─────────────────────────────────────────────────────────────────────────
#pragma once

// M5GFX 裡 M5Canvas 是 m5gfx::M5Canvas 的別名（using），只能前置宣告真正的類別
namespace m5gfx { class M5Canvas; }

namespace ui_info {

void begin();                              // 建立畫布（ui_status::begin 之後）
void loop();                               // 每圈呼叫：收歷史資料、Wi-Fi 連上後開始對時（不畫圖、非阻塞）
void render();                             // 資訊頁顯示中時每圈呼叫；只在跳秒／剛切過來／轉向時才真的重畫
bool shown();
void setShown(bool on, const char* why);   // 切頁（印 [ui] 表情頁 → 資訊頁）；切到資訊頁會立刻重畫
bool timeSynced();
m5gfx::M5Canvas* canvas();                        // 換整頁時 ui_fx 要存舊畫面

}  // namespace ui_info
