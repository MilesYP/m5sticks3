// ─────────────────────────────────────────────────────────────────────────
//  info_data：資訊頁的雲端資料（M1-A / A5-2）——天氣、快訊、語音額度（美金）
//
//  中控（cloud/src/info.js）在裝置連上、每輪對話結束、每 30 分鐘、換日時送
//  {"t":"info","k":"usd|wx|nw",…}；net_hub 在主迴圈解析後呼叫這裡存起來。
//  只存資料，不重畫、不叫醒裝置（資訊頁下一次重畫時才顯示，鐵則十）。
//  斷線時保留上次的值；畫面用 at（資料時間）標示。q0（ui_info）與 q1（ui_info_eva）共用。
// ─────────────────────────────────────────────────────────────────────────
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>

namespace info_data {

enum class WxIcon : uint8_t { Sun, Part, Cloud, Rain, Storm };

struct Usd {
  bool valid = false;
  float today = 0, cap = 0, left = -1;   // left < 0＝沒有餘額資料
  time_t at = 0;
};
struct Weather {
  bool valid = false;
  WxIcon icon = WxIcon::Cloud;
  int temp = -999, rh = -1, pop = -1;    // 沒有＝-999／-1
  char loc[16] = "";                     // UTF-8，例「中正」
  char desc[52] = "";                    // UTF-8，例「多雲短暫陣雨」
  time_t at = 0;
};
constexpr int NEWS_MAX = 6;
struct News {
  int n = 0;
  char item[NEWS_MAX][112] = {};         // UTF-8，中控已截到 36 字
  time_t at = 0;
};

// net_hub 收到 info 訊息時呼叫（主迴圈）
void onUsd(float today, float cap, float left, time_t at);
void onWeather(const char* icon, int temp, int rh, int pop, const char* loc, const char* desc, time_t at);
void onNewsItem(int i, int n, const char* s, time_t at);

const Usd& usd();
const Weather& weather();
const News& news();
uint32_t version();            // 每收到一則就 +1（畫面可用來判斷要不要重畫，不強制）

// 今日花費用掉上限的比例分級：0＝<70% 綠、1＝≥70% 黃、2＝≥90% 紅
int usdLevel();
// 格式化（純函式，模擬測試用）
//   直式短版：$0.42/2 餘8.37（上限只寫整數、餘額省 $）；橫式長版：$0.42/$2.00 餘$8.37
void formatUsd(char* out, size_t cap, const Usd& u, bool longForm);
// 「HH:MM」資料時間（台北）；沒資料回 "--:--"
void formatAt(char* out, size_t cap, time_t at);
// 從 UTF-8 字串取前 maxChars 個字（中文算 1 字），回傳寫入的 bytes
size_t utf8Prefix(char* out, size_t cap, const char* s, int maxChars);

}  // namespace info_data
