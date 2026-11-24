// 體感選單模擬：直接 include ui_airmenu.cpp（可用 AIRMENU_SRC 換候選版），設內部狀態後存圖
#include <M5Unified.h>
#include <string>
#include "air_mouse.h"
#include "ble_mouse.h"
#include "input_hal.h"
#include "ui_fx.h"
#include "ui_expression.h"
SimSerial Serial; SimEsp ESP; SimM5 M5;
static uint32_t g_ms = 100000;
uint32_t millis() { return g_ms; }
uint32_t micros() { return g_ms * 1000; }
static GroveDevice g_dev = GroveDevice::None;
static int g_npeer = 0;
static const char* g_names[3] = {"Miles 的 MacBook Pro", "iPad", "iPhone 15 Pro Max（工作用）"};
static std::string g_name;
static std::string g_suffix;
static Orient g_orientation = Orient::Portrait;
namespace ui_expression { Orient orientation() { return g_orientation; } }
namespace air_mouse { void reset(){} void setButton(uint8_t,bool){} void click(uint8_t){} void scroll(int){} void freezeMovement(){} void setClutch(bool,bool){} void takeMotion(int& x,int& y){x=y=0;} }
namespace ble_mouse {
int pairedPeers(Peer (&o)[kMaxPeers]) { for (int i=0;i<g_npeer;++i){ memset(&o[i],0,sizeof o[i]); snprintf(o[i].name,sizeof o[i].name,"%s",g_names[i]); } return g_npeer; }
bool begin(ConnectMode, const Peer*) { return true; } void end() {}
State state() { return State::Advertising; } bool starting() { return false; } bool secured() { return true; }
}
namespace battery_ui { int level() { return 87; } }
namespace input_hal { GroveDevice device() { return g_dev; } bool readScroll(int16_t&, bool&) { return false; } uint32_t bHeldMs() { return 0; } uint32_t bReleaseDuration() { return 0; } }
namespace ui_fx {
void invalidate() {}
void start(m5gfx::M5Canvas*) {}
void present(m5gfx::M5Canvas* cv) {
  const int W = cv->width(), H = cv->height();
  FILE* f = fopen(("air/" + g_name + ".ppm").c_str(), "wb");
  fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
    const uint16_t c = cv->readPixel(x, y);
    const uint8_t px[3] = {(uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31)};
    fwrite(px, 1, 3, f);
  }
  fclose(f);
}
}
#ifndef AIRMENU_SRC
#define AIRMENU_SRC "ui_airmenu.cpp"
#endif
#include AIRMENU_SRC

static void shot(const char* name, Page pg, int npeer, int sel, GroveDevice dev, const char* hint = nullptr, int sec = -1, int exitT = -1) {
  g_name = name + g_suffix; g_npeer = npeer; g_dev = dev;
  s_active = true; s_count = ble_mouse::pairedPeers(s_peers); s_page = pg; s_selected = sel;
  s_hint = hint; s_seconds = sec; s_exit_tenths = exitT; s_dirty = true; s_last_present = 0;
  g_ms += 1000; ui_airmenu::render();
}
static void shots() {
  const auto S = GroveDevice::Scroll, N = GroveDevice::None;
  shot("menu_empty_noscroll", Page::Menu, 0, 0, N);
  shot("menu_2_scroll", Page::Menu, 2, 1, S);
  shot("menu_full_noscroll", Page::Menu, 3, 2, N);
  shot("menu_3_timeout", Page::Menu, 3, 3, S, "連線逾時");
  shot("wait_saved", Page::Waiting, 3, 2, N, nullptr, 23);
  shot("wait_add", Page::Waiting, 1, 1, N, nullptr, 30);
  shot("wait_temp", Page::Waiting, 3, 3, N, nullptr, 8);
  shot("mouse_scroll", Page::Mouse, 3, 0, S);
  shot("mouse_noscroll", Page::Mouse, 3, 2, N);
  s_rpx = 18; s_rpy = -12; s_keys = 1 | 8;
  { g_name = "mouse_press_scroll" + g_suffix; g_dev = S; s_page = Page::Mouse; s_selected = 0; s_dirty = true; s_last_present = 0; g_ms += 1000; ui_airmenu::render(); }
  s_rpx = 0; s_rpy = 0; s_keys = 0;
  shot("exit_1s", Page::Mouse, 3, 0, S, nullptr, -1, 12);
  shot("exit_27", Page::Mouse, 3, 0, S, nullptr, -1, 27);
  shot("menu_disconnected", Page::Menu, 2, 0, S, "連線已中斷");
  shot("menu_stopped", Page::Menu, 1, 0, N, "已斷線");
}

int main() {
  shots();
  g_orientation = Orient::LandALeft;
  g_suffix = "_left";
  shots();
  g_orientation = Orient::LandARight;
  g_suffix = "_right";
  shots();
  puts("done");
}
