#pragma once
// 模擬標頭：真的 M5GFX 繪圖核心（LGFX_LINUX_FB 編譯），M5Canvas 與真函式庫一樣是 m5gfx::M5Canvas 的別名
#include <Arduino.h>
#define LGFX_USE_V1
#include "lgfx/v1/platforms/device.hpp"
#include "lgfx/v1/platforms/common.hpp"
#include "lgfx/v1/lgfx_filesystem_support.hpp"
#include "lgfx/v1/LGFXBase.hpp"
#include "lgfx/v1/LGFX_Sprite.hpp"
namespace m5gfx {
using namespace lgfx;
class M5Canvas : public lgfx::LGFX_Sprite {
 public:
  M5Canvas() : LGFX_Sprite() {}
  M5Canvas(LovyanGFX* parent) : LGFX_Sprite(parent) { _psram = true; }
  void* frameBuffer(uint8_t) { return getBuffer(); }
};
}  // namespace m5gfx
using M5Canvas = m5gfx::M5Canvas;
struct SimMic { bool isRunning() { return true; } };
struct SimM5 {
  lgfx::LGFX_Sprite Display;
  SimMic Mic;
  SimM5() { Display.setColorDepth(16); Display.createSprite(135, 240); }
};
extern SimM5 M5;
