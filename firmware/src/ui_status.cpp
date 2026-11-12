#include "ui_status.h"

#include <M5Unified.h>

#include "config.h"
#include "input_hal.h"
#include "killswitch.h"
#include "mode_manager.h"

namespace {

// 全螢幕畫布（sprite = 先畫在記憶體裡再一次推上螢幕，避免閃爍）
M5Canvas s_canvas(&M5.Display);
uint32_t s_last_render_ms = 0;

void drawNormal() {
  auto& c = s_canvas;
  c.fillScreen(TFT_BLACK);
  c.setTextColor(TFT_DARKGREY);
  c.setTextSize(1);
  c.drawString("M0 skeleton", 4, 4);

  c.drawString("MODE", 4, 24);
  c.setTextColor(TFT_CYAN);
  c.setTextSize(2);
  c.drawString(mode_manager::currentName(), 4, 36);

  c.setTextSize(1);
  c.setTextColor(TFT_DARKGREY);
  c.drawString("INPUT (Grove)", 4, 68);
  c.setTextColor(TFT_WHITE);
  c.setTextSize(2);
  c.drawString(input_hal::deviceName(), 4, 80);

  c.setTextSize(1);
  c.setTextColor(TFT_DARKGREY);
  c.drawString("KILL SWITCH", 4, 112);
  const bool armed = killswitch::armedByBtnA();
  c.setTextColor(armed ? TFT_ORANGE : TFT_GREEN);
  c.setTextSize(2);
  c.drawString(armed ? "A=ARMED" : "OK", 4, 124);

  c.setTextSize(1);
  c.setTextColor(TFT_DARKGREY);
  c.drawString("B     : next mode", 4, 176);
  c.drawString("B 1-3s: serial report", 4, 188);
  c.drawString("B 3s  : KILL", 4, 200);
  c.drawString(armed ? "A     : KILL" : "A     : (no action)", 4, 212);
  c.drawString(String("up ") + (millis() / 1000) + "s", 4, 228);
}

void drawKilled() {
  auto& c = s_canvas;
  c.fillScreen(TFT_RED);
  c.setTextColor(TFT_WHITE);
  c.setTextSize(3);
  c.drawString("KILLED", 14, 40);

  c.setTextSize(1);
  c.drawString("all output stopped", 4, 90);
  c.drawString(String("src : ") + killswitch::sourceName(), 4, 110);
  c.drawString(String("from: ") + mode_manager::name(mode_manager::previous()), 4, 124);
  c.drawString(String("gate: ") + killswitch::lastLatencyUs() + " us", 4, 138);

  c.setTextSize(2);
  c.drawString("B: resume", 4, 190);
}

}  // namespace

namespace ui_status {

void begin() {
  M5.Display.setRotation(0);   // 直式 135x240
  s_canvas.createSprite(M5.Display.width(), M5.Display.height());
  render(true);
}

void render(bool force) {
  const uint32_t now = millis();
  if (!force && now - s_last_render_ms < cfg::UI_REFRESH_MS) return;
  s_last_render_ms = now;

  if (mode_manager::current() == Mode::Safe) drawKilled();
  else drawNormal();
  s_canvas.pushSprite(0, 0);
}

void printReport() {
  Serial.println("===== M0 status report =====");
  Serial.printf("uptime   : %lu ms\n", (unsigned long)millis());
  Serial.printf("mode     : %s (previous: %s)\n", mode_manager::currentName(),
                mode_manager::name(mode_manager::previous()));
  Serial.printf("grove    : %s  [scan: %s]\n", input_hal::deviceName(), input_hal::scanLine());
  Serial.printf("kill     : %s  source=%s  gate=%s  A-armed=%s  last-latency=%ld us\n",
                killswitch::isTriggered() ? "TRIGGERED" : "idle", killswitch::sourceName(),
                killswitch::outputAllowed() ? "open" : "CLOSED",
                killswitch::armedByBtnA() ? "yes" : "no", (long)killswitch::lastLatencyUs());
  Serial.printf("heap     : %u free / psram %u free\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getFreePsram());
  Serial.println("============================");
}

}  // namespace ui_status
