// ─────────────────────────────────────────────────────────────────────────
//  ui_info：模式 1 資訊頁（M1-A A5-1 建立；A7 外觀統一，2026-11-20 Miles 逐版定案）
//
//  Desktop 短按 A（< 0.6 秒）在表情頁／資訊頁之間切換；按住 A 說話會自動回表情頁。
//  A7 起只有一版（q0／q1 共用，原 ui_info_eva 退休）：黑底琥珀橘、磚塊細框＋六角標籤、上下紅黑警示線。
//  直式由上到下：時鐘（台北時間、AM／PM NERV 框、斜體七段 12 小時制＋秒）｜電量（內部／外部框上下疊、
//  七段百分比、電壓小字、語音額度兩行、十格電量條）｜晶片波形＋記憶體長條｜天氣（窄）＋LINK icon 3×3｜快訊。
//  橫式：寬側＝時鐘、電量、快訊；A 鍵側＝天氣、晶片／記憶體上下疊（橫式不顯示 icon）。
//  每秒重畫一次；歷史資料（負載、heap）不論在哪一頁都在收。
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
