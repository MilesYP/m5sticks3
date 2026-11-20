#include "ui_info_eva.h"

#include <M5Unified.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

namespace {

constexpr uint16_t kBlack = 0x0000;
constexpr uint16_t kAmber = 0xFCC0;
constexpr uint16_t kDarkAmber = 0x61C0;
constexpr uint16_t kUnlit = 0x30C0;
constexpr uint16_t kDim = 0xDD6A;
constexpr uint16_t kGreen = 0x47E0;
constexpr uint16_t kYellow = 0xFEE0;
constexpr uint16_t kRed = 0xF923;

void text(M5Canvas& cv, int x, int y, const char* label, uint16_t color) {
  cv.setFont(&fonts::Font0);
  cv.setTextColor(color);
  cv.drawString(label, x, y);
}

void stripes(M5Canvas& cv, int width) {
  constexpr int rows = 4, stripeWidth = 4;
  for (int row = 0; row < rows; ++row) {
    for (int left = -row; left < width; left += 2 * stripeWidth) {
      const int start = left < 0 ? 0 : left;
      const int end = left + stripeWidth < width ? left + stripeWidth : width;
      cv.drawFastHLine(start, row, end - start, kAmber);
    }
  }
}

void digit(M5Canvas& cv, int x, int y, int w, int h, int n, uint16_t color) {
  // a,b,c,d,e,f,g 順序，數字 10 為缺值的橫線。
  constexpr uint8_t mask[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f, 0x40};
  const int t = w >= 12 ? 3 : w >= 7 ? 2 : 1;
  const int mid = h / 2, upper = mid - t, lower = h - mid - 2 * t;
  const int sw = w - 2 * t;
  const int rects[][4] = {
    {t, 0, sw, t}, {w - t, t, t, upper}, {w - t, mid + t, t, lower},
    {t, h - t, sw, t}, {0, mid + t, t, lower}, {0, t, t, upper}, {t, mid, sw, t},
  };
  for (int i = 0; i < 7; ++i) {
    const int* r = rects[i];
    cv.fillRect(x + r[0], y + r[1], r[2], r[3], mask[n] & (1 << i) ? color : kUnlit);
  }
}

void pair(M5Canvas& cv, int x, int y, int w, int h, int value, uint16_t color) {
  digit(cv, x, y, w, h, value < 0 ? 10 : (value / 10) % 10, color);
  digit(cv, x + w + 2, y, w, h, value < 0 ? 10 : value % 10, color);
}

uint16_t batteryColor(int percent) {
  if (percent < 0) return kDim;
  if (percent >= 60) return kGreen;
  return percent >= 20 ? kYellow : kRed;
}

void clock(M5Canvas& cv, int x, bool wide, const ui_info_eva::Status& s) {
  const tm& now = s.localTime;
  const int w = wide ? 15 : 18, h = wide ? 28 : 34;
  constexpr int y = 22;
  const int sep = x + 2 * w + 4;
  pair(cv, x, y, w, h, s.timeSynced ? now.tm_hour : -1, kAmber);
  cv.fillRect(sep + 1, y + h / 3, 3, 3, kAmber);
  cv.fillRect(sep + 1, y + h * 2 / 3, 3, 3, kAmber);
  pair(cv, sep + 7, y, w, h, s.timeSynced ? now.tm_min : -1, kAmber);
  pair(cv, sep + 7 + 2 * w + 7, y + h - 12, 7, 12, s.timeSynced ? now.tm_sec : -1, kAmber);

  constexpr const char* days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  char date[22];
  if (!s.timeSynced) snprintf(date, sizeof date, "--.-- ---  WX --");
  else if (wide) snprintf(date, sizeof date, "%02d.%02d %s  WX --", now.tm_mon + 1, now.tm_mday, days[now.tm_wday]);
  else snprintf(date, sizeof date, "%04d.%02d.%02d %s  WX --", 1900 + now.tm_year, now.tm_mon + 1, now.tm_mday, days[now.tm_wday]);
  text(cv, x, y + h + 4, date, kDim);
}

// efontTW_16_b 的四個原生字形，32×16、每列 MSB first。
constexpr uint8_t kInternalLabel[] = {
  0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x18, 0x7E,
  0x1F, 0x00, 0xFF, 0xA6, 0x03, 0x00, 0x63, 0x66,
  0x7F, 0xFC, 0x36, 0x6C, 0x63, 0x0C, 0x36, 0x6C,
  0x63, 0x8C, 0x34, 0x68, 0x63, 0x8C, 0xFF, 0xAC,
  0x66, 0xCC, 0x00, 0x66, 0x66, 0xCC, 0x00, 0x66,
  0x6C, 0x6C, 0x7F, 0x63, 0x68, 0x34, 0x63, 0x73,
  0x60, 0x0C, 0x63, 0x6B, 0x60, 0x0C, 0x63, 0x6E,
  0x60, 0x3C, 0x7F, 0x60, 0x00, 0x00, 0x00, 0x60,
};
constexpr uint8_t kExternalLabel[] = {
  0x0C, 0x60, 0x18, 0x00, 0x0C, 0x60, 0x18, 0x7E,
  0x0F, 0xA0, 0xFF, 0xA6, 0x19, 0xA0, 0x63, 0x66,
  0x19, 0xA0, 0x36, 0x6C, 0x31, 0xA0, 0x36, 0x6C,
  0x3B, 0x70, 0x34, 0x68, 0x6D, 0x6C, 0xFF, 0xAC,
  0xC7, 0x66, 0x00, 0x66, 0x06, 0x63, 0x00, 0x66,
  0x06, 0x60, 0x7F, 0x63, 0x0C, 0x60, 0x63, 0x73,
  0x0C, 0x60, 0x63, 0x6B, 0x18, 0x60, 0x63, 0x6E,
  0x30, 0x60, 0x7F, 0x60, 0x60, 0x60, 0x00, 0x60,
};

void powerBox(M5Canvas& cv, int x, int y, int w, const uint8_t* label, bool active) {
  constexpr int height = 18;
  if (active) cv.fillRect(x, y, w, height, kGreen);
  else cv.drawRect(x, y, w, height, kAmber);
  cv.drawBitmap(x + (w - 32) / 2, y + 1, label, 32, 16, active ? kBlack : kAmber);
}

void segments(M5Canvas& cv, int x, int y, int w, int h, int count, int level, uint16_t color) {
  const int lit = (constrain(level, 0, 100) * count + 50) / 100;
  for (int i = 0; i < count; ++i) {
    const int left = x + i * w / count;
    cv.fillRect(left, y, x + (i + 1) * w / count - left - 1, h, i < lit ? color : kUnlit);
  }
}

void power(M5Canvas& cv, int x, int y, int w, const ui_info_eva::Status& s) {
  const int box = (w - 3) / 2;
  powerBox(cv, x, y, box, kInternalLabel, !s.externalPower);
  powerBox(cv, x + box + 3, y, w - box - 3, kExternalLabel, s.externalPower);

  const int yy = y + 20;
  const uint16_t color = batteryColor(s.batteryPercent);
  const int barX = x + 45;
  if (s.batteryPercent == 100) {
    digit(cv, x, yy, 10, 20, 1, color);
    pair(cv, x + 11, yy, 10, 20, 0, color);
    text(cv, x + 34, yy + 10, "%", color);
  } else {
    pair(cv, x, yy, 14, 20, s.batteryPercent, color);
    text(cv, x + 32, yy + 10, "%", color);
  }
  segments(cv, barX, yy + 1, x + w - barX, 7, 10, s.batteryPercent, color);
  char detail[20], voltage[10] = "--V", temp[8] = "--C";
  if (s.batteryMillivolts > 0) {
    snprintf(voltage, sizeof voltage, "%d.%02dV", s.batteryMillivolts / 1000, (s.batteryMillivolts % 1000) / 10);
  }
  if (isfinite(s.chipCelsius)) snprintf(temp, sizeof temp, "%dC", (int)lroundf(s.chipCelsius));
  snprintf(detail, sizeof detail, "%s %s", voltage, temp);
  text(cv, barX, yy + 11, detail, kDim);
}

void hexBadge(M5Canvas& cv, int x, int y, int w, const char* label, bool active) {
  constexpr int height = 11, cut = 4;
  for (int row = 0; row < height; ++row) {
    const int fromCenter = row <= height / 2 ? height / 2 - row : row - height / 2;
    const int inset = fromCenter * cut / (height / 2);
    const int left = x + inset, right = x + w - 1 - inset;
    if (active || row == 0 || row == height - 1) {
      cv.drawFastHLine(left, y + row, right - left + 1, kAmber);
    } else {
      cv.drawPixel(left, y + row, kAmber);
      cv.drawPixel(right, y + row, kAmber);
    }
  }
  cv.setFont(&fonts::Font0);
  cv.setTextColor(active ? kBlack : kAmber);
  cv.drawString(label, x + (w - cv.textWidth(label)) / 2, y + 2);
}

void badges(M5Canvas& cv, int x, int y, int width, const ui_info_eva::Status& s) {
  constexpr const char* labels[] = {"WiFi", "MIC", "IMU", "RF", "BLE", "HID"};
  const bool active[] = {s.wifiUp, s.micReady, s.imuReady, false, false, false};
  for (int i = 0; i < 6; ++i) {
    const int col = i % 3;
    const int left = x + col * width / 3;
    hexBadge(cv, left, y + (i / 3) * 12, x + (col + 1) * width / 3 - left - 2, labels[i], active[i]);
  }
}

void chart(M5Canvas& cv, int x, int y, int w, int h, const ui_info_eva::Status& s) {
  cv.drawRect(x, y, w, h, kDarkAmber);
  char label[17];
  snprintf(label, sizeof label, "CHIP %dms", s.lastLoopMs);
  text(cv, x + 3, y + 2, label, kAmber);
  const int left = x + 2, right = x + w - 3, top = y + 11, bot = y + h - 3;
  const int plotSpan = bot - top;
  for (int i = left; i <= right; i += 10) cv.drawFastVLine(i, top, plotSpan + 1, kUnlit);
  const int warning = bot - lroundf(plotSpan * cfg::INFO_LOOP_LIMIT_MS / cfg::INFO_LOOP_FULL_MS);
  for (int i = left; i <= right - 1; i += 4) cv.drawFastHLine(i, warning, 2, kRed);
  int prevX = 0, prevY = 0;
  for (int i = 0; i < s.historyCount; ++i) {
    const int index = (s.historyNext + cfg::INFO_HIST_SEC - s.historyCount + i) % cfg::INFO_HIST_SEC;
    const int px = left + (right - left) * (cfg::INFO_HIST_SEC - s.historyCount + i) / (cfg::INFO_HIST_SEC - 1);
    const float ms = fminf(s.loopHistory[index], cfg::INFO_LOOP_FULL_MS);
    const int py = bot - lroundf(plotSpan * ms / cfg::INFO_LOOP_FULL_MS);
    const uint16_t color = ms > cfg::INFO_LOOP_LIMIT_MS ? kRed : kAmber;
    if (i) cv.drawLine(prevX, prevY, px, py, color);
    else cv.drawPixel(px, py, color);
    prevX = px;
    prevY = py;
  }
}

void memory(M5Canvas& cv, int x, int y, int width, const ui_info_eva::Status& s) {
  const int psram = s.psramTotal ? (uint64_t)s.psramFree * 100 / s.psramTotal : 0;
  const int heap = s.heapTotal ? (uint64_t)s.heapFree * 100 / s.heapTotal : 0;
  text(cv, x, y, "PSRAM", kAmber);
  text(cv, x, y + 12, "HEAP", kAmber);
  segments(cv, x + 32, y, width - 32, 8, 16, psram, kAmber);
  segments(cv, x + 32, y + 12, width - 32, 8, 16, heap, kAmber);
}

void network(M5Canvas& cv, int x, int y, const ui_info_eva::Status& s) {
  char rtt[13] = "RTT --", rssi[12] = "RSSI --", ping[14] = "PING OFF";
  if (s.linkUp && s.rttMs >= 1000) snprintf(rtt, sizeof rtt, "RTT %lus", (unsigned long)(s.rttMs / 1000));
  else if (s.linkUp && s.rttMs) snprintf(rtt, sizeof rtt, "RTT %lums", (unsigned long)s.rttMs);
  if (s.wifiUp) snprintf(rssi, sizeof rssi, "RSSI %d", s.rssi);
  if (s.pingSeconds) snprintf(ping, sizeof ping, "PING %lus", (unsigned long)s.pingSeconds);
  text(cv, x, y, "NET", kAmber);
  cv.fillCircle(x + 21, y + 3, 2, s.linkUp ? kGreen : kRed);
  text(cv, x + 27, y, s.linkUp ? "LINK" : "OFF", s.linkUp ? kGreen : kRed);
  text(cv, x + 57, y, rtt, s.linkUp ? kGreen : kDim);
  text(cv, x, y + 11, rssi, kDim);
  text(cv, x + 59, y + 11, ping, kDim);
}

void news(M5Canvas& cv, int x, int y) {
  cv.setFont(&fonts::efontTW_10_b);
  cv.setTextColor(kDim);
  cv.drawString("快訊 --", x, y);
}

}  // namespace

namespace ui_info_eva {

void draw(M5Canvas& cv, Orient orientation, const Status& s) {
  const int width = cv.width();
  cv.fillScreen(kBlack);
  cv.setTextSize(1);
  cv.setTextDatum(textdatum_t::top_left);
  stripes(cv, width);
  text(cv, 4, 6, "STATUS", kAmber);
  cv.setFont(&fonts::efontTW_10_b);
  cv.setTextColor(kAmber);
  cv.drawString("資訊", 46, 5);
  char pct[7] = "--%";
  if (s.batteryPercent >= 0) snprintf(pct, sizeof pct, "%d%%", s.batteryPercent);
  text(cv, width - 4 - strlen(pct) * 6, 6, pct, batteryColor(s.batteryPercent));
  cv.drawFastHLine(0, 15, width, kAmber);

  if (orientation == Orient::Portrait) {
    clock(cv, 6, false, s);
    power(cv, 4, 73, 127, s);
    badges(cv, 4, 125, 127, s);
    chart(cv, 4, 150, 127, 34, s);
    memory(cv, 4, 190, 127, s);
    network(cv, 4, 215, s);
    return;
  }

  const bool aLeft = orientation == Orient::LandALeft;
  const int dashboard = aLeft ? 123 : 4;
  const int telemetry = aLeft ? 4 : 123;
  cv.drawFastVLine(119, 19, 112, kAmber);
  clock(cv, dashboard, true, s);
  power(cv, dashboard, 64, 112, s);
  network(cv, dashboard, 109, s);
  badges(cv, telemetry, 20, 114, s);
  chart(cv, telemetry, 44, 114, 40, s);
  memory(cv, telemetry, 90, 114, s);
  news(cv, telemetry, 118);
}

}  // namespace ui_info_eva
