#include "ui_persona.h"

#include <M5Unified.h>

#if __has_include("persona_assets.h")
#include "persona_assets.h"
#define PERSONA_HAS 1
#else
#define PERSONA_HAS 0
#endif

namespace {
bool s_enabled = true;
#if PERSONA_HAS
Mood s_last = Mood::Idle;
uint32_t s_state_t0 = 0;
#endif

// 取景框四角（EVA 風）
}  // namespace

namespace ui_persona {

bool available() { return PERSONA_HAS != 0; }
bool active() { return available() && s_enabled; }
void setEnabled(bool on) { s_enabled = on; }

const char* name() {
#if PERSONA_HAS
  return persona_assets::NAME;
#else
  return "(none)";
#endif
}

void draw(m5gfx::M5Canvas* cv, int x, int y, int w, int h, Mood m, uint32_t now, bool frozen) {
  // A7：取景框改由 ui_expression 統一畫（像素臉外框）
#if PERSONA_HAS
  using namespace persona_assets;
  int si = static_cast<int>(m);
  if (si < 0 || si > 8 || STATES[si].count == 0) si = 0;   // 缺的狀態用 idle
  if (m != s_last) { s_last = m; s_state_t0 = now; }
  const State& st = STATES[si];
  const uint32_t idx = frozen ? 0 : ((now - s_state_t0) * st.fps / 1000) % st.count;
  const uint8_t* fr = FRAMES + (uint32_t)(st.first + idx) * W * H;

  // 整數倍放大，留 8px 邊給取景框
  int s = (w - 8) / W;
  const int sy = (h - 8) / H;
  if (sy < s) s = sy;
  if (s < 1) s = 1;
  const int ox = x + (w - W * s) / 2, oy = y + (h - H * s) / 2;
  for (int j = 0; j < H; ++j) {
    const uint8_t* row = fr + j * W;
    int i = 0;
    while (i < W) {   // 同色連續的像素合成一個長方形，少呼叫幾次 fillRect
      const uint8_t c = row[i];
      int k = i + 1;
      while (k < W && row[k] == c) ++k;
      if (c) cv->fillRect(ox + i * s, oy + j * s, (k - i) * s, s, PALETTE[c]);
      i = k;
    }
  }
#else
  (void)m; (void)now; (void)frozen;
#endif
}

}  // namespace ui_persona
