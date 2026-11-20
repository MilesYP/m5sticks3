#include "ui_modeswitch.h"

#include <M5Unified.h>
#include <stdio.h>

#include "config.h"
#include "kanji_bitmaps.h"
#include "ui_expression.h"
#include "ui_fx.h"

namespace {

M5Canvas s_cv(&M5.Display);
bool s_ready_cv = false;
bool s_active = false;
bool s_mode_ready = false;
Mode s_target = Mode::Desktop;
uint32_t s_t0 = 0, s_t_ready = 0;
int s_drawn_rot = -1;
int s_filled_pixels = 0;

// EVA 風配色（RGB565 在 begin() 算好）
uint16_t C_AMBER, C_YELLOW, C_RED, C_WHITE;

const char* kanjiKey(Mode m) {
  switch (m) {
    case Mode::Desktop:  return "voice";
    case Mode::HidAgent: return "hid";
    case Mode::RfTool:   return "rf";
    case Mode::AirMouse: return "air";
    default:             return "kill";
  }
}
const char* label(Mode m) {
  switch (m) {
    case Mode::Desktop:  return "FRIDAY VOICE";
    case Mode::HidAgent: return "HID AGENT";
    case Mode::RfTool:   return "RF TOOL";
    case Mode::AirMouse: return "AIR MOUSE";
    default:             return "SAFE";
  }
}

// 一列一列畫固定寬度的色塊，讓斜紋邊緣落在 LCD 原生像素上。
void stripes(int y, int width, int height) {
  constexpr int stripeWidth = 8, period = stripeWidth * 2;
  for (int row = 0; row < height; ++row) {
    const int phase = row % period;
    for (int left = -phase; left < width; left += period) {
      const int start = left < 0 ? 0 : left;
      const int end = left + stripeWidth < width ? left + stripeWidth : width;
      if (end > start) s_cv.drawFastHLine(start, y + row, end - start, C_YELLOW);
    }
  }
}

void drawKanji(const kanji::Bmp& glyph, int x, int y, int boxWidth, int boxHeight) {
  // 原生 1-bit 字形可容納就直接畫，較寬的詞才等比縮小。
  int width = glyph.w;
  int height = glyph.h;
  if (width > boxWidth) {
    width = boxWidth;
    height = glyph.h * boxWidth / glyph.w;
  }
  if (height > boxHeight) {
    height = boxHeight;
    width = glyph.w * boxHeight / glyph.h;
  }
  x += (boxWidth - width) / 2;
  y += (boxHeight - height) / 2;
  if (width == glyph.w && height == glyph.h) {
    s_cv.drawBitmap(x, y, glyph.bits, glyph.w, glyph.h, C_RED);
    return;
  }
  // 依原始點陣的橫向連續區段縮放，維持明朝體字形的銳利邊緣。
  const int stride = (glyph.w + 7) / 8;
  for (int row = 0; row < height; ++row) {
    const uint8_t* bits = glyph.bits + (row * glyph.h / height) * stride;
    int runStart = -1;
    for (int col = 0; col <= width; ++col) {
      const int srcX = col * glyph.w / width;
      const bool set = col < width && (bits[srcX / 8] & (0x80 >> (srcX % 8)));
      if (set && runStart < 0) runStart = col;
      if (!set && runStart >= 0) {
        s_cv.drawFastHLine(x + runStart, y + row, col - runStart, C_RED);
        runStart = -1;
      }
    }
  }
}

void drawSwitching(int x, int y) {
  s_cv.setFont(&fonts::efontTW_10_b);
  s_cv.setTextColor(C_AMBER);
  s_cv.drawString("切換中…", x, y);
  s_cv.setFont(&fonts::Font0);
}

float progress(uint32_t now) {
  if (!s_mode_ready) return 0.25f;   // 資源還在開關：照實停在 25%
  const float p = 0.25f + 0.75f * (float)(now - s_t_ready) / (float)cfg::UI_MODESWITCH_MS;
  return p > 1.0f ? 1.0f : p;
}

void drawPortrait(int width, int height, const char* number,
                  const char* name, const kanji::Bmp* glyph) {
  stripes(0, width, 73);
  stripes(height - 70, width, 70);
  s_cv.fillRect(0, 73, width, 2, C_AMBER);
  s_cv.fillRect(0, height - 72, width, 2, C_AMBER);

  s_cv.setTextDatum(textdatum_t::top_center);
  s_cv.setTextColor(C_AMBER);
  s_cv.drawString(number, width / 2, 81);
  if (glyph) drawKanji(*glyph, 7, 94, width - 14, 40);

  s_cv.setTextDatum(textdatum_t::top_left);
  char subtitle[24];
  snprintf(subtitle, sizeof subtitle, "%s -", name);
  const int titleWidth = s_cv.textWidth(subtitle);
  s_cv.setFont(&fonts::efontTW_10_b);
  const int statusWidth = s_cv.textWidth("切換中…");
  s_cv.setFont(&fonts::Font0);
  const int titleX = (width - titleWidth - 3 - statusWidth) / 2;
  s_cv.drawString(subtitle, titleX, 139);
  drawSwitching(titleX + titleWidth + 3, 138);
}

void drawLandscape(int width, int height, const char* number,
                   const char* name, const kanji::Bmp* glyph) {
  stripes(0, width, 26);
  stripes(height - 26, width, 26);
  s_cv.fillRect(0, 28, width, 2, C_AMBER);
  s_cv.fillRect(0, height - 30, width, 2, C_AMBER);

  const int infoX = width / 2 + 2;
  if (glyph) drawKanji(*glyph, 6, 39, width / 2 - 13, 52);
  s_cv.setTextDatum(textdatum_t::top_left);
  s_cv.setTextColor(C_AMBER);
  s_cv.drawString(number, infoX, 41);
  s_cv.setTextColor(C_WHITE);
  s_cv.drawString(name, infoX, 55);
  drawSwitching(infoX, 68);
}

void draw(uint32_t now) {
  // 過場在所有目標模式都跟隨目前拿法。
  const Orient orient = ui_expression::orientation();
  const uint8_t rot = orient == Orient::LandALeft ? cfg::ORIENT_ROT_A_LEFT
                    : orient == Orient::LandARight ? cfg::ORIENT_ROT_A_RIGHT : 0;
  const bool redraw = s_drawn_rot != rot;
  if (redraw) s_cv.setRotation(rot);
  const int width = s_cv.width();
  const int height = s_cv.height();
  const bool portrait = width < height;
  constexpr int barHeight = 8, inset = 2;
  const int barX = portrait ? 14 : width / 2 + 2;
  const int barY = portrait ? 152 : 86;
  const int barWidth = width - barX - 14;
  const int fillWidth = (int)((barWidth - 2 * inset) * progress(now));
  if (!redraw && fillWidth == s_filled_pixels) return;

  if (redraw) {
    s_cv.fillScreen(TFT_BLACK);
    s_cv.setFont(&fonts::Font0);
    s_cv.setTextSize(1);
    char number[24];
    snprintf(number, sizeof number, "MODE %02d / 04", (int)s_target + 1);
    const kanji::Bmp* glyph = kanji::find(kanjiKey(s_target));
    if (portrait) drawPortrait(width, height, number, label(s_target), glyph);
    else drawLandscape(width, height, number, label(s_target), glyph);
    s_cv.drawRect(barX, barY, barWidth, barHeight, C_AMBER);
    s_drawn_rot = rot;
    s_filled_pixels = 0;
  }

  s_cv.fillRect(barX + inset + s_filled_pixels, barY + inset,
                fillWidth - s_filled_pixels, barHeight - 2 * inset, C_AMBER);
  s_filled_pixels = fillWidth;
  s_cv.pushSprite(0, 0);
}

}  // namespace

namespace ui_modeswitch {

void begin() {
  s_cv.setColorDepth(16);
  s_cv.setPsram(true);
  s_ready_cv = s_cv.createSprite(M5.Display.width(), M5.Display.height()) != nullptr;
  C_AMBER = s_cv.color565(0xFF, 0x9B, 0x00);
  C_YELLOW = s_cv.color565(0xFF, 0xD0, 0x00);
  C_RED = s_cv.color565(0xFF, 0x29, 0x23);
  C_WHITE = s_cv.color565(0xF2, 0xE8, 0xD8);
  if (!s_ready_cv) Serial.println("[modeswitch] 畫布建立失敗，過場停用");
}

void start(Mode target) {
  if (!s_ready_cv) return;
  ui_fx::cancel();
  s_target = target;
  s_active = true;
  s_mode_ready = false;
  s_drawn_rot = -1;
  s_t0 = millis();
  draw(s_t0);
}

void ready() {
  s_mode_ready = true;
  s_t_ready = millis();
  Serial.printf("[modeswitch] 切換完成 %lu ms\n", (unsigned long)(s_t_ready - s_t0));
}

bool active() { return s_active; }

bool render() {
  if (!s_active) return false;
  const uint32_t now = millis();
  if (s_mode_ready && now - s_t_ready >= cfg::UI_MODESWITCH_MS) {
    s_active = false;
    // 過場結束直接換畫面，避免警戒條與新畫面疊在一起。
    ui_fx::cancel();
    return false;
  }
  draw(now);
  return true;
}

void cancel() { s_active = false; }

M5Canvas* canvas() { return &s_cv; }

}  // namespace ui_modeswitch
