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
  if (!s_active) cv->pushSprite(0, 0);
}

void loop() {
  if (!s_active || !s_target) return;
  const uint32_t dt = millis() - s_t0;
  if (dt >= cfg::UI_XFADE_MS) {
    s_active = false;
    s_target->pushSprite(0, 0);   // 最後一幀：純新畫面
    return;
  }
  float t = (float)dt / cfg::UI_XFADE_MS;
  t = t * t * (3 - 2 * t);        // smoothstep：頭尾慢、中間快
  uint32_t k = (uint32_t)(t * 32 + 0.5f);
  if (k < 1) k = 1;
  if (k > 31) k = 31;
  blend((const uint16_t*)s_prev.getBuffer(), (const uint16_t*)s_target->getBuffer(), (uint16_t*)s_mix.getBuffer(), s_px, k);
  s_mix.pushSprite(0, 0);
}

void cancel() { s_active = false; }
bool active() { return s_active; }

void setEnabled(bool on) {
  s_on = on;
  if (!on) s_active = false;
  Serial.printf("[ui] 換頁交叉淡化 %s\n", on ? "開（t0 關）" : "關（t1 開）");
}
bool enabled() { return s_on; }

}  // namespace ui_fx
