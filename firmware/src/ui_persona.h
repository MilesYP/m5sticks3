// ─────────────────────────────────────────────────────────────────────────
//  ui_persona：可替換的本機角色（11-20 Miles）
//
//  Desktop 表情頁的「臉」可以換成一組像素動畫（每個 Mood 一段循環）。素材放在本機
//  firmware/persona_local/（.gitignore），用 tools/sprite2h.py 轉成 firmware/src/persona_assets.h（也在 .gitignore）。
//  編譯時有這個檔＝啟用角色；沒有＝維持原本的合成波表情（公開 repo 永遠是這個狀態）。
//  對話區、斷線圖示、交叉淡化等都沿用 ui_expression，這裡只負責畫臉的區域。
//  規格與交接說明：docs/persona-swap.md
// ─────────────────────────────────────────────────────────────────────────
#pragma once
#include <stdint.h>

#include "ui_expression.h"   // Mood

namespace m5gfx { class M5Canvas; }

namespace ui_persona {
bool available();             // 編譯時有 persona_assets.h
bool active();                // available() 且沒被序列埠 q0 關掉
void setEnabled(bool on);     // 序列埠 q0／q1：跟原本表情對照
const char* name();
// 在 (x, y, w, h) 這塊臉的區域畫目前狀態的那一幀（整數倍放大、置中），外加 EVA 風取景框
void draw(m5gfx::M5Canvas* cv, int x, int y, int w, int h, Mood m, uint32_t now_ms, bool frozen);
}
