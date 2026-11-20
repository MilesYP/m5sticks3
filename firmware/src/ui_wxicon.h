// A5-2：天氣小圖示（向量畫，q0 合成波與 q1 EVA 共用）。中心 (cx, cy)，大約 2r×2r 的方塊內。
// 顏色由呼叫端給：q0 用太陽黃／雲白／雨青，q1 全部琥珀色。
#pragma once
#include <M5Unified.h>
#include <math.h>

#include "info_data.h"

namespace ui_wxicon {

inline void cloud(lgfx::LGFXBase& g, int cx, int cy, int r, uint16_t c) {
  // 兩個圓＋底部平的雲：寬約 2r、高約 r
  const int b = cy + r / 2;                 // 底線
  g.fillCircle(cx - r / 3, b - r / 3, r / 3 + 1, c);
  g.fillCircle(cx + r / 4, b - r / 2, r / 2, c);
  g.fillRect(cx - r + 2, b - r / 3, 2 * r - 3, r / 3 + 1, c);
  g.fillCircle(cx - r + 2, b - 1, 1, c);
  g.fillCircle(cx + r - 2, b - 1, 1, c);
}

inline void sun(lgfx::LGFXBase& g, int cx, int cy, int r, uint16_t c) {
  const int core = r / 2;
  g.fillCircle(cx, cy, core, c);
  for (int i = 0; i < 8; ++i) {
    const float a = i * 0.785398f;
    const float ca = cosf(a), sa = sinf(a);
    g.drawLine(cx + (int)lroundf(ca * (core + 2)), cy + (int)lroundf(sa * (core + 2)),
               cx + (int)lroundf(ca * r), cy + (int)lroundf(sa * r), c);
  }
}

inline void draw(lgfx::LGFXBase& g, int cx, int cy, int r, info_data::WxIcon ic, uint16_t sunC, uint16_t cloudC,
                 uint16_t rainC) {
  using info_data::WxIcon;
  switch (ic) {
    case WxIcon::Sun:
      sun(g, cx, cy, r, sunC);
      break;
    case WxIcon::Part:
      sun(g, cx - r / 3, cy - r / 3, r * 2 / 3, sunC);
      cloud(g, cx + 1, cy + 1, r - 1, cloudC);
      break;
    case WxIcon::Cloud:
      cloud(g, cx, cy, r, cloudC);
      break;
    case WxIcon::Rain:
    case WxIcon::Storm: {
      cloud(g, cx, cy - r / 3, r, cloudC);
      const int y0 = cy + r / 3, y1 = cy + r;
      if (ic == WxIcon::Rain) {
        for (int k = -1; k <= 1; ++k) g.drawLine(cx + k * r / 2 + 1, y0, cx + k * r / 2 - 1, y1, rainC);
      } else {   // 閃電
        g.drawLine(cx + 1, y0, cx - 2, cy + r * 2 / 3, sunC);
        g.drawLine(cx - 2, cy + r * 2 / 3, cx + 2, cy + r * 2 / 3, sunC);
        g.drawLine(cx + 2, cy + r * 2 / 3, cx - 1, y1 + 1, sunC);
      }
      break;
    }
  }
}

}  // namespace ui_wxicon
