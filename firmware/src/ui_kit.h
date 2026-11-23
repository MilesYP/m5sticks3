// ─────────────────────────────────────────────────────────────────────────
//  ui_kit：各頁共用的畫圖元件（2026-11-23 從表情頁／資訊頁／切模式／體感抽出）
//
//  設計規則見 docs/design/README.md，元件對照 docs/design/COMPONENTS.md。
//  新畫面一律用這裡的元件；要改參數（顏色、斜紋寬、字型）先問 Miles，改了所有頁一起變。
//  抽出時以模擬器比對過：表情、資訊、切模式、體感共 1441 張圖逐像素相同。
//  只放「純畫圖」：不碰模式狀態、不送螢幕（送螢幕一律 ui_fx::present）。
// ─────────────────────────────────────────────────────────────────────────
#pragma once
#include <M5Unified.h>
#include <stdint.h>

namespace ui_kit {

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}

// ── 色票（docs/design/README.md 第 1 節）──
namespace color {
constexpr uint16_t BG     = rgb565(0x00, 0x00, 0x00);
constexpr uint16_t AMBER  = rgb565(0xFF, 0x9B, 0x00);   // 主色
constexpr uint16_t BRIGHT = rgb565(0xFF, 0xD0, 0x80);   // 強調（亮橘白）
constexpr uint16_t DIM    = rgb565(0xB0, 0x6A, 0x10);   // 次要（暗橘）
constexpr uint16_t FRAME  = rgb565(0x6A, 0x3A, 0x00);   // 細框、格線
constexpr uint16_t UNLIT  = rgb565(0x24, 0x13, 0x00);   // 七段沒亮的段、選中列底
constexpr uint16_t RED    = rgb565(0xFF, 0x29, 0x23);   // 錯誤／停止／危險、模式大字
constexpr uint16_t YELLOW = rgb565(0xFF, 0xD0, 0x00);   // 注意、黃黑斜紋
constexpr uint16_t GREEN  = rgb565(0x39, 0xFF, 0x14);   // 正常、已連線
constexpr uint16_t PURPLE = rgb565(0xB0, 0x4D, 0xFF);   // 資訊頁 MEM
constexpr uint16_t WHITE  = rgb565(0xF2, 0xE8, 0xD8);   // 切模式英文副標
}  // namespace color

using Gfx = lgfx::LovyanGFX;

// 斜紋：每列色段寬 seg、週期 period，第 r 列相位＝(r + phase) % period（往右下斜）。
//   紅黑警示（資訊頁）：seg 6、period 12、phase 8；黃黑（切模式、體感底部帶）：seg 8、period 16。
void stripes(Gfx& g, int x0, int y0, int w, int h, uint16_t c, int seg, int period, int phase = 0);
inline void hazardRed(Gfx& g, int y, int w, int h) { stripes(g, 0, y, w, h, color::RED, 6, 12, 8); }
inline void stripesYellow(Gfx& g, int y, int w, int h, int phase = 0) { stripes(g, 0, y, w, h, color::YELLOW, 8, 16, phase); }

// 七段數字：段兩端削尖、段間 1px 縫；n＝0～9，10＝「-」；off＝沒亮的段
void hseg(Gfx& g, int x, int y, int len, int t, uint16_t c);
void vseg(Gfx& g, int x, int y, int len, int t, uint16_t c);
void seg7(Gfx& g, int x, int y, int w, int h, int n, uint16_t on, uint16_t off = color::UNLIT);

// 六角形（左右尖，同表情頁背景格方向）。a＝半寬（中心到左右尖）
void hexShape(Gfx& g, int cx, int cy, int a, uint16_t c, bool fill);

// 磚塊細框＋左上／右下四角亮 4px（資訊頁）
void tile(Gfx& g, int x, int y, int w, int h, uint16_t line = color::FRAME, uint16_t corner = color::DIM);
// 六角實心標籤（10px 高、左右各削 3px）：英文 Font0、中文 efontTW_10。回傳寬度。cjkDy＝中文往下幾 px
int hexLabel(Gfx& g, int x, int y, const char* s, uint16_t fill = color::AMBER, uint16_t ink = color::BG, int cjkDy = 0);
// 取景角（四角 L 形）
void brackets(Gfx& g, int x, int y, int w, int h, uint16_t c, int len = 9);

// 小字混排：英文 Font0（往下 1px）、數字與 . : / % - + 用 efontTW_10_b、中文 efontTW_10
int mixedWidth(Gfx& g, const char* s);
int drawMixed(Gfx& g, int x, int y, const char* s, uint16_t c);   // 回傳寬度
// 單一字型，放不下 maxWidth 就截斷加「…」（maxWidth≤0＝不截）
void textFit(Gfx& g, const char* s, int x, int y, uint16_t c, const lgfx::IFont* font, int maxWidth = 0,
             textdatum_t datum = top_left);

// 1-bit 點陣字（gen_kanji／gen_air_labels 產生）：放得下原生尺寸就直接畫，較大才等比縮小、置中在框內
void glyphFit(Gfx& g, const uint8_t* bits, int gw, int gh, int x, int y, int boxW, int boxH, uint16_t c);

// 狀態列（表情頁、體感）：「名稱・狀態」＋右邊電量 %＋電量左邊的實心小標籤，y＝12 一條細線。
// 狀態字放不下就省略（標籤優先）。battery<0＝不顯示。
struct Tag { const char* text; const lgfx::IFont* font; int ty; uint16_t bg; };
void statusBar(Gfx& g, int x0, int w, const char* name, const char* status, uint16_t accent,
               int battery, uint16_t batColor, const Tag* tags = nullptr, int nTags = 0);
uint16_t batteryColor(int pct);   // ≥60 綠、≥20 黃、其他紅

// 底部動作帶（體感）：黃黑（或紅黑）斜紋＋上方 2px 線＋中間黑底放一張點陣字
void actionBand(Gfx& g, int y, int w, int h, uint16_t c, uint16_t line, const uint8_t* bits, int bw, int bh);

}  // namespace ui_kit
