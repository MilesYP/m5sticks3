// ─────────────────────────────────────────────────────────────────────────
//  ui_modeswitch：B 短按切模式的過場畫面（EVA 風，11-20 Miles 定案）
//
//  黃黑警示條紋＋模式編號＋明朝體大字（kanji_bitmaps.h）＋進度條。
//  目的不只是好看：mode_manager 的 exit()／enter() 會開關 Wi-Fi、ES8311 等資源（之後各里程碑會越來越久），
//  過場先畫上螢幕、蓋住這段等待；進度條照實際進度走——資源還沒好停在 25%，好了才在 UI_MODESWITCH_MS 內跑完。
//
//  用法（main.cpp）：
//    ui_modeswitch::start(下一個模式);   // 立刻畫第一幀並推上螢幕
//    mode_manager::next();              // 阻塞也沒關係，畫面已經是過場
//    ui_modeswitch::ready();
//    之後每圈 renderActive() 先問 active()：是就 render()，跑完直接交還給新模式。
//  急停：cancel()，直接畫 KILLED（急停不走過場）。
//  記憶體：自己一張 135×240 畫布（PSRAM 約 64KB）。過場依目前拿法支援直式、橫式 A 左與 A 右。
// ─────────────────────────────────────────────────────────────────────────
#pragma once
#include <stdint.h>

#include "mode_manager.h"

namespace m5gfx { class M5Canvas; }

namespace ui_modeswitch {
void begin();                 // 建立畫布（ui_status::begin 之後）
void start(Mode target);      // 開始過場：畫第一幀並直接推上螢幕
void ready();                 // mode_manager 切換完成（新模式 enter() 已回來）
bool active();
bool render();                // 畫一幀；回 true＝還在過場（呼叫端不要畫別的），false＝剛結束、請畫新模式
void cancel();                // 急停：立即結束過場
m5gfx::M5Canvas* canvas();
}
