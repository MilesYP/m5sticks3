#include "info_data.h"

#include <stdio.h>
#include <string.h>

namespace info_data {
namespace {
Usd s_usd;
Weather s_wx;
News s_news;
News s_newsIn;      // 收集中（一批 n 則收齊才換上，避免畫到一半新一半舊）
uint32_t s_ver = 0;

void copyStr(char* dst, size_t cap, const char* src) {
  if (!cap) return;
  size_t n = utf8Prefix(dst, cap, src ? src : "", 1000);
  dst[n] = '\0';
}
}  // namespace

size_t utf8Prefix(char* out, size_t cap, const char* s, int maxChars) {
  if (!cap) return 0;
  size_t i = 0;
  int chars = 0;
  while (s[i] && chars < maxChars) {
    const unsigned char c = (unsigned char)s[i];
    const size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    if (i + len >= cap) break;
    bool ok = true;
    for (size_t k = 1; k < len; ++k) if (!s[i + k]) ok = false;
    if (!ok) break;
    memcpy(out + i, s + i, len);
    i += len;
    ++chars;
  }
  out[i] = '\0';
  return i;
}

void onUsd(float today, float cap, float left, time_t at) {
  s_usd.valid = true;
  s_usd.today = today;
  s_usd.cap = cap;
  s_usd.left = left;
  s_usd.at = at;
  ++s_ver;
}

void onWeather(const char* icon, int temp, int rh, int pop, const char* loc, const char* desc, time_t at) {
  WxIcon ic = WxIcon::Cloud;
  if (icon) {
    if (!strcmp(icon, "sun")) ic = WxIcon::Sun;
    else if (!strcmp(icon, "part")) ic = WxIcon::Part;
    else if (!strcmp(icon, "rain")) ic = WxIcon::Rain;
    else if (!strcmp(icon, "storm")) ic = WxIcon::Storm;
  }
  s_wx.valid = true;
  s_wx.icon = ic;
  s_wx.temp = temp;
  s_wx.rh = rh;
  s_wx.pop = pop;
  copyStr(s_wx.loc, sizeof s_wx.loc, loc);
  copyStr(s_wx.desc, sizeof s_wx.desc, desc);
  s_wx.at = at;
  ++s_ver;
}

void onNewsItem(int i, int n, const char* s, time_t at) {
  if (n < 1 || i < 0 || i >= NEWS_MAX) return;
  if (n > NEWS_MAX) n = NEWS_MAX;
  if (i == 0 || s_newsIn.at != at || s_newsIn.n != n) {   // 新的一批
    s_newsIn = News();
    s_newsIn.n = n;
    s_newsIn.at = at;
  }
  copyStr(s_newsIn.item[i], sizeof s_newsIn.item[i], s);
  if (i == n - 1) {
    s_news = s_newsIn;
    ++s_ver;
  }
}

const Usd& usd() { return s_usd; }
const Weather& weather() { return s_wx; }
const News& news() { return s_news; }
uint32_t version() { return s_ver; }

int usdLevel() {
  if (!s_usd.valid || s_usd.cap <= 0) return 0;
  const float r = s_usd.today / s_usd.cap;
  return r >= 0.9f ? 2 : r >= 0.7f ? 1 : 0;
}

void formatUsd(char* out, size_t cap, const Usd& u, bool longForm) {
  if (!u.valid) { snprintf(out, cap, "GPT -"); return; }
  char left[16] = "";
  if (u.left >= 0) {
    if (longForm) snprintf(left, sizeof left, " 餘$%.2f", u.left);
    else snprintf(left, sizeof left, " 餘%.2f", u.left);
  }
  if (longForm) snprintf(out, cap, "$%.2f/$%.2f%s", u.today, u.cap, left);
  else snprintf(out, cap, "$%.2f/%d%s", u.today, (int)(u.cap + 0.5f), left);
}

void formatAt(char* out, size_t cap, time_t at) {
  if (at <= 0) { snprintf(out, cap, "--:--"); return; }
  struct tm lt;
  const time_t t = at + 8 * 3600;   // 台北（不依賴 TZ 設定，模擬環境也一樣）
  gmtime_r(&t, &lt);
  snprintf(out, cap, "%02d:%02d", lt.tm_hour, lt.tm_min);
}

}  // namespace info_data
