#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "ui_expression.h"

namespace ui_info_eva {

struct Status {
  const tm& localTime;
  bool timeSynced;
  int batteryPercent;
  int batteryMillivolts;
  bool externalPower;
  float chipCelsius;
  bool wifiUp;
  bool linkUp;
  int rssi;
  uint32_t rttMs;
  uint32_t pingSeconds;
  bool micReady;
  bool imuReady;
  size_t psramFree;
  size_t psramTotal;
  size_t heapFree;
  size_t heapTotal;
  const uint8_t* loopHistory;
  int historyCount;
  int historyNext;
  int lastLoopMs;
};

void draw(m5gfx::M5Canvas& canvas, Orient orientation, const Status& status);

}
