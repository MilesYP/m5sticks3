#include "ui_kit.h"
#include "status_labels.h"

#include <stdio.h>
#include <string.h>

namespace ui_kit {

void stripes(Gfx& g, int x0, int y0, int w, int h, uint16_t c, int seg, int period, int phase) {
  const int x1 = x0 + w;
  for (int row = 0; row < h; ++row) {
    const int off = (row + phase) % period;
    for (int left = x0 - off; left < x1; left += period) {
      const int a = left < x0 ? x0 : left;
      const int b = left + seg < x1 ? left + seg : x1;
      if (b > a) g.drawFastHLine(a, y0 + row, b - a, c);
    }
  }
}

void hseg(Gfx& g, int x, int y, int len, int t, uint16_t c) {
  for (int r = 0; r < t; ++r) {
    const int in = r < (t - 1) / 2 ? (t - 1) / 2 - r : r - t / 2 > 0 ? r - t / 2 : 0;
    if (len - 2 * in > 0) g.drawFastHLine(x + in, y + r, len - 2 * in, c);
  }
}
void vseg(Gfx& g, int x, int y, int len, int t, uint16_t c) {
  for (int k = 0; k < t; ++k) {
    const int in = k < (t - 1) / 2 ? (t - 1) / 2 - k : k - t / 2 > 0 ? k - t / 2 : 0;
    if (len - 2 * in > 0) g.drawFastVLine(x + k, y + in, len - 2 * in, c);
  }
}
void seg7(Gfx& g, int x, int y, int w, int h, int n, uint16_t on, uint16_t off) {
  constexpr uint8_t mask[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f, 0x40};
  const int t = w >= 11 ? 3 : 2;
  const int mid = (h - t) / 2;                    // 中間那段的 y
  const int hl = w - 2;                           // 橫段長（左右各讓 1px 縫）
  const int vu = mid - 1, vl = h - t - mid - 1;   // 上、下直段長（上下各讓縫）
  auto c = [&](int i) { return (mask[n] & (1 << i)) ? on : off; };
  hseg(g, x + 1, y, hl, t, c(0));                        // a
  vseg(g, x + w - t, y + 1, vu, t, c(1));                // b
  vseg(g, x + w - t, y + mid + 1, vl + t - 1, t, c(2));  // c
  hseg(g, x + 1, y + h - t, hl, t, c(3));                // d
  vseg(g, x, y + mid + 1, vl + t - 1, t, c(4));          // e
  vseg(g, x, y + 1, vu, t, c(5));                        // f
  hseg(g, x + 1, y + mid, hl, t, c(6));                  // g
}

void hexShape(Gfx& g, int cx, int cy, int a, uint16_t c, bool fill) {
  const int h = a * 866 / 1000;
  const int l = cx - a, ml = cx - a / 2, mr = cx + a / 2, r = cx + a, t = cy - h, b = cy + h;
  if (fill) {
    g.fillRect(ml, t, mr - ml + 1, b - t + 1, c);
    g.fillTriangle(l, cy, ml, t, ml, b, c);
    g.fillTriangle(r, cy, mr, t, mr, b, c);
  } else {
    g.drawLine(l, cy, ml, t, c); g.drawFastHLine(ml, t, mr - ml + 1, c); g.drawLine(mr, t, r, cy, c);
    g.drawLine(r, cy, mr, b, c); g.drawFastHLine(ml, b, mr - ml + 1, c); g.drawLine(ml, b, l, cy, c);
  }
}

void tile(Gfx& g, int x, int y, int w, int h, uint16_t line, uint16_t corner) {
  g.drawRect(x, y, w, h, line);
  g.drawFastHLine(x, y, 4, corner); g.drawFastVLine(x, y, 4, corner);
  g.drawFastHLine(x + w - 4, y + h - 1, 4, corner); g.drawFastVLine(x + w - 1, y + h - 4, 4, corner);
}

int hexLabel(Gfx& g, int x, int y, const char* s, uint16_t fill, uint16_t ink, int cjkDy) {
  const bool cjk = (uint8_t)s[0] >= 0x80;
  g.setFont(cjk ? (const lgfx::IFont*)&fonts::efontTW_10 : (const lgfx::IFont*)&fonts::Font0);
  const int w = g.textWidth(s) + 8, h = 10;
  for (int row = 0; row < h; ++row) {
    const int fc = row <= h / 2 ? h / 2 - row : row - h / 2;
    const int in = fc * 3 / (h / 2);
    g.drawFastHLine(x + in, y + row, w - 2 * in, fill);
  }
  g.setTextColor(ink);
  g.drawString(s, x + 4, cjk ? y + cjkDy : y + 1);
  return w;
}

void brackets(Gfx& g, int x, int y, int w, int h, uint16_t c, int l) {
  g.drawFastHLine(x, y, l, c);  g.drawFastVLine(x, y, l, c);
  g.drawFastHLine(x + w - l, y, l, c);  g.drawFastVLine(x + w - 1, y, l, c);
  g.drawFastHLine(x, y + h - 1, l, c);  g.drawFastVLine(x, y + h - l, l, c);
  g.drawFastHLine(x + w - l, y + h - 1, l, c);  g.drawFastVLine(x + w - 1, y + h - l, l, c);
}

// ── 小字混排 ──
namespace {
enum { RUN_THIN, RUN_NUM, RUN_CJK };
int runClass(char ch) {
  if ((uint8_t)ch >= 0x80) return RUN_CJK;
  return ((ch >= '0' && ch <= '9') || ch == '.' || ch == ':' || ch == '/' || ch == '%' || ch == '-' || ch == '+') ? RUN_NUM : RUN_THIN;
}
const lgfx::IFont* runFont(int k) {
  return k == RUN_NUM ? (const lgfx::IFont*)&fonts::efontTW_10_b
         : k == RUN_CJK ? (const lgfx::IFont*)&fonts::efontTW_10 : (const lgfx::IFont*)&fonts::Font0;
}
size_t nextRun(const char* p, char* run, size_t cap, int* kind) {
  *kind = runClass(*p);
  size_t n = 0;
  while (p[n] && runClass(p[n]) == *kind && n < cap - 1) { run[n] = p[n]; ++n; }
  while (n > 0 && p[n] && ((uint8_t)p[n] & 0xC0) == 0x80) --n;   // 不切斷 UTF-8
  if (n == 0) n = 1;
  run[n] = '\0';
  return n;
}
}  // namespace

int mixedWidth(Gfx& g, const char* s) {
  int w = 0; char run[64];
  for (const char* p = s; *p;) { int k; const size_t n = nextRun(p, run, sizeof run, &k); g.setFont(runFont(k)); w += g.textWidth(run); p += n; }
  return w;
}
int drawMixed(Gfx& g, int x, int y, const char* s, uint16_t c) {
  const int x0 = x;
  g.setTextColor(c);
  char run[64];
  for (const char* p = s; *p;) {
    int k; const size_t n = nextRun(p, run, sizeof run, &k);
    g.setFont(runFont(k));
    x += g.drawString(run, x, k == RUN_THIN ? y + 1 : y);
    p += n;
  }
  return x - x0;
}

void textFit(Gfx& g, const char* s, int x, int y, uint16_t c, const lgfx::IFont* font, int maxWidth, textdatum_t datum) {
  char buf[64];
  snprintf(buf, sizeof buf, "%s", s);
  g.setFont(font);
  if (maxWidth > 0 && g.textWidth(buf) > maxWidth) {
    size_t len = strlen(buf);
    char tmp[64];
    while (len) {
      do { --len; } while (len && ((uint8_t)buf[len] & 0xc0) == 0x80);
      while (len && buf[len - 1] == ' ') --len;
      snprintf(tmp, sizeof tmp, "%.*s…", int(len), buf);
      if (g.textWidth(tmp) <= maxWidth) break;
    }
    snprintf(tmp, sizeof tmp, "%.*s…", int(len), buf);
    snprintf(buf, sizeof buf, "%s", tmp);
  }
  g.setTextDatum(datum);
  g.setTextColor(c);
  g.drawString(buf, x, y);
  g.setTextDatum(top_left);
}

void glyphFit(Gfx& g, const uint8_t* bits, int gw, int gh, int x, int y, int boxW, int boxH, uint16_t c) {
  int width = gw, height = gh;
  if (width > boxW) { width = boxW; height = gh * boxW / gw; }
  if (height > boxH) { height = boxH; width = gw * boxH / gh; }
  x += (boxW - width) / 2;
  y += (boxH - height) / 2;
  if (width == gw && height == gh) { g.drawBitmap(x, y, bits, gw, gh, c); return; }
  // 依原始點陣的橫向連續區段縮放，維持明朝體字形的銳利邊緣。
  const int stride = (gw + 7) / 8;
  for (int row = 0; row < height; ++row) {
    const uint8_t* line = bits + (row * gh / height) * stride;
    int runStart = -1;
    for (int col = 0; col <= width; ++col) {
      const int srcX = col * gw / width;
      const bool set = col < width && (line[srcX / 8] & (0x80 >> (srcX % 8)));
      if (set && runStart < 0) runStart = col;
      if (!set && runStart >= 0) { g.drawFastHLine(x + runStart, y + row, col - runStart, c); runStart = -1; }
    }
  }
}

uint16_t batteryColor(int pct) { return pct >= 60 ? color::GREEN : pct >= 20 ? color::YELLOW : color::RED; }

void statusBar(Gfx& g, int x0, int w, const char* name, const char* status, uint16_t accent,
               int battery, uint16_t batColor, const Tag* tags, int nTags) {
  g.fillRect(x0, 0, w, 12, color::BG);   // 底塗黑：符號、眉毛不會壓到字
  g.setTextSize(1);
  g.setTextDatum(top_left);
  int rx = x0 + w - 3;
  if (battery >= 0) {
    char b[8];
    snprintf(b, sizeof b, "%d%%", battery);
    g.setFont(&fonts::Font0);
    g.setTextColor(batColor);
    g.drawString(b, rx - g.textWidth(b), 3);
    rx -= g.textWidth("100%") + 3;
  }
  for (int i = 0; i < nTags; ++i) {   // 電量左邊的實心小標籤（ROOT、斷線…）
    g.setFont(tags[i].font);
    const int tw = g.textWidth(tags[i].text);
    rx -= tw + 4;
    g.fillRect(rx, 1, tw + 4, 11, tags[i].bg);
    g.setTextColor(color::BG);
    g.drawString(tags[i].text, rx + 2, tags[i].ty);
    rx -= 3;
  }
  g.setFont(&fonts::Font0);
  g.setTextColor(accent);
  g.drawString(name, x0 + 3, 3);
  int x = x0 + 3 + g.textWidth(name) + 3;
  g.fillRect(x, 6, 2, 2, accent);
  x += 5;
  g.setFont(&fonts::efontTW_10);
  if (status) {
    const auto* label = status_labels::find(status);
    if (label && x + label->w <= rx) {
      g.drawBitmap(x, (12 - label->h) / 2, label->bits, label->w, label->h, accent);
    } else if (x + g.textWidth(status) <= rx) {
      g.drawString(status, x, 1);
    }
  }
  g.drawFastHLine(x0, 12, w, accent);
}

void actionBand(Gfx& g, int y, int w, int h, uint16_t c, uint16_t line, const uint8_t* bits, int bw, int bh) {
  stripes(g, 0, y, w, h, c, 8, 16, y);
  g.fillRect(0, y - 2, w, 2, line);
  const int boxW = (bits ? bw : 60) + 14;
  const int x = (w - boxW) / 2;
  g.fillRect(x, y + 1, boxW, h - 2, color::BG);
  if (bits) g.drawBitmap(x + 7, y + 2, bits, bw, bh, c);
}

}  // namespace ui_kit
