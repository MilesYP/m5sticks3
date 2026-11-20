// ─────────────────────────────────────────────────────────────────────────
//  ui_fx：換整頁時的交叉淡化（A5-1，11-20 Miles 實機兩輪）
//
//  問題：整張 135×240（64KB）經 SPI 40MHz 送進螢幕約 14.6ms（實測），螢幕邊收邊顯示、沒有跟掃描同步，
//        整頁大換（切頁、直橫轉向）時看得到新畫面由上往下蓋過去。
//  第一版（背光先關再亮回）遮住了刷新，但「整頁黑一下再亮」看起來更慢（Miles：要絲滑）→ 改交叉淡化：
//  舊畫面在 UI_XFADE_MS 內溶解成新畫面（約 5～6 幀）。每一幀跟上一幀只差一點點，由上往下刷新就看不出來。
//
//  用法：
//   - 畫面（表情頁、資訊頁）畫完不直接 pushSprite，改呼叫 present(畫布)。
//   - 要換整頁的那一刻（還沒畫新內容前）呼叫 begin(目前顯示中的畫布)：把舊畫面存一份。
//   - 主迴圈每圈呼叫 loop()：淡化中就把「舊畫面 × (1−t) ＋ 最新畫面 × t」推上螢幕。
//  記憶體：舊畫面＋混合結果各一張 135×240×2（PSRAM 共約 128KB）。序列埠 t 開／關（比較用）。
//  11-21（27-a7m）部分更新：present 只把跟上一張不同的矩形送上螢幕（影子緩衝再 64KB）；t2 關、t3 開。
// ─────────────────────────────────────────────────────────────────────────
#pragma once

// M5GFX 裡 M5Canvas 是 m5gfx::M5Canvas 的別名（using），只能前置宣告真正的類別
namespace m5gfx { class M5Canvas; }

namespace ui_fx {
void begin();                       // 建立兩張緩衝（ui_status::begin 之後）
void start(m5gfx::M5Canvas* showing);      // 開始換整頁：showing＝螢幕上目前那一頁的畫布（內容還是舊的）
void present(m5gfx::M5Canvas* cv);         // 畫面畫好了：沒在淡化就直接送上螢幕；淡化中只記下來，由 loop() 混合後送
void loop();                        // 每圈呼叫（畫完之後）
void cancel();                      // 離開 Desktop 等：停止淡化
void invalidate();                  // 11-21：別的畫面直接推過螢幕 → 下一次 present 整張送
void setPartial(bool on);           // 11-21：部分更新開關（序列埠 t2 關／t3 開）
bool active();
void setEnabled(bool on);           // 序列埠 t0／t1
bool enabled();
}
