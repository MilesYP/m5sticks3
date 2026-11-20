#include "ui_info_eva.h"

#include <M5Unified.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "info_data.h"
#include "ui_wxicon.h"

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

// A5-2：英數用 Font0、中文（UTF-8 非 ASCII）用 efontTW_10（細體；粗體 10px 中文會糊），逐段換字型；回傳寬度（draw=false 只量寬）
int mixed(M5Canvas& cv, int x, int y, const char* s, uint16_t color, bool draw = true) {
  cv.setTextColor(color);
  int w = 0;
  char run[96];
  for (const char* p = s; *p;) {
    const bool cjk = (uint8_t)*p >= 0x80;
    size_t n = 0;
    while (p[n] && (((uint8_t)p[n] >= 0x80) == cjk) && n < sizeof(run) - 1) { run[n] = p[n]; ++n; }
    while (n > 0 && p[n] && ((uint8_t)p[n] & 0xC0) == 0x80) --n;
    if (n == 0) n = 1;
    run[n] = '\0';
    cv.setFont(cjk ? (const lgfx::IFont*)&fonts::efontTW_10 : (const lgfx::IFont*)&fonts::Font0);
    const int rw = cv.textWidth(run);
    if (draw) cv.drawString(run, x + w, cjk ? y - 1 : y);
    w += rw;
    p += n;
  }
  return w;
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
  if (!s.timeSynced) snprintf(date, sizeof date, "--.-- ---");
  else if (wide) snprintf(date, sizeof date, "%02d.%02d %s", now.tm_mon + 1, now.tm_mday, days[now.tm_wday]);
  else snprintf(date, sizeof date, "%04d.%02d.%02d %s", 1900 + now.tm_year, now.tm_mon + 1, now.tm_mday, days[now.tm_wday]);
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

// A5-2：記憶體一行版（PSRAM、HEAP 兩條並排），騰出位置給天氣、額度、快訊
void memoryRow(M5Canvas& cv, int x, int y, int width, const ui_info_eva::Status& s) {
  const int psram = s.psramTotal ? (uint64_t)s.psramFree * 100 / s.psramTotal : 0;
  const int heap = s.heapTotal ? (uint64_t)s.heapFree * 100 / s.heapTotal : 0;
  const int half = width / 2;
  text(cv, x, y, "PS", kAmber);
  segments(cv, x + 14, y, half - 18, 7, 8, psram, kAmber);
  text(cv, x + half, y, "HP", kAmber);
  segments(cv, x + half + 14, y, width - half - 14, 7, 8, heap, kAmber);
}

// A5-2：連線一行版：NET ●LINK RTT（沒連上時改顯示 RSSI）
void networkRow(M5Canvas& cv, int x, int y, const ui_info_eva::Status& s) {
  char rtt[16] = "RTT --";
  if (s.linkUp && s.rttMs >= 1000) snprintf(rtt, sizeof rtt, "RTT %lus", (unsigned long)(s.rttMs / 1000));
  else if (s.linkUp && s.rttMs) snprintf(rtt, sizeof rtt, "RTT %lums", (unsigned long)s.rttMs);
  else if (!s.linkUp && s.wifiUp) snprintf(rtt, sizeof rtt, "RSSI %d", s.rssi);
  text(cv, x, y, "NET", kAmber);
  cv.fillCircle(x + 21, y + 3, 2, s.linkUp ? kGreen : kRed);
  text(cv, x + 27, y, s.linkUp ? "LINK" : "OFF", s.linkUp ? kGreen : kRed);
  text(cv, x + 57, y, rtt, s.linkUp ? kGreen : kDim);
}

// A5-2：天氣一行：圖示＋溫度＋描述（截字）＋降雨機率；中控斷線時降雨機率換成資料時間
void weatherRow(M5Canvas& cv, int x, int y, int width, const ui_info_eva::Status& s) {
  const info_data::Weather& w = info_data::weather();
  ui_wxicon::draw(cv, x + 5, y + 3, 5, w.valid ? w.icon : info_data::WxIcon::Cloud, kAmber, w.valid ? kAmber : kUnlit, kAmber);
  if (!w.valid) { text(cv, x + 14, y, "WX --", kDim); return; }
  char tb[12] = "--", tail[12];
  if (w.temp > -100) snprintf(tb, sizeof tb, "%dC", w.temp);
  if (!s.linkUp) info_data::formatAt(tail, sizeof tail, w.at);
  else if (w.pop >= 0) snprintf(tail, sizeof tail, "%d%%", w.pop);
  else strcpy(tail, "");
  int xx = x + 14;
  xx += mixed(cv, xx, y, tb, kAmber) + 6;
  const bool umb = s.linkUp && w.pop >= 0;          // 降雨機率前面畫雨傘（斷線時這格是資料時間）
  const int tw = mixed(cv, 0, y, tail, kDim, false) + (umb ? 9 : 0);
  const int room = x + width - tw - 6 - xx;
  char d[40];
  info_data::utf8Prefix(d, sizeof d, w.desc, room > 0 ? room / 12 : 0);
  cv.setFont(&fonts::efontTW_12);   // 11-20 Miles：10px 中文太擠 → 描述用 12px
  cv.setTextColor(kDim);
  cv.drawString(d, xx, y - 2);
  const uint16_t pc = s.linkUp && w.pop >= 50 ? kYellow : kDim;
  if (umb) ui_wxicon::umbrella(cv, x + width - tw, y, pc);
  mixed(cv, x + width - tw + (umb ? 9 : 0), y, tail, pc);
}

// A5-2：語音額度一行「GPT $0.42/2 餘8.37」；今日花費依比例綠／黃／紅，中控斷線時變暗
void usdRow(M5Canvas& cv, int x, int y, const ui_info_eva::Status& s) {
  const info_data::Usd& u = info_data::usd();
  int xx = x + mixed(cv, x, y, "GPT ", kAmber);
  char line[40];
  info_data::formatUsd(line, sizeof line, u, false);
  if (!u.valid) { mixed(cv, xx, y, "-", kDim); return; }
  const int lv = info_data::usdLevel();
  const uint16_t tc = !s.linkUp ? kDim : lv == 2 ? kRed : lv == 1 ? kYellow : kGreen;
  const size_t cut = strcspn(line, "/");
  char head[16];
  snprintf(head, sizeof head, "%.*s", (int)cut, line);
  xx += mixed(cv, xx, y, head, tc);
  mixed(cv, xx, y, line + cut, kDim);
}

// A5-2：快訊 rows 行（每 cfg::INFO_NEWS_PAGE_SEC 秒換一批）；中控斷線時第 1 行改「快訊 18:30」
void newsRows(M5Canvas& cv, int x, int y, int width, int rows, const ui_info_eva::Status& s) {
  const info_data::News& nw = info_data::news();
  if (nw.n <= 0) { mixed(cv, x, y, "快訊 --", kDim); return; }
  int k0 = 0;
  if (!s.linkUp) {
    char at[8], h[24];
    info_data::formatAt(at, sizeof at, nw.at);
    snprintf(h, sizeof h, "快訊 %s", at);
    mixed(cv, x, y, h, kAmber);
    k0 = 1;
  }
  const int per = rows - k0 > 0 ? rows - k0 : 1;
  const int pages = (nw.n + per - 1) / per;
  const long now = s.timeSynced ? (long)time(nullptr) : (long)(millis() / 1000);
  const int page = (int)((now / cfg::INFO_NEWS_PAGE_SEC) % pages);
  const int chars = width / 10;
  for (int k = 0; k + k0 < rows; ++k) {
    const int i = page * per + k;
    if (i >= nw.n) break;
    char line[80];
    const size_t n = info_data::utf8Prefix(line, sizeof line, nw.item[i], chars);
    if (nw.item[i][n]) {   // 截斷 → 「…」
      const size_t m = info_data::utf8Prefix(line, sizeof line - 3, nw.item[i], chars - 1);
      snprintf(line + m, sizeof line - m, "…");
    }
    mixed(cv, x, y + (k + k0) * 11, line, k == 0 && k0 == 0 ? kAmber : kDim);
  }
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
    // A5-2：CHART 34→20px、記憶體與連線各併成一行，騰出天氣、語音額度、快訊各一行
    clock(cv, 6, false, s);
    power(cv, 4, 73, 127, s);
    badges(cv, 4, 125, 127, s);
    chart(cv, 4, 150, 127, 20, s);
    weatherRow(cv, 4, 174, 127, s);
    usdRow(cv, 4, 186, s);
    memoryRow(cv, 4, 198, 127, s);
    networkRow(cv, 4, 210, s);
    cv.drawFastHLine(4, 220, 127, kDarkAmber);
    newsRows(cv, 4, 225, 127, 1, s);
    return;
  }

  const bool aLeft = orientation == Orient::LandALeft;
  const int dashboard = aLeft ? 123 : 4;
  const int telemetry = aLeft ? 4 : 123;
  cv.drawFastVLine(119, 19, 112, kAmber);
  // A5-2：儀表欄時鐘下面加語音額度、連線併成一行；數據欄 CHART 40→28、加天氣一行、快訊兩行
  clock(cv, dashboard, true, s);
  usdRow(cv, dashboard, 64, s);
  power(cv, dashboard, 75, 112, s);
  networkRow(cv, dashboard, 122, s);
  badges(cv, telemetry, 20, 114, s);
  chart(cv, telemetry, 44, 114, 28, s);
  weatherRow(cv, telemetry, 76, 114, s);
  memory(cv, telemetry, 89, 114, s);
  newsRows(cv, telemetry, 113, 114, 2, s);
}

}  // namespace ui_info_eva
