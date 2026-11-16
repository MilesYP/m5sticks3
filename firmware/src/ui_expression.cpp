#include "ui_expression.h"

#include <M5Unified.h>
#include <math.h>

#include "config.h"

namespace {

// 全螢幕畫布（放 PSRAM，避免佔用內部 RAM；與 ui_status 的畫布各自獨立）
M5Canvas s_cv(&M5.Display);
bool s_ready = false;

// 眨眼狀態機
bool     s_blinking = false;
uint32_t s_blink_start_ms = 0;
uint32_t s_last_blink_end_ms = 0;
uint32_t s_next_gap_ms = 2800;    // 下次眨眼的間隔（會變動）

constexpr uint32_t BLINK_DUR_MS = 120;   // 一次眨眼壓扁多久

}  // namespace

namespace ui_expression {

void begin() {
  s_cv.setPsram(true);           // 大緩衝放 PSRAM
  s_cv.setColorDepth(16);
  s_cv.createSprite(M5.Display.width(), M5.Display.height());
  s_ready = (s_cv.getBuffer() != nullptr);
  s_last_blink_end_ms = millis();
}

void render() {
  if (!s_ready) return;
  const uint32_t now = millis();

  // ── 眨眼排程 ──
  if (!s_blinking && now - s_last_blink_end_ms >= s_next_gap_ms) {
    s_blinking = true;
    s_blink_start_ms = now;
  }
  if (s_blinking && now - s_blink_start_ms >= BLINK_DUR_MS) {
    s_blinking = false;
    s_last_blink_end_ms = now;
    s_next_gap_ms = 2500 + (now % 1800);   // 2.5～4.3 秒隨機
  }

  // ── 幾何 ──
  const int W = s_cv.width();
  const int H = s_cv.height();
  const int cx = W / 2;
  const int cy = (int)(H * 0.42f);
  const int ew = 32;            // 眼寬（放大）
  const int eh_base = 46;       // 眼高（放大）
  const int gap = 28;           // 中心到各眼中心的距離
  const int r = 11;             // 圓角

  // 呼吸：整組眼睛「上下平移」（像胸口起伏），形狀不變；眨眼才壓扁
  const int breathe = (int)(sinf(now / 318.0f) * 9.0f);   // 約 2 秒一個來回、上下 ±9px
  int eh = eh_base;
  if (s_blinking) eh = 5;
  const int ey = cy - eh / 2 + breathe;
  const uint16_t col = s_cv.color565(76, 240, 232);  // 待機青（固定色）

  // ── 繪製 ──
  s_cv.fillScreen(TFT_BLACK);
  s_cv.fillSmoothRoundRect(cx - gap - ew / 2, ey, ew, eh, r, col);  // 左眼
  s_cv.fillSmoothRoundRect(cx + gap - ew / 2, ey, ew, eh, r, col);  // 右眼
  s_cv.pushSprite(0, 0);
}

}  // namespace ui_expression
