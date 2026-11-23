#include "ui_modeswitch.h"

#include <M5Unified.h>
#include <stdio.h>

#include "config.h"
#include "kanji_bitmaps.h"
#include "ui_expression.h"
#include "ui_fx.h"
#include "ui_kit.h"

namespace {

M5Canvas s_cv(&M5.Display);
bool s_ready_cv = false;
bool s_active = false;
bool s_mode_ready = false;
Mode s_target = Mode::Desktop;
uint32_t s_t0 = 0, s_t_ready = 0;
int s_drawn_rot = -1;
int s_filled_pixels = 0;
// 31-m4f：enter() 回來後還要等新模式資源（mode_manager::currentReady）才結束過場
bool s_res_ready = false;
uint32_t s_t_enter = 0;        // ready()（enter 回來）的時刻
float s_p_at_ready = 0.25f;    // 資源好了那一刻進度條在哪，從這裡跑完
const char* s_step = "切換中…";
const char* s_drawn_step = nullptr;

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

// 黃黑斜紋與點陣大字都在 ui_kit（docs/design/COMPONENTS.md C4／C4b）
void stripes(int y, int width, int height) { ui_kit::stripes(s_cv, 0, y, width, height, C_YELLOW, 8, 16, 0); }

void drawKanji(const kanji::Bmp& glyph, int x, int y, int boxWidth, int boxHeight) {
  ui_kit::glyphFit(s_cv, glyph.bits, glyph.w, glyph.h, x, y, boxWidth, boxHeight, C_RED);
}

void drawSwitching(int x, int y) {
  s_cv.setFont(&fonts::efontTW_10_b);
  s_cv.setTextColor(C_AMBER);
  s_cv.drawString(s_step, x, y);
  s_cv.setFont(&fonts::Font0);
}

float waitProgress(uint32_t now) {   // 等資源：25% 起慢慢爬，最多到 80%（照實：好了才跑完）
  if (!s_mode_ready) return 0.25f;
  float t = (float)(now - s_t_enter) / (float)cfg::UI_MODESWITCH_WAIT_MAX_MS;
  if (t > 1.0f) t = 1.0f;
  return 0.25f + 0.55f * (1.0f - (1.0f - t) * (1.0f - t));   // 先快後慢
}

float progress(uint32_t now) {
  if (!s_res_ready) return waitProgress(now);
  const float p = s_p_at_ready + (1.0f - s_p_at_ready) * (float)(now - s_t_ready) / (float)cfg::UI_MODESWITCH_MS;
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
  const int statusWidth = s_cv.textWidth(s_step);
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
  const bool redraw = s_drawn_rot != rot || s_drawn_step != s_step;   // 狀態字變了也整張重畫
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
    s_drawn_step = s_step;
    s_filled_pixels = 0;
  }

  s_cv.fillRect(barX + inset + s_filled_pixels, barY + inset,
                fillWidth - s_filled_pixels, barHeight - 2 * inset, C_AMBER);
  s_filled_pixels = fillWidth;
  s_cv.pushSprite(0, 0);
  ui_fx::invalidate();   // 11-21：螢幕被這頁蓋掉，表情／資訊頁下次整張送
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
  s_res_ready = false;
  s_step = "切換中…";
  s_drawn_rot = -1;
  s_t0 = millis();
  draw(s_t0);
}

void ready() {
  s_mode_ready = true;
  s_t_enter = s_t_ready = millis();
  Serial.printf("[modeswitch] 切換完成 %lu ms（等新模式資源…）\n", (unsigned long)(s_t_enter - s_t0));
}

bool active() { return s_active; }

bool render() {
  if (!s_active) return false;
  const uint32_t now = millis();
  if (s_mode_ready && !s_res_ready) {   // 31-m4f：等新模式資源（Wi-Fi＋中控、藍牙）
    const char* step = "切換中…";
    const bool ok = mode_manager::currentReady(&step);
    const bool timeout = now - s_t_enter >= cfg::UI_MODESWITCH_WAIT_MAX_MS;
    if (ok || timeout) {
      s_p_at_ready = waitProgress(now);
      s_res_ready = true;
      s_t_ready = now;
      s_step = "切換中…";
      Serial.printf("[modeswitch] %s（等了 %lu ms）\n", ok ? "資源就緒" : "等太久，先進去（斷線狀態）",
                    (unsigned long)(now - s_t_enter));
    } else {
      s_step = step;
    }
  }
  if (s_res_ready && now - s_t_ready >= cfg::UI_MODESWITCH_MS) {
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
