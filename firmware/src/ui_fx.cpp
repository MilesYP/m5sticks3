#include "ui_fx.h"

#include <M5Unified.h>
#include <string.h>

#include "config.h"

namespace {
M5Canvas s_prev(&M5.Display);   // 換頁前螢幕上的畫面
M5Canvas s_mix(&M5.Display);    // 混合結果（送上螢幕的那張）
bool      s_ready = false;
bool      s_on = true;
bool      s_active = false;
uint32_t  s_t0 = 0;
M5Canvas* s_target = nullptr;   // 新的一頁（最新一次 present 的畫布）
size_t    s_px = 0;             // 像素數
int       s_w = 0, s_h = 0;     // 實體尺寸（螢幕 rotation 0：135×240，跟畫布緩衝的排列一樣）

// 11-21（27-a7m）部分更新：螢幕上目前內容的影子。present 時跟新畫面逐列比較，只把有變的矩形送上螢幕
// （整張 64KB 經 SPI 約 14.6ms，表情頁一幀通常只動眼睛、嘴巴、符號那幾塊）。
// 其他直接推螢幕的畫面（切模式過場、狀態頁）推完要呼叫 invalidate()，下一次就整張送。
uint16_t* s_shadow = nullptr;
bool      s_shadow_ok = false;
bool      s_partial = true;     // 序列埠 t2／t3 可關（對照用）

// 16 位元畫布的緩衝是「位元組對調」的 RGB565（LGFX 的 swap565）：先轉回、混合、再轉回去
inline uint16_t bswap(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }

// a→b，k＝0～32（32＝全是 b）。把 565 拆成 0x07E0F81F 的欄位一次算三色，欄位之間的空位夠放 ×32
void blend(const uint16_t* a, const uint16_t* b, uint16_t* out, size_t n, uint32_t k) {
  const uint32_t ik = 32 - k;
  for (size_t i = 0; i < n; ++i) {
    const uint16_t pa = a[i], pb = b[i];
    if (pa == pb) { out[i] = pa; continue; }
    const uint32_t ua = bswap(pa), ub = bswap(pb);
    const uint32_t x = (ua | (ua << 16)) & 0x07E0F81Fu;
    const uint32_t y = (ub | (ub << 16)) & 0x07E0F81Fu;
    const uint32_t r = ((x * ik + y * k) >> 5) & 0x07E0F81Fu;
    out[i] = bswap((uint16_t)((r & 0xFFFF) | (r >> 16)));
  }
}
// 逐列比對 cur 與 shadow，相鄰的變動列（中間不超過 GAP 列沒變）合成一個矩形，左右只取有變的範圍
template <typename Emit>
void diffRects(const uint16_t* cur, uint16_t* sh, int W, int H, Emit emit) {
  constexpr int GAP = 6;
  int y = 0;
  while (y < H) {
    if (memcmp(cur + y * W, sh + y * W, W * 2) == 0) { ++y; continue; }
    const int y0 = y;
    int yl = y, x0 = W, x1 = -1, gap = 0;
    for (; y < H; ++y) {
      const uint16_t* a = cur + y * W;
      const uint16_t* b = sh + y * W;
      if (memcmp(a, b, W * 2) == 0) { if (++gap > GAP) break; continue; }
      gap = 0;
      yl = y;
      int l = 0;
      while (a[l] == b[l]) ++l;
      int r = W - 1;
      while (a[r] == b[r]) --r;
      if (l < x0) x0 = l;
      if (r > x1) x1 = r;
    }
    emit(x0, y0, x1 - x0 + 1, yl - y0 + 1);
    for (int yy = y0; yy <= yl; ++yy) memcpy(sh + yy * W + x0, cur + yy * W + x0, (x1 - x0 + 1) * 2);
    y = yl + 1;
  }
}

void pushFull(M5Canvas* cv) {
  cv->pushSprite(0, 0);
  if (s_shadow) { memcpy(s_shadow, cv->getBuffer(), s_px * 2); s_shadow_ok = true; }
}

void pushPartial(M5Canvas* cv) {
  if (!s_partial || !s_shadow || !s_shadow_ok) { pushFull(cv); return; }
  const uint16_t* cur = (const uint16_t*)cv->getBuffer();
  bool any = false;
  diffRects(cur, s_shadow, s_w, s_h, [&](int x, int y, int w, int h) {
    if (!any) { M5.Display.startWrite(); any = true; }
    M5.Display.setAddrWindow(x, y, w, h);
    // 畫布緩衝是 swap565（位元組對調），用型別告訴 LGFX 不要再轉換
    for (int r = 0; r < h; ++r) M5.Display.writePixels((const lgfx::swap565_t*)(cur + (y + r) * s_w + x), w);
  });
  if (any) M5.Display.endWrite();
}
}  // namespace

namespace ui_fx {

void begin() {
  const int w = M5.Display.width(), h = M5.Display.height();
  for (M5Canvas* c : {&s_prev, &s_mix}) {
    c->setPsram(true);
    c->setColorDepth(16);
    c->createSprite(w, h);
  }
  s_ready = s_prev.getBuffer() && s_mix.getBuffer();
  s_px = (size_t)w * h;
  s_w = w; s_h = h;
  s_shadow = (uint16_t*)ps_malloc(s_px * 2);   // 部分更新的影子（PSRAM 64KB）
  if (!s_shadow) Serial.println("[ui] 部分更新影子緩衝配置失敗 → 每幀整張送");
  if (!s_ready) Serial.println("[ui] 交叉淡化緩衝配置失敗（PSRAM 不足？）→ 換頁直接切換");
}

void start(M5Canvas* showing) {
  if (!s_on || !s_ready || !showing || !showing->getBuffer()) return;
  // 已經在淡化：螢幕上是混合結果 → 從那裡接著淡
  const void* src = s_active ? s_mix.getBuffer() : showing->getBuffer();
  memcpy(s_prev.getBuffer(), src, s_px * 2);
  s_active = true;
  s_t0 = millis();
}

void present(M5Canvas* cv) {
  s_target = cv;
  if (!s_active) pushPartial(cv);
}

void loop() {
  if (!s_active || !s_target) return;
  const uint32_t dt = millis() - s_t0;
  if (dt >= cfg::UI_XFADE_MS) {
    s_active = false;
    pushFull(s_target);           // 最後一幀：純新畫面（同時更新影子）
    return;
  }
  float t = (float)dt / cfg::UI_XFADE_MS;
  t = t * t * (3 - 2 * t);        // smoothstep：頭尾慢、中間快
  uint32_t k = (uint32_t)(t * 32 + 0.5f);
  if (k < 1) k = 1;
  if (k > 31) k = 31;
  blend((const uint16_t*)s_prev.getBuffer(), (const uint16_t*)s_target->getBuffer(), (uint16_t*)s_mix.getBuffer(), s_px, k);
  s_mix.pushSprite(0, 0);
  s_shadow_ok = false;            // 螢幕上是混合畫面，影子不準了
}

void cancel() { s_active = false; }
void invalidate() { s_shadow_ok = false; }
void setPartial(bool on) {
  s_partial = on;
  s_shadow_ok = false;
  Serial.printf("[ui] 部分更新 %s\n", on ? "開（t2 關）" : "關（t3 開，每幀整張送）");
}
bool active() { return s_active; }

void setEnabled(bool on) {
  s_on = on;
  if (!on) s_active = false;
  Serial.printf("[ui] 換頁交叉淡化 %s\n", on ? "開（t0 關）" : "關（t1 開）");
}
bool enabled() { return s_on; }

}  // namespace ui_fx
