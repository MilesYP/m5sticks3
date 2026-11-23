#include "ui_expression.h"

#include <M5Unified.h>
#include <math.h>
#include <string.h>

#include "battery_ui.h"
#include "config.h"
#include "power_key.h"
#include "ui_fx.h"
#include "ui_kit.h"
#include "ui_labels.h"
#include <cstring>   // A7：狀態大字點陣圖（不連結整套 efontTW_24，見 tools/gen_kanji.py）

// ─────────────────────────────────────────────────────────────────────────
//  座標約定：表情用「設計單位」描述（直式 135×240 時 1 單位＝1px），相對於臉的中心點；
//  畫的時候乘上比例 sc（直式 1.0、橫式 0.8）再加上臉中心的像素座標。
//  A7（11-20，Miles 參考圖定案）：像素臉——黑底六角格、狀態列「FRIDAY・狀態」＋電量 %、取景角、左上狀態大字、
//   掃描線眼睛與嘴巴（每 3 列空 1 列）、五官與符號一律淡琥珀（警報時框格變紅）、右側強度方格、對話框兩版都用 EVA「受信」版。
//   原本合成波畫法保留在 PIXEL=false 的分支（對照用）。11-21 Miles：EVA 角色臉（ui_persona）刪除，只剩這一個表情頁。
//   11-21（27-a7f）：表情符號改六角徽章（見「A7 六角符號」）：驚嚇＝水滴、生氣＝! WARNING、其他「!」單純驚嘆號；出場閃三下＋彈簧＋外框疊影；眼角汗滴拿掉。
//  畫布用 M5Canvas::setRotation 轉向（只換畫圖座標，緩衝區仍是 135×240，推上螢幕不變）。
// ─────────────────────────────────────────────────────────────────────────

namespace {

M5Canvas s_cv(&M5.Display);
bool s_ready = false;

// ── 合成波色盤（A4，Miles 11-20 選方案 3）：每種表情上下兩色（眼睛漸層、嘴巴用下色）──
struct MoodInfo {
  const char* name;
  uint8_t r1, g1, b1;   // 上色（眼睛上緣）
  uint8_t r2, g2, b2;   // 下色（眼睛下緣、嘴巴、光暈）
  float grid;           // 地平線網格捲動速度（設計單位／秒）
};
// 順序必須與 enum class Mood 一致
const MoodInfo kMoods[] = {
  {"Idle",      0x00, 0xF0, 0xFF, 0x9D, 0x5C, 0xFF,  5},
  {"Listening", 0x39, 0xFF, 0x88, 0x00, 0xF0, 0xFF, 12},
  {"Thinking",  0x9D, 0x5C, 0xFF, 0xFF, 0x2B, 0xD6,  7},
  {"Speaking",  0xF2, 0xF6, 0xFF, 0x00, 0xF0, 0xFF, 17},
  {"Happy",     0xFF, 0xE6, 0x00, 0xFF, 0x7A, 0x1A, 24},
  {"Angry",     0xFF, 0x2E, 0x4D, 0xFF, 0x2B, 0xD6, 29},
  {"Scared",    0xFF, 0x2B, 0xD6, 0x9D, 0x5C, 0xFF, 31},
  {"Sleepy",    0x2B, 0xB3, 0xA8, 0x3A, 0x3F, 0x9E,  2},
  {"Asleep",    0x3A, 0x3F, 0x9E, 0x26, 0x22, 0x66,  0},
};
static_assert(sizeof(kMoods) / sizeof(kMoods[0]) == static_cast<size_t>(Mood::Asleep) + 1,
              "kMoods 要和 enum class Mood 一一對應");

constexpr uint8_t BG_R = 0x00, BG_G = 0x00, BG_B = 0x00;   // A7：黑底
constexpr uint8_t MG_R = 0xFF, MG_G = 0x2B, MG_B = 0xD6;   // 洋紅：網格、舌頭、符號
constexpr uint8_t OFF_R = 95, OFF_G = 94, OFF_B = 90;      // 斷線灰
constexpr uint8_t BLINK_RED_R = 226, BLINK_RED_G = 75, BLINK_RED_B = 74;

struct Rgb { float r, g, b; };
inline uint16_t c565(float r, float g, float b) {
  auto cl = [](float v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); };
  return s_cv.color565(cl(r), cl(g), cl(b));
}
inline uint16_t c565(const Rgb& c) { return c565(c.r, c.g, c.b); }
inline Rgb mixc(const Rgb& a, const Rgb& b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
const Rgb BG = {BG_R, BG_G, BG_B};
const Rgb MG = {MG_R, MG_G, MG_B};
inline uint16_t fade(const Rgb& c, float a) { return c565(mixc(BG, c, a < 0 ? 0 : a > 1 ? 1 : a)); }

float rnd(float a, float b) { return a + (esp_random() / 4294967296.0f) * (b - a); }
// 決定性亂數（模擬音節用）
float hash01(int32_t n) {
  const float s = sinf(n * 127.1f + 311.7f) * 43758.5453f;
  return s - floorf(s);
}

// ── 彈簧動畫：每個會動的量都有位置 p、速度 v、目標 t；阻尼不足 → 衝過頭再彈回 ──
struct Spr { float p, v, t; };
enum Param { K_HX, K_HY, K_LX, K_LY, K_LW, K_LH, K_RX, K_RY, K_RW, K_RH, K_DX, K_DY, K_ANG, K_SLP, K_HAP, K_BRA, K_BRL, K_BRR, K_MW, K_MH, K_MC, K_SK, K_ED, K_NP };
const float kDefault[K_NP] = {0, 0, -28, 0, 32, 46, 28, 0, 32, 46, 0, 0, 0, 0, 0, 0, 0, 0, 24, 6, 0.6f, 0, -45};
Spr s_sp[K_NP];
inline float P(Param i) { return s_sp[i].p; }
void springStep(Spr& s, float dt, float k, float c) {
  const float a = k * (s.t - s.p) - c * s.v;
  s.v += a * dt;
  s.p += s.v * dt;
}

enum class MouthMode : uint8_t { Line, U, Smirk, O, Ring, Open, OFill, Wave, Zig };

// ── 狀態 ──
Mood     s_mood = Mood::Idle;        // 邏輯表情
int      s_preview = -1;             // y 指令預覽
Mood     s_shown = Mood::Idle;       // 畫面上的表情
uint32_t s_t0 = 0;                   // 這個表情開始的時刻（動畫時間）
uint32_t s_last_frame_ms = 0;
float    s_speak_level = -1.0f;
float    s_listen_level = -1.0f;
float    s_prev_v = 0;
float    s_quiet = 0;                // 連續沒聲音幾秒（聆聽／說話）
char     s_vowel = 'A';
float    s_grid_off = 0;

struct Ev {   // 每個表情自己的計時與旗標（切表情時歸零）
  float n = 0, dx_until = 0, dx = 0, grin = 0, brow = 0, x2 = 0, d = 0;
  bool paused = false, snap = false, yawn = false, popped = false;
};
Ev s_ev;

// 眨眼
uint32_t s_blink_end = 0, s_next_blink = 1500, s_wink_end = 0;

// 斷線
bool     s_offline = false;
uint32_t s_freeze_ms = 0;

// 休息臉電量色／低電量閃爍／看方向
bool  s_tint_on = false;
float s_tint_r = 0, s_tint_g = 0, s_tint_b = 0;
int   s_low_blink = 0;   // 0 不閃、1 原色紅閃、2 紅灰閃
// 低電量閃爍色：<20% 原本顏色↔紅（11-21 Miles：黃色不好看，用原本的琥珀）、<5% 紅↔灰（半週期 BAT_BLINK_MS）
Rgb lowBlinkColor(const Rgb& base) {
  const bool ph = ((millis() / cfg::BAT_BLINK_MS) % 2) == 0;
  if (s_low_blink >= 2) return ph ? Rgb{BLINK_RED_R, BLINK_RED_G, BLINK_RED_B} : Rgb{OFF_R, OFF_G, OFF_B};
  return ph ? base : Rgb{BLINK_RED_R, BLINK_RED_G, BLINK_RED_B};
}
float s_gaze_tx = 0, s_gaze_ty = 0, s_gaze_x = 0, s_gaze_y = 0;
constexpr float GAZE_EASE_PER_MS = 0.006f;
Rgb   s_c1 = {0x00, 0xF0, 0xFF}, s_c2 = {0x9D, 0x5C, 0xFF};   // 目前畫面上的顏色（平滑過渡）

// 拿法
Orient s_orient = Orient::Portrait;

// ── A4 驚嚇彈珠台：0 左眼、1 右眼、2 嘴巴；位移 o／速度 v 是設計單位，sx／sy 是撞牆壓扁倍率 ──
struct Body { float ox, oy, vx, vy, sx, sy; };
Body     s_bd[3] = {{0, 0, 0, 0, 1, 1}, {0, 0, 0, 0, 1, 1}, {0, 0, 0, 0, 1, 1}};
const float kMass[3] = {1.0f, 0.75f, 1.35f};   // 推力倍率不同 → 三顆甩得不一樣
float    s_mot_x = 0, s_mot_y = 0;             // 最新線性加速度（g，畫面方向）
float    s_gr_x = 0, s_gr_y = 1;               // A8：視重力（g，畫面方向）
bool     s_pin = false;                        // 正在甩
uint32_t s_pin_last = 0;                       // 最近一次「有在搖」的時刻

// 一顆的物理：甩的時候被推、撞牆反彈；停手後彈簧歸位。home／half 是這顆平常的中心與半寬高（設計單位，相對臉中心）
void pinStep(Body& b, int i, float dt, float hx, float hy, float halfw, float halfh,
             float wl, float wr, float wt, float wb) {
  if (dt <= 0) return;
  if (s_pin) {
    b.vx += cfg::PIN_SIGN * cfg::PIN_GAIN * kMass[i] * s_mot_x * dt;
    b.vy += cfg::PIN_SIGN * cfg::PIN_GAIN * kMass[i] * s_mot_y * dt;
    const float drag = fmaxf(0, 1 - cfg::PIN_DRAG * dt);
    b.vx *= drag; b.vy *= drag;
  } else {
    b.vx += (-cfg::PIN_RETURN_K * b.ox - cfg::PIN_RETURN_C * b.vx) * dt;
    b.vy += (-cfg::PIN_RETURN_K * b.oy - cfg::PIN_RETURN_C * b.vy) * dt;
  }
  const float vmax = cfg::PIN_VMAX;
  b.vx = fmaxf(-vmax, fminf(vmax, b.vx));
  b.vy = fmaxf(-vmax, fminf(vmax, b.vy));
  b.ox += b.vx * dt;
  b.oy += b.vy * dt;
  // 撞牆：位置夾回牆內、速度反向打折、被撞方向壓扁
  const float x = hx + b.ox, y = hy + b.oy;
  if (x - halfw < wl) { b.ox = wl + halfw - hx; if (b.vx < 0) { b.vx = -b.vx * cfg::PIN_REST; b.sx = cfg::PIN_SQUASH; } }
  if (x + halfw > wr) { b.ox = wr - halfw - hx; if (b.vx > 0) { b.vx = -b.vx * cfg::PIN_REST; b.sx = cfg::PIN_SQUASH; } }
  if (y - halfh < wt) { b.oy = wt + halfh - hy; if (b.vy < 0) { b.vy = -b.vy * cfg::PIN_REST; b.sy = cfg::PIN_SQUASH; } }
  if (y + halfh > wb) { b.oy = wb - halfh - hy; if (b.vy > 0) { b.vy = -b.vy * cfg::PIN_REST; b.sy = cfg::PIN_SQUASH; } }
  const float rk = fminf(1, dt * 12);
  b.sx += (1 - b.sx) * rk;
  b.sy += (1 - b.sy) * rk;
  if (!s_pin && fabsf(b.ox) < 0.3f && fabsf(b.oy) < 0.3f && fabsf(b.vx) < 3 && fabsf(b.vy) < 3) { b.ox = b.oy = b.vx = b.vy = 0; }
}

// ── 小符號（粒子）──
// A5-1（11-20 Miles：符號太小、要可愛生動、右上角空間可以佔滿）：
//   符號改成向量畫（粗筆畫＋外圈光暈，跟眼睛的霓虹風一致），不再用放大的點陣字（放大會鋸齒）；
//   尺寸約放大 1.6 倍；位置集中在「符號角」：直式＝右上、橫式＝臉旁邊不靠 A 鍵的那一側（上方只剩約 20px）。
//   Text 的 size＝符號高度（設計單位）；Note／Dots 的 size＝比例。
enum class PK : uint8_t { Text, Note, Dots, Dot, Heart, Spark };
struct Part { bool on; PK kind; char ch; float x, y, vx, vy, age, life, size; Rgb col; };
constexpr int MAX_PARTS = 6;
Part s_parts[MAX_PARTS];
float s_face_l = 0, s_face_r = 135;   // 臉的區域（像素），render() 每幀更新
float s_sym_top = 0;                    // 像素臉：符號最高只到狀態列下面（不蓋狀態列、左上大字）
float clampX(float x, float half) { return fminf(fmaxf(x, s_face_l + half + 2), s_face_r - half - 2); }
// 符號角（設計單位，相對臉中心）：直式右上；橫式在臉旁、避開 A 鍵側（對話區在 A 鍵側）
float symX() { return s_orient == Orient::Portrait ? 36.0f : s_orient == Orient::LandARight ? -104.0f : 104.0f; }
float symY() { return s_orient == Orient::Portrait ? -70.0f : -30.0f; }
void emit(PK kind, char ch, float x, float y, const Rgb& col, float size, float vx = 0, float vy = -18, float life = 1.1f) {
  int slot = 0;
  float oldest = -1;
  for (int i = 0; i < MAX_PARTS; ++i) {
    if (!s_parts[i].on) { slot = i; oldest = 1e9f; break; }
    if (s_parts[i].age > oldest) { oldest = s_parts[i].age; slot = i; }
  }
  s_parts[slot] = {true, kind, ch, x, y, vx, vy, 0, life, size, col};
}

// ── B4 對話區 ──
constexpr int      CONV_LINES_PORTRAIT = 5;
constexpr int      CONV_PAD     = 3;
constexpr int      EVA_CONV_PAD = 4;
constexpr int      EVA_CONV_HEADER = 16;   // 頁籤＋外框上緣到第一行字
constexpr int      EVA_CONV_TAB_H = 14;    // 左上「受信」頁籤高度
constexpr int      EVA_CONV_LAND_WIDTH = 80;
constexpr uint32_t CONV_HOLD_MS = 5000;
constexpr size_t   USER_CAP     = 480;
constexpr size_t   BOT_CAP      = 1500;
char     s_user[USER_CAP + 1] = "";
char     s_bot[BOT_CAP + 1] = "";
size_t   s_user_len = 0, s_bot_len = 0;
bool     s_conv_on = false;
uint32_t s_conv_hide_at = 0;
float    s_conv_k = 0.0f;
bool     s_conv_dirty = false;
bool     s_conv_wrapped_eva = false;
// pfx＝這一行先畫前綴（直式：段落第一行前綴＋字；橫式：前綴自己一行、len＝0）
struct ConvLine { const char* p; uint16_t len; bool bot; bool pfx; };
constexpr int MAX_LINES = 120;
ConvLine s_lines[MAX_LINES];
int      s_nlines = 0;
const char* const PFX_USER = "你：";
const char* const PFX_BOT  = "Friday：";

size_t utf8Len(uint8_t lead) { return lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1; }

void appendText(char* buf, size_t& len, size_t cap, const char* add) {
  size_t n = strlen(add);
  if (n > cap) { add += n - cap; n = cap; while (n && (((uint8_t)*add) & 0xC0) == 0x80) { ++add; --n; } }
  if (len + n > cap) {
    size_t drop = len + n - cap;
    while (drop < len && (((uint8_t)buf[drop]) & 0xC0) == 0x80) ++drop;
    memmove(buf, buf + drop, len - drop);
    len -= drop;
  }
  memcpy(buf + len, add, n);
  len += n;
  buf[len] = '\0';
}

void pushLine(const char* p, uint16_t len, bool bot, bool pfx) {
  if (s_nlines < MAX_LINES) s_lines[s_nlines++] = {p, len, bot, pfx};
}

void wrapInto(const char* text, size_t len, bool bot, int maxw, int prefix_w) {
  const char* line = text;
  int w = prefix_w;
  const char* p = text;
  const char* end = text + len;
  char ch[5];
  while (p < end) {
    if (*p == '\n') {
      pushLine(line, (uint16_t)(p - line), bot, false);
      ++p; line = p; w = 0; continue;
    }
    const size_t n = utf8Len((uint8_t)*p);
    memcpy(ch, p, n); ch[n] = '\0';
    const int cw = s_cv.textWidth(ch);
    if (w + cw > maxw && p > line) {
      pushLine(line, (uint16_t)(p - line), bot, false);
      line = p; w = 0;
    }
    w += cw;
    p += n;
  }
  if (p > line) pushLine(line, (uint16_t)(p - line), bot, false);
}

int convHeight() { return CONV_LINES_PORTRAIT * 14 + CONV_PAD * 2; }   // efontTW_12：每行 12px＋行距 2px
bool landscape() { return s_orient != Orient::Portrait; }
int convExtent(bool eva) { return eva && landscape() ? EVA_CONV_LAND_WIDTH : convHeight() + (eva ? 4 : 0); }   // +4：底邊警示斜紋（11-21）

const char* convPrefix(bool bot, bool eva) {
  if (eva) return bot ? "F: " : "> ";
  return bot ? PFX_BOT : PFX_USER;
}
bool land_sym() { return s_orient != Orient::Portrait; }

void addSeg(const char* text, size_t len, bool bot, int maxw, bool eva) {
  if (landscape() && !eva) {
    pushLine(text, 0, bot, true);            // 橫式欄太窄：前綴自己一行
    wrapInto(text, len, bot, maxw, 0);
  } else {
    const int start = s_nlines;
    wrapInto(text, len, bot, maxw, s_cv.textWidth(convPrefix(bot, eva)));
    if (s_nlines > start) s_lines[start].pfx = true;
  }
}

void rewrap(bool eva) {
  s_cv.setFont(&fonts::efontTW_12);
  s_cv.setTextSize(1);
  const int pad = eva ? EVA_CONV_PAD : CONV_PAD;
  const int maxw = (landscape() ? convExtent(eva) : s_cv.width()) - pad * 2;
  s_nlines = 0;
  if (s_user_len) addSeg(s_user, s_user_len, false, maxw, eva);
  if (s_bot_len) addSeg(s_bot, s_bot_len, true, maxw, eva);
  s_cv.setFont(&fonts::Font0);
  s_conv_wrapped_eva = eva;
  s_conv_dirty = false;
}

// A7（11-21 Miles）：表情背景淡淡的浮水印——原創字樣（不用 EVA 的商標字），
// 小字機號＋大字「初號機」（Miles：第一台機器，致敬）＋三條斜槓，壓在臉區左下、在臉之下。
void pxWatermark(M5Canvas& g, int fx0, int top, int fw, int fy1, uint16_t col) {
  const ui_labels::Bmp* big = ui_labels::find("wm_big");
  if (!big) return;
  const int x = fx0 + 5, by = fy1 - big->h - 5;
  if (by - 12 < top) return;
  g.fillRect(x - 2, by - 13, fx0 + fw - x + 2, big->h + 15, TFT_BLACK);   // 浮水印那塊不畫格子（11-21 Miles：被格子蓋到）
  g.setFont(&fonts::Font0);
  g.setTextSize(1);
  g.setTextDatum(top_left);
  g.setTextColor(col);
  g.drawString("M5S3PACK-01", x + 1, by - 11);
  g.drawBitmap(x, by, big->bits, big->w, big->h, col);
  // 三條斜槓（平行四邊形，往右上斜）
  const int sx = x + big->w + 6, sh = big->h;
  for (int k = 0; k < 3; ++k) {
    const int bx = sx + k * 7;
    if (bx + 4 + sh / 3 > fx0 + fw) break;
    for (int r = 0; r < sh; ++r) g.drawFastHLine(bx + (sh - 1 - r) / 3, by + r, 4, col);
  }
}

// 紅色警示斜紋（跟資訊頁上下邊同一款：6px 紅、6px 空、每列左移 1px）
void convHazard(int x0, int x1, int y, int h) {
  if (x1 <= x0) return;
  const uint16_t red = s_cv.color565(0xFF, 0x29, 0x23);
  for (int row = 0; row < h; ++row)
    for (int left = x0 - row - 8; left < x1; left += 12) {
      const int a = left < x0 ? x0 : left, b = left + 6 < x1 ? left + 6 : x1;
      if (b > a) s_cv.drawFastHLine(a, y + row, b - a, red);
    }
}

void drawEvaConvFrame(int x, int y, int w, int h, uint16_t amber) {
  // A7（11-21）：外框從頁籤下緣開始；左上凸出一塊實心頁籤寫「受信」（點陣圖，不糊）
  const int fy = y + EVA_CONV_TAB_H - 1;
  s_cv.drawRect(x, fy, w, h - (fy - y), amber);
  // 11-21 Miles「受信不夠銳利」：改用 efontTW_12（原生 12px 點陣字，不是縮小的明體），黑字疊兩次加粗
  s_cv.setFont(&fonts::efontTW_12);
  s_cv.setTextSize(1);
  s_cv.setTextDatum(top_left);
  const int tw = s_cv.textWidth("受信") + 9;
  s_cv.fillRect(x, y, tw, EVA_CONV_TAB_H, amber);
  s_cv.setTextColor(TFT_BLACK);
  s_cv.drawString("受信", x + 4, y + 1);
  s_cv.drawString("受信", x + 5, y + 1);
  s_cv.setFont(&fonts::Font0);
  s_cv.setTextSize(1);
  s_cv.setTextDatum(top_left);
  s_cv.setTextColor(amber);
  // 橫式直欄貼著狀態列（已寫 FRIDAY）→ 只有直式加副標
  int sx0 = x + tw + 3;
  if (!landscape()) { s_cv.drawString("// FRIDAY", sx0, y + (EVA_CONV_TAB_H - 8) / 2 + 1); sx0 += s_cv.textWidth("// FRIDAY") + 4; }
  // 11-21 Miles：加資訊頁邊緣同款紅色警示斜紋（頁籤右邊一段＋外框底邊）
  convHazard(sx0, x + w - 1, fy - 5, 4);
  convHazard(x + 1, x + w - 1, y + h - 4, 3);
}

// 對話區：直式＝畫面下方橫條；橫式＝A 鍵側直欄
void drawConv(int W, int H, bool eva) {
  const int cs = (int)(convExtent(eva) * s_conv_k + 0.5f);
  if (cs <= 0) return;
  const bool land = landscape();
  int rx, ry, rw, rh;
  if (!land) { rx = 0; ry = H - cs; rw = W; rh = cs; }
  else { rw = cs; rh = H; ry = 0; rx = (s_orient == Orient::LandALeft) ? 0 : W - cs; }
  s_cv.fillRect(rx, ry, rw, rh, TFT_BLACK);
  const uint16_t amber = s_cv.color565(255, 155, 0);
  const uint16_t userColor = s_cv.color565(255, 241, 222);
  const uint16_t botGreen = s_cv.color565(0x39, 0xFF, 0x14);   // 11-21 Miles：Friday 的回答用資訊頁電池綠
  if (eva) {
    drawEvaConvFrame(rx, ry, rw, rh, amber);
  } else {
    const uint16_t edge = s_cv.color565(40, 52, 60);
    if (!land) s_cv.drawFastHLine(0, ry, W, edge);
    else s_cv.drawFastVLine(s_orient == Orient::LandALeft ? rx + rw - 1 : rx, 0, H, edge);
  }
  if (s_conv_k < 0.95f) return;   // 展開中不畫字
  if (s_conv_dirty || s_conv_wrapped_eva != eva) rewrap(eva);
  s_cv.setTextSize(1);
  s_cv.setTextDatum(top_left);
  if (eva) {
    s_cv.setClipRect(rx + 1, ry + EVA_CONV_TAB_H, rw - 2, rh - EVA_CONV_TAB_H - 1);
    s_cv.setTextColor(amber);
  }
  s_cv.setFont(&fonts::efontTW_12);
  const int pad = eva ? EVA_CONV_PAD : CONV_PAD;
  const int firstY = eva ? EVA_CONV_HEADER + 3 : CONV_PAD + 1;
  const int nl = eva ? 1 + (rh - firstY - 13 - 4) / 14   // -4：底邊警示斜紋
                     : land ? (H - CONV_PAD * 2) / 14 : CONV_LINES_PORTRAIT;
  const int keep = land ? 3 : 2;   // Friday 開口後，你的話保留幾行（橫式含前綴那行）
  int idx[MAX_LINES];
  int nshow = 0, nuser = 0;
  for (int i = 0; i < s_nlines; ++i) if (!s_lines[i].bot) ++nuser;
  const int nbot = s_nlines - nuser;
  bool user_cut = false;
  if (nbot == 0) {
    for (int i = (s_nlines > nl ? s_nlines - nl : 0); i < s_nlines; ++i) idx[nshow++] = i;
  } else {
    const int ku = nuser < keep ? nuser : keep;
    user_cut = nuser > ku;
    for (int i = 0; i < ku; ++i) idx[nshow++] = i;
    const int room = nl - ku;
    for (int i = (nbot > room ? s_nlines - room : nuser); i < s_nlines; ++i) idx[nshow++] = i;
  }
  char tmp[256];
  for (int k = 0; k < nshow; ++k) {
    const ConvLine& L = s_lines[idx[k]];
    const int y = ry + firstY + k * 14;
    int x = rx + pad;
    if (L.pfx) {
      s_cv.setTextColor(eva ? (L.bot ? botGreen : userColor)
                            : L.bot ? s_cv.color565(0x00, 0xF0, 0xFF) : s_cv.color565(MG_R, MG_G, MG_B));
      const char* pf = convPrefix(L.bot, eva);
      s_cv.drawString(pf, x, y);
      x += s_cv.textWidth(pf);
    }
    if (L.len == 0) continue;
    size_t n = L.len < sizeof(tmp) - 5 ? L.len : sizeof(tmp) - 5;
    const bool last_user = !L.bot && (k + 1 < nshow ? s_lines[idx[k + 1]].bot : true);
    if (user_cut && last_user) {
      size_t j = n;
      while (j > 0 && (((uint8_t)L.p[j - 1]) & 0xC0) == 0x80) --j;
      if (j > 0) --j;
      memcpy(tmp, L.p, j);
      memcpy(tmp + j, "\xE2\x80\xA6", 3);   // …
      n = j + 3;
    } else {
      memcpy(tmp, L.p, n);
    }
    tmp[n] = '\0';
    s_cv.setTextColor(eva ? (L.bot ? botGreen : userColor)
                          : L.bot ? s_cv.color565(241, 250, 238) : s_cv.color565(150, 160, 175));
    s_cv.drawString(tmp, x, y);
  }
  s_cv.setFont(&fonts::Font0);
  if (eva) s_cv.clearClipRect();
}


// ═════ A7 像素臉（Miles 11-20 參考圖：NERV 風——六角格底、掃描線眼睛、狀態大字、強度方格、取景角、狀態列）═════
constexpr bool PIXEL = true;
const char* const kPxLabel[] = {"st_idle", "st_listen", "st_think", "st_speak", "st_happy", "st_alert", "st_scared", "st_sleepy", "st_asleep"};   // ui_labels 的 key
const char* const kPxStatus[] = {"待機中", "聆聽中", "思考中", "說話中", "開心", "警報", "驚嚇", "想睡", "睡眠中"};
struct PxTheme { Rgb accent, eye; };
PxTheme pxTheme(Mood m, bool offline) {
  if (offline) return {{95, 94, 90}, {120, 118, 112}};
  if (m == Mood::Angry) return {{0xE8, 0x2A, 0x2A}, {0xFF, 0xC8, 0x70}};   // 警報：紅色系，眼睛淡琥珀
  return {{0xFF, 0x9B, 0x00}, {0xFF, 0xB0, 0x50}};
}

// ── A7 六角符號（11-21 Miles 定案）：正六角形、方向與比例同背景格、邊長＝背景格 2 倍、中心吸附在格子中心 ──
constexpr float HEX_A = 9.0f, HEX_HH = HEX_A * 0.866f, HEX_DX = HEX_A * 1.5f;
// 找最近的格子中心（跟 pxHexGrid 同一套座標）
void hexSnap(float& x, float& y) {
  const int col = (int)lroundf(x / HEX_DX);
  float best = 1e9f, bx = x, by = y;
  for (int c = col - 1; c <= col + 1; ++c) {
    const float cx = c * HEX_DX, base = ((c & 1) ? HEX_HH : 0) - HEX_HH;
    const int k = (int)lroundf((y - base) / (2 * HEX_HH));
    for (int kk = k - 1; kk <= k + 1; ++kk) {
      const float cy = base + kk * 2 * HEX_HH, d = (cx - x) * (cx - x) + (cy - y) * (cy - y);
      if (d < best) { best = d; bx = cx; by = cy; }
    }
  }
  x = bx; y = by;
}
void fillHexCell(float cx, float cy, float a, uint16_t c) {
  const float h = a * 0.866f;
  const int l = (int)(cx - a), ml = (int)(cx - a / 2), mr = (int)(cx + a / 2), r = (int)(cx + a);
  const int t = (int)(cy - h), b = (int)(cy + h), m = (int)cy;
  s_cv.fillRect(ml, t, mr - ml + 1, b - t + 1, c);
  s_cv.fillTriangle(l, m, ml, t, ml, b, c);
  s_cv.fillTriangle(r, m, mr, t, mr, b, c);
}
void drawHexCell(float cx, float cy, float a, uint16_t c) {
  const float h = a * 0.866f;
  const int l = (int)(cx - a), ml = (int)(cx - a / 2), mr = (int)(cx + a / 2), r = (int)(cx + a);
  const int t = (int)(cy - h), b = (int)(cy + h), m = (int)cy;
  s_cv.drawLine(l, m, ml, t, c); s_cv.drawFastHLine(ml, t, mr - ml + 1, c); s_cv.drawLine(mr, t, r, m, c);
  s_cv.drawLine(r, m, mr, b, c); s_cv.drawFastHLine(ml, b, mr - ml + 1, c); s_cv.drawLine(ml, b, l, m, c);
}
void sprite(float cx, float cy, const char* const* rows, int n, int k, uint16_t c) {
  const int w = (int)strlen(rows[0]);
  const int x0 = (int)cx - w * k / 2, y0 = (int)cy - n * k / 2;
  for (int j = 0; j < n; ++j)
    for (int i = 0; i < w; ++i)
      if (rows[j][i] == '#') s_cv.fillRect(x0 + i * k, y0 + j * k, k, k, c);
}
const char* const SP_BANG[] = {"##", "##", "##", "##", "  ", "##"};
const char* const SP_Q[] = {" ### ", "#   #", "   # ", "  #  ", "     ", "  #  "};
const char* const SP_NOTE[] = {"   ###", "   # #", "   #  ", "   #  ", " ###  ", "####  ", " ##   "};
const char* const SP_HEART[] = {" ## ## ", "#######", "#######", " ##### ", "  ###  ", "   #   "};
const char* const SP_SPARK[] = {"   #   ", "   #   ", "  ###  ", "#######", "  ###  ", "   #   ", "   #   "};
const char* const SP_DROP[] = {"   ##   ", "   ##   ", "  ####  ", " ###### ", " ###### ", "## #####", "## #####", "########", " ###### ", "  ####  "};
const char* const SP_Z[] = {"#####", "   # ", "  #  ", " #   ", "#####"};
const char* const SP_DOTS[] = {"# # #"};
const char* const SP_BOLT[] = {"   ##", "  ## ", " ##  ", "#####", "  ## ", " ##  ", "##   "};   // A4b 閃電
enum class HB : uint8_t { Warn, Bang, Drop, Ques, Note, Heart, Spark, Dots, Zed, Bolt };
// 單顆六角徽章：正六角形、方向跟背景格一樣（左右尖），邊長＝背景格 2 倍，中心吸附在格子中心
constexpr float BADGE_A = 2 * HEX_A, BADGE_HH = BADGE_A * 0.866f;
void badgeFit(float& x, float& y) {
  hexSnap(x, y);
  for (int i = 0; i < 4 && y - BADGE_HH < s_sym_top; ++i) y += 2 * HEX_HH;
  for (int i = 0; i < 10 && x - BADGE_A < s_face_l; ++i) x += 2 * HEX_DX;
  for (int i = 0; i < 10 && x + BADGE_A > s_face_r - 1; ++i) x -= 2 * HEX_DX;   // 最後保證不出右邊（橫式符號區比徽章窄時寧可壓到眼睛邊）
}
// 事件（警告、音符、Z）＝實心底黑圖；狀態（疑問、愛心、星芒、哈欠）＝黑底外框＋亮圖
void drawBadge(HB b, float cx, float cy, float alpha, float grow, const Rgb& acc, const Rgb& eye) {
  const bool ev = (b == HB::Warn || b == HB::Bang || b == HB::Drop || b == HB::Note || b == HB::Zed || b == HB::Bolt);
  const float a = BADGE_A * grow;
  const uint16_t edge = fade(acc, alpha);
  const uint16_t fg = ev ? (uint16_t)TFT_BLACK : fade(eye, alpha);
  if (ev) fillHexCell(cx, cy, a, edge);
  else { fillHexCell(cx, cy, a, TFT_BLACK); drawHexCell(cx, cy, a, edge); drawHexCell(cx, cy, a - 1, edge); }
  if (grow < 0.85f) return;
  switch (b) {
    case HB::Warn:
      sprite(cx, cy - 6, SP_BANG, 6, 2, fg);
      s_cv.setFont(&fonts::TomThumb); s_cv.setTextSize(1); s_cv.setTextDatum(middle_center);
      s_cv.setTextColor(fg); s_cv.drawString("WARNING", (int)cx + 1, (int)cy + 7);
      break;
    case HB::Bang: sprite(cx, cy, SP_BANG, 6, 3, fg); break;
    case HB::Drop: sprite(cx, cy, SP_DROP, 10, 2, fg); break;
    case HB::Ques: sprite(cx, cy, SP_Q, 6, 2, fg); break;
    case HB::Note: sprite(cx, cy, SP_NOTE, 7, 2, fg); break;
    case HB::Heart: sprite(cx, cy, SP_HEART, 6, 2, fg); break;
    case HB::Spark: sprite(cx, cy, SP_SPARK, 7, 2, fg); break;
    case HB::Dots: sprite(cx, cy, SP_DOTS, 1, 3, fg); break;
    case HB::Zed: sprite(cx, cy, SP_Z, 5, 2, fg); break;
    case HB::Bolt: sprite(cx, cy, SP_BOLT, 7, 2, fg); break;
  }
}
// 六角格（平頂，邊長 a）：每格只畫 3 條邊（相鄰格共用），省一半線
void pxHexGrid(M5Canvas& g, int W, int H, uint16_t c) {
  const float a = 9.0f, hh = a * 0.866f;   // 邊長、半高
  const float dx = a * 1.5f;
  for (int col = -1; col * dx < W + a; ++col) {
    const float cx = col * dx;
    const float off = (col & 1) ? hh : 0;
    for (float cy = -hh + off; cy < H + hh; cy += 2 * hh) {
      const int x0 = (int)(cx - a), x1 = (int)(cx - a / 2), x2 = (int)(cx + a / 2);
      const int yt = (int)(cy - hh), ym = (int)cy;
      g.drawLine(x0, ym, x1, yt, c);   // 左上斜邊
      g.drawFastHLine(x1, yt, x2 - x1, c);   // 上邊
      g.drawLine(x2, yt, (int)(cx + a), ym, c);   // 右上斜邊
    }
  }
}

// 背景快取（11-21 27-a7f 實機 j：迴圈 37ms 超過 25ms 預算）：六角格＋浮水印每幀約 400 條線，
// 只有方向、臉區域、主題色變了才重畫到 s_bg，平常整塊 memcpy（64KB，PSRAM→PSRAM 約 1ms 內）。
M5Canvas s_bg(&M5.Display);
struct BgKey { int rot, fx0, top, fw, fy1; uint16_t grid, wm; };
BgKey s_bgk = {-1, 0, 0, 0, 0, 0, 0};
size_t s_bg_bytes = 0;
bool pxBackground(int W, int H, int fx0, int top, int fw, int fy1, uint16_t grid, uint16_t wm) {
  if (!s_bg_bytes || !s_bg.getBuffer()) return false;
  const BgKey k = {(int)s_cv.getRotation(), fx0, top, fw, fy1, grid, wm};
  if (memcmp(&k, &s_bgk, sizeof k) != 0) {
    s_bg.setRotation(k.rot);
    s_bg.fillScreen(c565(BG));
    s_bg.setClipRect(fx0, top, fw, fy1 - top);
    pxHexGrid(s_bg, W, H, grid);
    pxWatermark(s_bg, fx0, top, fw, fy1, wm);
    s_bg.clearClipRect();
    s_bgk = k;
  }
  memcpy(s_cv.getBuffer(), s_bg.getBuffer(), s_bg_bytes);
  return true;
}
// ═════ A4b 充電液體（Miles 11-21）：背景六角格裝液體，量跟電量，每格往重力方向的空鄰格流、被擋住就側滑（會攤平）═════
// 格子座標跟 pxHexGrid／hexSnap 同一套：第 c 欄中心 x＝c·DX；y＝base(c)＋k·2HH，base(c)＝(c 奇數 ? HH : 0) − HH
constexpr int LQ_COLS = 20, LQ_ROWS = 20;   // 橫式 240px／13.5＝18 欄、直式 240px／15.6＝16 列
struct LqCell { int8_t c, k, dir; };
LqCell   s_lq[cfg::LQ_MAX];
int      s_lq_n = 0;
uint8_t  s_lq_occ[LQ_COLS][LQ_ROWS];
bool     s_lq_on = false;
uint32_t s_lq_next = 0, s_lq_off_t = 0;
int      s_lq_x0 = 0, s_lq_x1 = 0, s_lq_y0 = 0, s_lq_y1 = 0;   // 容器（臉的區域）
int      s_chg = 0;                                           // 0 沒充電、1 充電中、2 充飽
inline float lqX(int c) { return c * HEX_DX; }
inline float lqY(int c, int k) { return ((c & 1) ? HEX_HH : 0) - HEX_HH + k * 2 * HEX_HH; }
int lqRowOf(int c, float y) { return (int)lroundf((y - (((c & 1) ? HEX_HH : 0) - HEX_HH)) / (2 * HEX_HH)); }
bool lqValid(int c, int k) {
  if (c < 0 || c >= LQ_COLS || k < 0 || k >= LQ_ROWS) return false;
  const float x = lqX(c), y = lqY(c, k);
  return x - HEX_A >= s_lq_x0 && x + HEX_A <= s_lq_x1 && y - HEX_HH >= s_lq_y0 && y + HEX_HH <= s_lq_y1;
}
// 11-21 實機（29-a4d）：插線時 j 迴圈仍 35～65ms → 流動計算改查表：
//  有效格（容器內）存成表、容器變了才重算；鄰格的列偏移只跟欄的奇偶有關，開機算一次；
//  12 個方向的分數每步算一次（不是每格每方向算一次開根號／除法）；亂數用 LCG（不呼叫 esp_random）
uint8_t  s_lq_ok[LQ_COLS][LQ_ROWS];
int      s_lq_cap = 0, s_lq_key = -1;
int8_t   s_lq_dk[2][12];                        // [欄奇偶][方向] → 列偏移
float    s_lq_ux[12], s_lq_uy[12];              // 方向單位向量
bool     s_lq_tab = false;
uint32_t s_lq_rng = 12345;
float    s_lq_grx = 0, s_lq_gry = 1;              // 參考重力方向（變化超過約 8° 才更新）
uint32_t s_lq_gstable_t = 0;                      // 參考重力最後一次更新的時刻
uint32_t s_lq_us_max = 0;                     // 這一窗液體（流動＋畫）最久花幾 µs
inline uint32_t lqRand() { s_lq_rng = s_lq_rng * 1664525u + 1013904223u; return s_lq_rng >> 8; }
static const int8_t LQ_CX[12] = {0, 0, 1, 1, -1, -1, 2, -2, 1, 1, -1, -1};   // 6 鄰格＋6 隔一格（側滑用）
static const int8_t LQ_CY[12] = {-2, 2, -1, 1, -1, 1, 0, 0, -3, 3, -3, 3};   // 單位：x＝DX、y＝HH
void lqTables() {
  if (s_lq_tab) return;
  for (int p = 0; p < 2; ++p)
    for (int i = 0; i < 12; ++i) {
      const int c = 4 + p, k = 6, c2 = c + LQ_CX[i];
      s_lq_dk[p][i] = (int8_t)(lqRowOf(c2, lqY(c, k) + LQ_CY[i] * HEX_HH) - k);
    }
  for (int i = 0; i < 12; ++i) {
    const float vx = LQ_CX[i] * HEX_DX, vy = LQ_CY[i] * HEX_HH, len = sqrtf(vx * vx + vy * vy);
    s_lq_ux[i] = vx / len; s_lq_uy[i] = vy / len;
  }
  s_lq_tab = true;
}
void lqRebuild() {   // 容器變了：重算有效格與容量
  s_lq_cap = 0;
  for (int c = 0; c < LQ_COLS; ++c)
    for (int k = 0; k < LQ_ROWS; ++k) { s_lq_ok[c][k] = lqValid(c, k); s_lq_cap += s_lq_ok[c][k]; }
}
inline bool lqFree(int c, int k) {
  return (unsigned)c < (unsigned)LQ_COLS && (unsigned)k < (unsigned)LQ_ROWS && s_lq_ok[c][k] && !s_lq_occ[c][k];
}
void lqClear() { s_lq_n = 0; memset(s_lq_occ, 0, sizeof s_lq_occ); }
// 11-21 實機（29-a4c）：液體每幀用 fillHexCell（1 矩形＋2 三角形，每列一次繪圖呼叫）畫 70～110 格，
// 插線時 j 迴圈 30～57ms → 改成直接寫畫布緩衝（每列一段像素），繪圖呼叫的固定成本全省掉。
// 畫布會轉向（setRotation），所以先探出「畫面座標 → 緩衝索引」：index＝o＋x·sx＋y·sy（轉向變了才重探）
struct BufMap { int rot = -1, w = 0, h = 0; int32_t o = 0, sx = 0, sy = 0; bool ok = false; } s_bm;
void bufMapProbe() {   // 會清掉畫布 → 只能在背景 memcpy 之前呼叫
  const int rot = s_cv.getRotation();
  if (rot == s_bm.rot && s_bm.w == s_cv.width()) return;
  s_bm.rot = rot; s_bm.w = s_cv.width(); s_bm.h = s_cv.height(); s_bm.ok = false;
  uint16_t* buf = (uint16_t*)s_cv.getBuffer();
  if (!buf) return;
  const int32_t n = (int32_t)s_bm.w * s_bm.h;
  auto find = [&](int x, int y) -> int32_t {
    s_cv.fillScreen(TFT_BLACK);
    s_cv.drawPixel(x, y, TFT_WHITE);
    for (int32_t i = 0; i < n; ++i) if (buf[i]) return i;
    return -1;
  };
  const int32_t a = find(0, 0), b = find(1, 0), c = find(0, 1);
  if (a < 0 || b < 0 || c < 0) return;
  s_bm.o = a; s_bm.sx = b - a; s_bm.sy = c - a; s_bm.ok = true;
}
void fillHexFast(float cx, float cy, float a, uint16_t col) {
  if (!s_bm.ok) { fillHexCell(cx, cy, a, col); return; }
  uint16_t* buf = (uint16_t*)s_cv.getBuffer();
  const uint16_t sc = (uint16_t)((col >> 8) | (col << 8));   // 緩衝是位元組對調的 RGB565
  const float h = a * 0.866f;
  const int y0 = (int)ceilf(cy - h), y1 = (int)floorf(cy + h);
  for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < s_bm.h; ++y) {
    const float half = a - 0.5f * a * fabsf(y + 0.5f - cy) / h;   // 左右尖的六角：離中線越遠越窄
    int xl = (int)ceilf(cx - half), xr = (int)floorf(cx + half);
    if (xl < 0) xl = 0;
    if (xr >= s_bm.w) xr = s_bm.w - 1;
    int32_t idx = s_bm.o + xl * s_bm.sx + y * s_bm.sy;
    for (int x = xl; x <= xr; ++x, idx += s_bm.sx) buf[idx] = sc;
  }
}
// 容器變了（轉向、對話框開合）：超出容器的格子丟掉，之後會再從上方補進來
void lqPrune() {
  int w = 0;
  for (int i = 0; i < s_lq_n; ++i) {
    if (s_lq_ok[s_lq[i].c][s_lq[i].k]) s_lq[w++] = s_lq[i];
    else s_lq_occ[s_lq[i].c][s_lq[i].k] = 0;
  }
  s_lq_n = w;
}
// 走一步：沿重力最遠（最「下面」）的先動；優先往下方三個鄰格，全被擋住就照自己的流向側滑；撞牆有機會換方向
void lqStep(bool side) {   // side＝允許側滑（重力穩定一陣子後關掉，液面才會靜止、不用一直重送畫面）
  const float gm = sqrtf(s_gr_x * s_gr_x + s_gr_y * s_gr_y);
  if (gm < 0.15f) return;   // 平放：重力在螢幕平面上幾乎沒有分量 → 不流
  const float gx = s_gr_x / gm, gy = s_gr_y / gm, px = -gy, py = gx;   // p＝垂直重力（側邊）
  float sc[12], sd[12];     // 每個方向：沿重力的分數、往側邊的分數（每步算一次）
  for (int i = 0; i < 12; ++i) { sc[i] = s_lq_ux[i] * gx + s_lq_uy[i] * gy; sd[i] = s_lq_ux[i] * px + s_lq_uy[i] * py; }
  static int16_t ord[cfg::LQ_MAX];
  static float key[cfg::LQ_MAX];
  for (int i = 0; i < s_lq_n; ++i) { ord[i] = (int16_t)i; key[i] = lqX(s_lq[i].c) * gx + lqY(s_lq[i].c, s_lq[i].k) * gy; }
  for (int i = 1; i < s_lq_n; ++i) {   // 插入排序（上一步的順序大多沒變，接近線性）
    const int16_t v = ord[i];
    int j = i - 1;
    while (j >= 0 && key[ord[j]] < key[v]) { ord[j + 1] = ord[j]; --j; }
    ord[j + 1] = v;
  }
  for (int oi = 0; oi < s_lq_n; ++oi) {
    LqCell& q = s_lq[ord[oi]];
    const int p = q.c & 1;
    int bc = -1, bk = -1;
    float best = 0.45f;
    for (int i = 0; i < 6; ++i) {   // 往下：分數 ≥0.45 的空鄰格挑最高（加一點亂數打破平手）
      const float v = sc[i] + (lqRand() & 63) * (0.04f / 64);
      if (v < best) continue;
      const int c = q.c + LQ_CX[i], k = q.k + s_lq_dk[p][i];
      if (!lqFree(c, k)) continue;
      best = v; bc = c; bk = k;
    }
    if (bc < 0 && !side) continue;
    if (bc < 0) {                   // 被擋住 → 照自己的流向側滑
      float bs = -1;
      for (int i = 0; i < 12; ++i) {
        if (sc[i] < -0.12f || sc[i] > 0.45f || sd[i] * q.dir < 0.3f || sc[i] <= bs) continue;
        const int c = q.c + LQ_CX[i], k = q.k + s_lq_dk[p][i];
        if (!lqFree(c, k)) continue;
        bs = sc[i]; bc = c; bk = k;
      }
      if (bc < 0) { if ((lqRand() & 1023) < 307) q.dir = -q.dir; continue; }
    }
    s_lq_occ[q.c][q.k] = 0;
    q.c = (int8_t)bc; q.k = (int8_t)bk;
    s_lq_occ[q.c][q.k] = 1;
  }
}
// 補到目標格數：從容器「上方」那一半（逆重力那頭）隨機空格滴進來；多了就從最後加的拿掉
void lqFill(int target) {
  const float mx = (s_lq_x0 + s_lq_x1) * 0.5f, my = (s_lq_y0 + s_lq_y1) * 0.5f;
  for (int tries = 0; s_lq_n < target && s_lq_n < cfg::LQ_MAX && tries < 40; ++tries) {
    const int c = (int)(lqRand() % LQ_COLS), k = (int)(lqRand() % LQ_ROWS);
    if (!lqFree(c, k)) continue;
    if ((lqX(c) - mx) * s_gr_x + (lqY(c, k) - my) * s_gr_y > 0) continue;
    s_lq[s_lq_n++] = {(int8_t)c, (int8_t)k, (int8_t)((lqRand() & 1) ? 1 : -1)};
    s_lq_occ[c][k] = 1;
  }
  while (s_lq_n > target) { --s_lq_n; s_lq_occ[s_lq[s_lq_n].c][s_lq[s_lq_n].k] = 0; }
}

// ═════ A8 甩碎（Miles 11-21）：彈珠台一觸發，眼睛、嘴巴、符號炸成小六角，像骰子／彈珠在臉的框裡滾、互撞；
//       彈珠台結束（停手 0.4 秒）後彈簧拉回原位、拼回臉 ═════
// 碎片 feat：0 左眼、1 右眼、2 嘴巴（有家，拼回）；3 符號（沒有家，拼回時淡出）
struct Frag { float x, y, vx, vy, ang, va, ox, oy, b; uint8_t feat; };
Frag     s_fr[cfg::FR_MAX];
int      s_fr_n = 0;
bool     s_fr_on = false, s_fr_back = false;
uint32_t s_fr_back_t = 0;
uint32_t s_fr_calm_t = 0;                 // 停手的時刻（開始等 FR_HOLD_MS）
float    s_fr_home[3][2];                 // 三個有家的部位目前的中心（每幀更新，跟著表情與看方向）
float    s_fr_wl = 0, s_fr_wr = 135, s_fr_wt = 0, s_fr_wb = 240;   // 牆＝臉的區域
// 把一塊矩形切成小六角格，每格一顆碎片（記住相對位置，拼回時用）
void frSpawn(uint8_t feat, float cx, float cy, float w, float h, bool ell) {
  const float a = cfg::FR_A, dx = a * 1.5f, dy = a * 1.732f;
  for (float x = -w / 2 + a; x <= w / 2 - a * 0.3f; x += dx) {
    const int col = (int)lroundf((x + w / 2) / dx);
    for (float y = -h / 2 + a * 0.866f + ((col & 1) ? dy / 2 : 0); y <= h / 2 - a * 0.3f; y += dy) {
      if (ell && (x * x) / (w * w / 4) + (y * y) / (h * h / 4) > 1.05f) continue;
      if (s_fr_n >= cfg::FR_MAX) return;
      s_fr[s_fr_n++] = {cx + x, cy + y, rnd(-60, 60), rnd(-90, 30), 0, 0, x, y, rnd(0.85f, 1.0f), feat};
    }
  }
}
void frBadge(float x, float y) { badgeFit(x, y); frSpawn(3, x, y, BADGE_A * 1.6f, BADGE_HH * 2, true); }
void frPhysics(float dt) {
  if (dt <= 0) return;
  const float r = cfg::FR_A * 0.9f;
  for (int i = 0; i < s_fr_n; ++i) {
    Frag& f = s_fr[i];
    if (!s_fr_back) {
      // 11-22：甩著的時候重力打折＋依甩的力道亂數踢 → 一直甩就一直在盒子裡跳，不會全沉在底部不動
      const float gk = s_pin ? cfg::FR_SHAKE_GRAV : 1.0f;
      const float kick = s_pin ? cfg::FR_RATTLE * sqrtf(s_mot_x * s_mot_x + s_mot_y * s_mot_y) : 0.0f;
      f.vx += (cfg::FR_GRAVITY * gk * s_gr_x + cfg::FR_PUSH * s_mot_x + kick * rnd(-1, 1)) * dt;
      f.vy += (cfg::FR_GRAVITY * gk * s_gr_y + cfg::FR_PUSH * s_mot_y + kick * rnd(-1, 1)) * dt;
      if (kick > 0) f.va += kick * rnd(-0.02f, 0.02f) * dt;
      const float d = fmaxf(0, 1 - cfg::FR_DRAG * dt);
      f.vx *= d; f.vy *= d;
    } else if (f.feat < 3) {
      const float hx = s_fr_home[f.feat][0] + f.ox, hy = s_fr_home[f.feat][1] + f.oy;
      f.vx += (cfg::FR_RETURN_K * (hx - f.x) - cfg::FR_RETURN_C * f.vx) * dt;
      f.vy += (cfg::FR_RETURN_K * (hy - f.y) - cfg::FR_RETURN_C * f.vy) * dt;
      f.va += (-120 * f.ang - 12 * f.va) * dt;
    }
    f.vx = fmaxf(-900, fminf(900, f.vx));
    f.vy = fmaxf(-900, fminf(900, f.vy));
    f.x += f.vx * dt; f.y += f.vy * dt; f.ang += f.va * dt;
    if (s_fr_back) continue;
    // 撞牆：反彈；貼牆那一下把切向速度變成轉速（像骰子滾）
    if (f.x - r < s_fr_wl) { f.x = s_fr_wl + r; if (f.vx < 0) f.vx = -f.vx * cfg::FR_REST; f.va = f.vy / r; f.vy *= 0.9f; }
    if (f.x + r > s_fr_wr) { f.x = s_fr_wr - r; if (f.vx > 0) f.vx = -f.vx * cfg::FR_REST; f.va = -f.vy / r; f.vy *= 0.9f; }
    if (f.y - r < s_fr_wt) { f.y = s_fr_wt + r; if (f.vy < 0) f.vy = -f.vy * cfg::FR_REST; f.va = -f.vx / r; f.vx *= 0.9f; }
    if (f.y + r > s_fr_wb) { f.y = s_fr_wb - r; if (f.vy > 0) f.vy = -f.vy * cfg::FR_REST; f.va = f.vx / r; f.vx *= 0.9f; }
  }
  if (s_fr_back) return;
  // 互撞（當成圓，兩輪）：推開重疊、法向速度交換一部分（60 顆約 3600 次比對，ESP32 不到 1ms）
  for (int it = 0; it < 2; ++it)
    for (int i = 0; i < s_fr_n; ++i)
      for (int j = i + 1; j < s_fr_n; ++j) {
        Frag &a = s_fr[i], &b = s_fr[j];
        const float dx = b.x - a.x, dy = b.y - a.y, d2 = dx * dx + dy * dy, m = 2 * r;
        if (d2 >= m * m || d2 < 1e-4f) continue;
        const float d = sqrtf(d2), nx = dx / d, ny = dy / d, ov = (m - d) * 0.5f;
        a.x -= nx * ov; a.y -= ny * ov; b.x += nx * ov; b.y += ny * ov;
        const float rv = (b.vx - a.vx) * nx + (b.vy - a.vy) * ny;
        if (rv < 0) {
          const float imp = -1.3f * rv * 0.5f;
          a.vx -= imp * nx; a.vy -= imp * ny; b.vx += imp * nx; b.vy += imp * ny;
        }
      }
}
// 每顆畫成會轉的實心小六角（4 個三角形）
void frDraw(uint16_t eyeC, uint16_t mouthC, const Rgb& acc, float badgeAlpha) {
  for (int i = 0; i < s_fr_n; ++i) {
    const Frag& f = s_fr[i];
    const uint16_t c = f.feat == 2 ? mouthC : f.feat == 3 ? fade(acc, badgeAlpha * f.b) : eyeC;
    if (f.feat == 3 && badgeAlpha <= 0) continue;
    float px[6], py[6];
    for (int k = 0; k < 6; ++k) {
      const float an = f.ang + k * 1.0472f;
      px[k] = f.x + cfg::FR_A * cosf(an);
      py[k] = f.y + cfg::FR_A * sinf(an);
    }
    s_cv.fillTriangle((int)px[0], (int)py[0], (int)px[1], (int)py[1], (int)px[2], (int)py[2], c);
    s_cv.fillTriangle((int)px[0], (int)py[0], (int)px[2], (int)py[2], (int)px[3], (int)py[3], c);
    s_cv.fillTriangle((int)px[0], (int)py[0], (int)px[3], (int)py[3], (int)px[5], (int)py[5], c);
    s_cv.fillTriangle((int)px[3], (int)py[3], (int)px[4], (int)py[4], (int)px[5], (int)py[5], c);
  }
}

// 掃描線眼睛：切角矩形（八角）、每 3 列畫 2 列；生氣／想睡斜切、開心下方弧形挖掉，算每一列的左右界（不用底色遮，六角格才不會被塗掉）
void pxEye(float ex, float ey, float w, float h, int side, float ang, float slp, float hap, float sc, uint16_t c) {
  if (w < 3) w = 3;
  if (h < 3) h = 3;
  const float L = ex - w / 2, R = ex + w / 2, T = ey - h / 2, B = ey + h / 2;
  const float ch = fminf(w, h) * 0.24f;          // 切角
  const float dropA = T + h * 0.55f * ang, dropS = T + h * 0.65f * slp;
  const float hcy = ey + h * (0.62f + (1.0f - hap)), hrx = w * 0.8f + 2 * sc, hry = h * 0.55f + 2 * sc;
  for (int y = (int)ceilf(T); y < (int)B; ++y) {
    if (((y - (int)ceilf(T)) % 3) == 2) continue;     // 掃描線縫
    const float yy = y + 0.5f;
    float xl = L, xr = R;
    const float dt = yy - T, db = B - yy;
    if (dt < ch) { xl += ch - dt; xr -= ch - dt; }
    if (db < ch) { xl += ch - db; xr -= ch - db; }
    if (ang > 0.02f && dropA > T + 0.5f) {           // 生氣：內側上方斜切
      const float k = (yy - T) / (dropA - T) * (R - L);
      if (side < 0) xr = fminf(xr, L + k); else xl = fmaxf(xl, R - k);
    }
    if (slp > 0.02f && dropS > T + 0.5f) {           // 想睡：外側上方斜切
      const float k = (dropS - yy) / (dropS - T) * (R - L);
      if (side < 0) xl = fmaxf(xl, L + k); else xr = fminf(xr, R - k);
    }
    if (xr - xl < 1) continue;
    if (hap > 0.02f) {                                // 開心：挖掉下方橢圓 → 笑眼
      const float e = (yy - hcy) / hry;
      if (e * e < 1) {
        const float hw = hrx * sqrtf(1 - e * e);
        const float a0 = ex - hw, a1 = ex + hw;
        if (a0 > xl) s_cv.drawFastHLine((int)xl, y, (int)(fminf(a0, xr) - xl), c);
        if (a1 < xr) s_cv.drawFastHLine((int)fmaxf(a1, xl), y, (int)(xr - fmaxf(a1, xl)), c);
        continue;
      }
    }
    s_cv.drawFastHLine((int)lroundf(xl), y, (int)lroundf(xr - xl), c);
  }
}
void pxBrackets(int x, int y, int w, int h, uint16_t c) { ui_kit::brackets(s_cv, x, y, w, h, c); }
// 狀態列：「FRIDAY・待機中」＋右邊電量 %，下面一條細線
int s_px_battery = -1;
const char* s_status_ovr = nullptr;   // A4b：狀態列文字改成「充電中／充飽」
void pxHeader(int fx0, int W, Mood m, const Rgb& accent, uint16_t batCol) {
  // 狀態列畫法在 ui_kit::statusBar（體感同一套）；ROOT（紅底）／斷線（灰底）標籤放電量左邊
  ui_kit::Tag tags[2];
  int n = 0;
  if (power_key::rootMode()) tags[n++] = {"ROOT", &fonts::Font0, 3, s_cv.color565(0xFF, 0x29, 0x23)};
  if (s_offline) tags[n++] = {"斷線", &fonts::efontTW_10, 1, s_cv.color565(150, 150, 150)};
  const char* st = s_status_ovr ? s_status_ovr : kPxStatus[static_cast<int>(m)];
  ui_kit::statusBar(s_cv, fx0, W, "FRIDAY", st, c565(accent), s_px_battery, batCol, tags, n);
}
// 強度方格（右側直排，從下往上亮）：n 格亮、共 total 格
void pxMeter(int x, int yBottom, int total, int n, uint16_t on, uint16_t off) {
  for (int i = 0; i < total; ++i) {
    const int y = yBottom - (i + 1) * 9 + 3;
    if (i < n) s_cv.fillRect(x, y, 6, 6, on);
    else if (off) s_cv.drawRect(x, y, 6, 6, off);
  }
}


// ── 繪圖小工具（像素座標）──
void quad(float x0, float y0, float cx, float cy, float x1, float y1, float th, uint16_t col) {
  float px = x0, py = y0;
  for (int i = 1; i <= 8; ++i) {
    const float t = i / 8.0f, u = 1 - t;
    const float x = u * u * x0 + 2 * u * t * cx + t * t * x1;
    const float y = u * u * y0 + 2 * u * t * cy + t * t * y1;
    s_cv.drawWideLine((int32_t)px, (int32_t)py, (int32_t)x, (int32_t)y, th * 0.5f, col);
    px = x; py = y;
  }
}

// 漸層圓角眼（上 c1 下 c2）＋光暈＋眼皮。像素座標；side：−1 左眼、+1 右眼
void drawEye(float ex, float ey, float w, float h, const Rgb& c1, const Rgb& c2, int side,
             float ang, float slp, float hap, float sc) {
  if (w < 2) w = 2;
  if (h < 2) h = 2;
  // 11-20 Miles「外圈加光暈、要霓虹燈管那種螢光」：光暈從 1 圈（4px、28%）改成 3 圈由外往內變亮
  // （10／6.5／3.5px、10%／18%／32%）。外兩圈用一般圓角矩形（比平滑版快），最內圈維持平滑邊。
  // P1（11-20 Miles「光暈有點太大」）：10／6.5／3.5px → 7／4.5／2.5px，最外圈 10% → 8%
  const float pad = 7 * sc;    // 最外圈（眼皮遮罩、裁切範圍都要蓋到這裡）
  const float r = fminf(11 * sc, fminf(w, h) * 0.5f);
  {
    const float pads[3] = {7 * sc, 4.5f * sc, 2.5f * sc};
    const float alph[3] = {0.08f, 0.18f, 0.32f};
    for (int k = 0; k < 3; ++k) {
      const float p = pads[k];
      const int32_t gx = (int32_t)(ex - w / 2 - p), gy = (int32_t)(ey - h / 2 - p);
      const int32_t gw = (int32_t)(w + 2 * p), gh = (int32_t)(h + 2 * p), gr = (int32_t)(r + p);
      if (k < 2) s_cv.fillRoundRect(gx, gy, gw, gh, gr, fade(c2, alph[k]));
      else       s_cv.fillSmoothRoundRect(gx, gy, gw, gh, gr, fade(c2, alph[k]));
    }
  }
  const int x0 = (int)lroundf(ex - w / 2), y0 = (int)lroundf(ey - h / 2);
  const int iw = (int)lroundf(w), ih = (int)lroundf(h);
  for (int i = 0; i < ih; ++i) {
    const float yy = i + 0.5f;
    float d = 0;
    if (yy < r) d = r - yy;
    else if (yy > ih - r) d = yy - (ih - r);
    const int ins = d > 0 ? (int)(r - sqrtf(fmaxf(0, r * r - d * d)) + 0.5f) : 0;
    if (iw - 2 * ins <= 0) continue;
    const float f = ih > 1 ? i / (float)(ih - 1) : 0;
    s_cv.drawFastHLine(x0 + ins, y0 + i, iw - 2 * ins, c565(mixc(c1, c2, f)));
  }
  const uint16_t bg = c565(BG);
  const float L = ex - w / 2 - pad - 1, R = ex + w / 2 + pad + 1, T = ey - h / 2 - pad - 1;
  // 眼皮只准蓋在這隻眼睛（含光暈）的範圍內：開心的弧形遮罩在 hap 小的時候圓心會落到眼睛下方很遠，
  // A3 純黑底看不出來，A4 有網格後會把地平線與網格塗成底色（11-20 實機：說話句尾瞇眼時地平線出現缺口）
  s_cv.setClipRect((int32_t)L, (int32_t)T, (int32_t)(R - L + 1), (int32_t)(h + 2 * pad + 3));
  if (ang > 0.02f) {   // 生氣：內側往下壓
    const float drop = ey - h / 2 + h * 0.55f * ang;
    if (side < 0) s_cv.fillTriangle(L, T, R, T, R, drop, bg);
    else          s_cv.fillTriangle(L, T, R, T, L, drop, bg);
  }
  if (slp > 0.02f) {   // 想睡：外側往下垂
    const float drop = ey - h / 2 + h * 0.65f * slp;
    if (side < 0) s_cv.fillTriangle(L, T, R, T, L, drop, bg);
    else          s_cv.fillTriangle(L, T, R, T, R, drop, bg);
  }
  if (hap > 0.02f) {   // 開心：下方弧形遮罩 → 笑眼
    s_cv.fillEllipse((int32_t)ex, (int32_t)(ey + h * (0.62f + (1.0f - hap))), (int32_t)(w * 0.8f + pad),
                     (int32_t)(h * 0.55f + pad), bg);
  }
  s_cv.clearClipRect();
}

// 向量符號（x,y＝中心像素、h＝高度像素、g＝筆畫加粗量：先畫 g>0 的光暈再畫 g=0 的本體）
void glyphBang(float x, float y, float h, float g, uint16_t c) {   // ！
  s_cv.drawWideLine((int32_t)x, (int32_t)(y - 0.40f * h), (int32_t)x, (int32_t)(y + 0.12f * h), 0.09f * h + g, c);
  s_cv.fillCircle((int32_t)x, (int32_t)(y + 0.36f * h), (int32_t)(0.09f * h + g), c);
}
// 粗筆畫的圓弧（角度：0°＝右、順時針，螢幕 y 向下），用短線段接起來
void strokeArc(float cx, float cy, float R, float a0, float a1, float r, uint16_t c) {
  const int n = (int)fmaxf(3, fabsf(a1 - a0) / 22);   // 每段約 22°：夠圓，又不會畫太多段（錄音中主迴圈 11-20 實測變慢）
  float px = cx + R * cosf(a0 * 0.0174533f), py = cy + R * sinf(a0 * 0.0174533f);
  for (int i = 1; i <= n; ++i) {
    const float a = (a0 + (a1 - a0) * i / n) * 0.0174533f;
    const float x = cx + R * cosf(a), y = cy + R * sinf(a);
    s_cv.drawWideLine((int32_t)px, (int32_t)py, (int32_t)x, (int32_t)y, r, c);
    px = x; py = y;
  }
}
void glyphQuestion(float x, float y, float h, float g, uint16_t c) {   // ？
  const float R = 0.20f * h, t = 0.08f * h + g, cy = y - 0.18f * h;
  strokeArc(x, cy, R, 180, 450, t, c);   // 從左邊經過頂端繞到正下方
  s_cv.drawWideLine((int32_t)x, (int32_t)(cy + R), (int32_t)x, (int32_t)(y + 0.14f * h), t, c);
  s_cv.fillCircle((int32_t)x, (int32_t)(y + 0.36f * h), (int32_t)(0.09f * h + g), c);
}
void glyphZ(float x, float y, float h, float g, uint16_t c) {   // Z（睡覺）
  const float w = 0.30f * h, r = 0.075f * h + g;
  s_cv.drawWideLine((int32_t)(x - w), (int32_t)(y - 0.3f * h), (int32_t)(x + w), (int32_t)(y - 0.3f * h), r, c);
  s_cv.drawWideLine((int32_t)(x + w), (int32_t)(y - 0.3f * h), (int32_t)(x - w), (int32_t)(y + 0.3f * h), r, c);
  s_cv.drawWideLine((int32_t)(x - w), (int32_t)(y + 0.3f * h), (int32_t)(x + w), (int32_t)(y + 0.3f * h), r, c);
}
void glyphHeart(float x, float y, float h, float g, uint16_t c) {   // ♥
  const float r = 0.24f * h + g;
  s_cv.fillCircle((int32_t)(x - 0.21f * h), (int32_t)(y - 0.12f * h), (int32_t)r, c);
  s_cv.fillCircle((int32_t)(x + 0.21f * h), (int32_t)(y - 0.12f * h), (int32_t)r, c);
  s_cv.fillTriangle((int32_t)(x - 0.44f * h - g), (int32_t)(y - 0.04f * h), (int32_t)(x + 0.44f * h + g), (int32_t)(y - 0.04f * h),
                    (int32_t)x, (int32_t)(y + 0.42f * h + g), c);
}
void glyphSpark(float x, float y, float h, float g, uint16_t c) {   // ✦ 四角星
  const float a = 0.5f * h + g, b = 0.11f * h + g;
  s_cv.fillTriangle((int32_t)(x - b), (int32_t)y, (int32_t)(x + b), (int32_t)y, (int32_t)x, (int32_t)(y - a), c);
  s_cv.fillTriangle((int32_t)(x - b), (int32_t)y, (int32_t)(x + b), (int32_t)y, (int32_t)x, (int32_t)(y + a), c);
  s_cv.fillTriangle((int32_t)x, (int32_t)(y - b), (int32_t)x, (int32_t)(y + b), (int32_t)(x - a), (int32_t)y, c);
  s_cv.fillTriangle((int32_t)x, (int32_t)(y - b), (int32_t)x, (int32_t)(y + b), (int32_t)(x + a), (int32_t)y, c);
}
void glyphAnger(float x, float y, float h, float g, uint16_t c) {   // 漫畫的生氣青筋 💢：四段短弧，往中心凸
  const float d = 0.54f * h, R = 0.28f * h, t = 0.05f * h + g;
  for (int q = 0; q < 4; ++q) {
    const float ang = 45.0f + 90.0f * q;
    const float cx = x + d * cosf(ang * 0.0174533f), cy = y + d * sinf(ang * 0.0174533f);
    strokeArc(cx, cy, R, ang + 180 - 27, ang + 180 + 27, t, c);   // 四段分開、中間留空
  }
}

void drawNote(float x, float y, float s, uint16_t c) {   // ♪（畫的，不依賴字型）
  s_cv.fillCircle((int32_t)(x - 2 * s), (int32_t)(y + 4 * s), (int32_t)(3.2f * s), c);
  s_cv.drawWideLine((int32_t)(x + 1 * s), (int32_t)(y + 4 * s), (int32_t)(x + 1 * s), (int32_t)(y - 7 * s), 1.0f * s, c);
  s_cv.drawWideLine((int32_t)(x + 1 * s), (int32_t)(y - 7 * s), (int32_t)(x + 6 * s), (int32_t)(y - 4 * s), 1.0f * s, c);
}

const MoodInfo& info(Mood m) { return kMoods[static_cast<uint8_t>(m)]; }

void enterMood(Mood m, uint32_t now) {
  s_shown = m;
  s_t0 = now;
  s_ev = Ev();
  s_quiet = 0;
  s_sp[K_HY].v -= (m == Mood::Scared) ? 240.0f : 70.0f;   // 彈進來
  if (m == Mood::Scared) emit(PK::Text, PIXEL ? 'd' : '!', symX(), symY(), MG, 50);
}

// 模擬音量（預覽或還沒有真實音量時）：講 talk 秒、停 (cyc−talk) 秒，每秒約 6.5 個音節
float simVoice(float t, float talk, float cyc) {
  const float c = fmodf(t, cyc);
  if (c >= talk) return 0;
  const int32_t s = (int32_t)(t * 6.5f);
  const float f = t * 6.5f - s;
  const float v = (0.35f + 0.65f * hash01(s)) * fmaxf(0, sinf(f * 3.14159f)) * 1.15f;
  return v > 1 ? 1 : v;
}

int earSide() { return s_orient == Orient::LandARight ? 1 : -1; }   // 聆聽的耳朵：橫式朝 A 鍵側，直式在左

// 依表情設定這一幀的目標值（設計單位）。回傳音量 v、嘴型、眉毛模式
struct Frame { float v; MouthMode mm; int brows; float mouth_dx; };   // brows：0 無、1 單邊（聆聽）、2 雙邊
Frame behave(Mood m, float t, float dt, uint32_t now, float* tg) {
  Frame f = {0, MouthMode::Line, 2, 0};
  memcpy(tg, kDefault, sizeof(kDefault));
  tg[K_HY] = sinf(t * 2.8f) * 2.5f;   // 呼吸
  Ev& E = s_ev;
  switch (m) {
    case Mood::Idle:
      if (t > E.n) {
        E.n = t + rnd(1.4f, 3.0f);
        const float r = rnd(0, 1);
        if (r < 0.4f) { E.dx = rnd(5, 8) * (rnd(0, 1) < 0.5f ? -1 : 1); E.dx_until = t + 0.6f; }
        else if (r < 0.7f) s_wink_end = now + 180;
        else E.grin = t + 0.8f;
      }
      if (t < E.dx_until) tg[K_DX] = E.dx;
      if (t < E.grin) { tg[K_MW] = 30; tg[K_MC] = 1.1f; tg[K_SK] = 4; }
      break;

    case Mood::Listening: {
      const float v = s_listen_level >= 0 ? s_listen_level : simVoice(t, 2.6f, 4.0f);
      f.v = v;
      const int s = earSide();
      // 近耳的眼睛小、遠的大（像頭轉過去）
      const float nearX = s * 15, farX = -s * 37;
      if (s < 0) { tg[K_LX] = nearX; tg[K_LW] = 30; tg[K_LH] = 40; tg[K_LY] = 3; tg[K_RX] = farX; tg[K_RW] = 34; tg[K_RH] = 50 + (v > 0.9f ? 6 : 0); tg[K_RY] = -1; }
      else       { tg[K_RX] = nearX; tg[K_RW] = 30; tg[K_RH] = 40; tg[K_RY] = 3; tg[K_LX] = farX; tg[K_LW] = 34; tg[K_LH] = 50 + (v > 0.9f ? 6 : 0); tg[K_LY] = -1; }
      tg[K_ED] = 0; tg[K_BRA] = 1; tg[K_BRR] = 3 + v * 7;
      f.brows = 1;
      f.mouth_dx = -s * 19;
      const bool talk = v > 0.1f;
      s_quiet = talk ? 0 : s_quiet + dt;
      f.mm = (!talk && s_quiet > 0.4f) ? MouthMode::Smirk : MouthMode::O;
      tg[K_MW] = tg[K_MH] = 8 + v * 12;
      if (!talk && s_quiet > 0.4f && !E.paused) {   // 停頓：瞄耳朵、冒「？」
        E.paused = true; E.dx = s * 7; E.dx_until = t + 0.5f;
        emit(PK::Text, '?', symX(), symY(), MG, 46, 0, -6, 1.2f);
      }
      if (talk) E.paused = false;
      if (t < E.dx_until) tg[K_DX] = E.dx;
      if (s_prev_v < 0.62f && v >= 0.62f) s_sp[K_HY].v += 70;                      // 跟節奏點頭
      if (s_prev_v < 0.95f && v >= 0.95f) { s_sp[K_ED].v -= 160; emit(PK::Text, '!', symX(), symY(), MG, 44); }   // 大聲：耳朵一抖
      break;
    }

    case Mood::Thinking: {
      const int sq = ((int)(t / 1.5f)) % 2;
      tg[K_DX] = 6; tg[K_DY] = -10;
      tg[K_LH] = sq ? 32 : 44; tg[K_RH] = sq ? 44 : 32;
      tg[K_LY] = sq ? 3 : -3;  tg[K_RY] = -tg[K_LY];
      const float rc = fmodf(t, 4.5f);
      if (rc > 3.4f) {   // 偶爾眼睛轉一圈
        const float a = (rc - 3.4f) / 1.1f * 6.2832f;
        tg[K_DX] = 6 + cosf(a) * 7; tg[K_DY] = -10 + sinf(a) * 7;
      }
      f.mm = MouthMode::Wave; tg[K_MW] = 26;
      f.mouth_dx = sinf(t * 1.3f) * 6 + 4;
      break;
    }

    case Mood::Speaking: {
      const float v = s_speak_level >= 0 ? s_speak_level : simVoice(t, 2.8f, 3.6f);
      f.v = v;
      if (s_prev_v < 0.3f && v >= 0.3f) {   // 新音節：換嘴型
        static const char kV[] = "AOEAOAM";
        s_vowel = kV[(int)rnd(0, 6.99f)];
      }
      s_quiet = v > 0.08f ? 0 : s_quiet + dt;
      if (s_quiet < 0.35f) {
        f.mm = s_vowel == 'O' ? MouthMode::OFill : MouthMode::Open;
        switch (s_vowel) {
          case 'O': tg[K_MW] = 14 + 6 * v; tg[K_MH] = 8 + 18 * v; break;
          case 'E': tg[K_MW] = 36; tg[K_MH] = 4 + 8 * v; break;
          case 'M': tg[K_MW] = 22; tg[K_MH] = 3; break;
          default:  tg[K_MW] = 30; tg[K_MH] = 6 + 22 * v; break;
        }
        tg[K_LH] = tg[K_RH] = 46 * (1 - 0.16f * v);   // 張大嘴 → 眼睛被擠扁
        tg[K_LW] = tg[K_RW] = 32 * (1 + 0.07f * v);
        if (s_prev_v <= 0.72f && v > 0.72f) {     // 強拍：點頭、挑眉、偶爾冒符號或 wink
          s_sp[K_HY].v -= 95;
          E.brow = t + 0.35f;
          if (rnd(0, 1) < 0.35f) {
            const float r3 = rnd(0, 1);
            if (r3 < 0.4f) emit(PK::Note, 0, symX() + rnd(-8, 8), symY() + rnd(-6, 6), MG, 3.0f, rnd(-6, 6));
            else if (r3 < 0.7f) emit(PK::Spark, 0, symX() + rnd(-8, 8), symY() + rnd(-6, 6), {0x00, 0xF0, 0xFF}, 40, rnd(-6, 6));
            else emit(PK::Text, '!', symX() + rnd(-6, 6), symY(), {0x00, 0xF0, 0xFF}, 42, rnd(-6, 6));
          }
          if (rnd(0, 1) < 0.15f) s_wink_end = now + 160;
        }
      } else {
        f.mm = MouthMode::Line; tg[K_MW] = 26; tg[K_MC] = 1;
        if (s_quiet < 0.8f) tg[K_HAP] = 0.45f;     // 句尾：瞇眼笑
      }
      tg[K_HX] = sinf(t * 1.8f) * 4;
      const bool br = t < E.brow;
      tg[K_BRA] = br ? 1 : 0; tg[K_BRL] = tg[K_BRR] = br ? 6 : 0;
      break;
    }

    case Mood::Happy: {
      const float b = fabsf(sinf(t * 5));
      tg[K_HY] = -b * 11;
      if (b < 0.25f) { tg[K_LH] = tg[K_RH] = 38; tg[K_LW] = tg[K_RW] = 37; }   // 落地壓扁
      tg[K_HAP] = 1; f.mm = MouthMode::U; tg[K_MW] = 40; tg[K_MC] = 1.2f;
      tg[K_SK] = sinf(t * 2.5f) * 3;
      tg[K_LY] = sinf(t * 2.5f) * 4; tg[K_RY] = -tg[K_LY];
      if (t > E.n) {
        E.n = t + 0.6f;
        const bool left = rnd(0, 1) < 0.5f;
        // 開心：音符、愛心、星星輪流冒；直式從頭頂兩側、橫式從臉的兩側
        const float sx = land_sym() ? (left ? -100.0f : 100.0f) : (left ? rnd(-46, -26) : rnd(26, 46));
        const float sy = land_sym() ? rnd(-40, -10) : rnd(-74, -58);
        const Rgb cc = rnd(0, 1) < 0.5f ? MG : Rgb{0xFF, 0xE6, 0x00};
        const float k = rnd(0, 1);
        if (k < 0.4f) emit(PK::Note, 0, sx, sy, cc, 2.8f, rnd(-8, 8), -24, 1.4f);
        else if (k < 0.75f) emit(PK::Heart, 0, sx, sy, Rgb{0xFF, 0x4D, 0x8D}, 34, rnd(-8, 8), -24, 1.4f);
        else emit(PK::Spark, 0, sx, sy, cc, 36, rnd(-8, 8), -24, 1.2f);
      }
      break;
    }

    case Mood::Angry: {
      tg[K_ANG] = 1; tg[K_LH] = tg[K_RH] = 42;
      f.mm = MouthMode::Zig; tg[K_MW] = 34;
      tg[K_HX] = sinf(t * 38) * 0.8f;
      if (fmodf(t, 1.8f) < 0.35f) { tg[K_LW] = tg[K_RW] = 38; tg[K_LH] = tg[K_RH] = 50; tg[K_HY] = -5; }   // 往前衝
      break;
    }

    case Mood::Scared:
      tg[K_LW] = tg[K_RW] = 24; tg[K_LH] = tg[K_RH] = 34;
      if (s_pin) { f.mm = MouthMode::Wave; tg[K_MW] = 26; break; }   // 甩的時候交給彈珠物理，不疊自己的亂瞄／發抖
      tg[K_HX] = sinf(t * 55) * 1.7f;
      f.mm = MouthMode::Wave; tg[K_MW] = 26;
      if (t > E.n) { E.n = t + 0.45f; E.d = rnd(-6, 6); }
      tg[K_DX] = E.d;
      if (t > E.x2) { E.x2 = t + 2.6f; if (t > 0.5f) emit(PK::Text, PIXEL ? 'd' : '!', symX(), symY(), MG, 46); }
      break;

    case Mood::Sleepy: {
      tg[K_SLP] = 1;
      const float c = fmodf(t, 5.5f);
      const bool yawn_cycle = ((int)(t / 5.5f)) % 2 == 1;
      f.mm = MouthMode::Line; tg[K_MW] = 18; tg[K_MC] = 0;
      if (c < 3) {                       // 眼皮越來越重、頭往下沉
        tg[K_LH] = tg[K_RH] = 26 - c / 3 * 18; tg[K_HY] = c / 3 * 11;
      } else if (c < 3.35f) {            // 猛然驚醒
        if (!E.snap) { E.snap = true; s_sp[K_HY].v -= 170; emit(PK::Text, '!', symX(), symY(), MG, 42, 0, -10, 0.8f); }
        tg[K_LH] = tg[K_RH] = 34;
      } else {
        E.snap = false;
        tg[K_LH] = tg[K_RH] = 28;
        if (yawn_cycle && c > 3.7f && c < 5.2f) {   // 打哈欠
          f.mm = MouthMode::OFill; tg[K_MW] = 12; tg[K_MH] = 4 + 12 * sinf((c - 3.7f) / 1.5f * 3.14159f);
          tg[K_LH] = tg[K_RH] = 5;
          if (!E.yawn) { E.yawn = true; emit(PK::Dots, 0, symX(), symY() + 10, {0x2B, 0xB3, 0xA8}, 2.4f, 0, -8, 1.4f); }
        } else {
          E.yawn = false;
        }
      }
      break;
    }

    case Mood::Asleep:
      tg[K_LH] = tg[K_RH] = 5;
      tg[K_HY] = sinf(t * 1.4f) * 3;
      f.mm = MouthMode::Ring; tg[K_MW] = tg[K_MH] = 6 + 2 * sinf(t * 1.4f);
      if (t > E.n) { E.n = t + 1.2f; emit(PK::Text, 'z', symX() - 10 + rnd(-4, 4), symY() + 18, {127, 184, 180}, rnd(24, 38), 8, -16, 2.0f); }
      break;
  }
  return f;
}

}  // namespace

namespace ui_expression {

void begin() {
  s_cv.setPsram(true);
  s_cv.setColorDepth(16);
  s_cv.createSprite(M5.Display.width(), M5.Display.height());   // 135×240（螢幕 rotation 0）
  s_cv.setRotation(0);
  s_ready = (s_cv.getBuffer() != nullptr);
  if (PIXEL && s_ready) {   // 背景快取（再 64KB PSRAM）
    s_bg.setPsram(true);
    s_bg.setColorDepth(16);
    if (s_bg.createSprite(M5.Display.width(), M5.Display.height())) s_bg_bytes = (size_t)M5.Display.width() * M5.Display.height() * 2;
  }
  for (int i = 0; i < K_NP; ++i) s_sp[i] = {kDefault[i], 0, kDefault[i]};
  s_last_frame_ms = s_t0 = millis();
}

void setMood(Mood m) { s_mood = m; }
Mood mood() { return s_mood; }
const char* moodName() { return info(s_mood).name; }
const char* moodNameOf(Mood m) { return info(m).name; }
void setSpeakLevel(float level01) { s_speak_level = level01; }
void setListenLevel(float level01) { s_listen_level = level01; }

void setOffline(bool off) {
  if (off && !s_offline) {
    s_freeze_ms = millis();
    s_blink_end = 0;
  }
  s_offline = off;
}
bool offline() { return s_offline; }

void convBegin() {
  s_user_len = 0; s_user[0] = '\0';
  s_bot_len = 0; s_bot[0] = '\0';
  s_nlines = 0;
  s_conv_on = true;
  s_conv_hide_at = 0;
  s_conv_dirty = false;
}
void convText(bool assistant, const char* utf8) {
  if (!utf8 || !*utf8) return;
  if (assistant) appendText(s_bot, s_bot_len, BOT_CAP, utf8);
  else appendText(s_user, s_user_len, USER_CAP, utf8);
  s_conv_on = true;
  s_conv_dirty = true;
}
void convEnd() {
  s_conv_hide_at = millis() + ((s_user_len || s_bot_len) ? CONV_HOLD_MS : 0) + 1;
}

void setRestTint(bool enabled, uint8_t r, uint8_t g, uint8_t b) {
  s_tint_on = enabled;
  s_tint_r = r; s_tint_g = g; s_tint_b = b;
}
void setLowBatteryBlink(int mode) { s_low_blink = mode; }
void setGaze(float x, float y) {
  s_gaze_tx = x < -1 ? -1 : x > 1 ? 1 : x;
  s_gaze_ty = y < -1 ? -1 : y > 1 ? 1 : y;
}

void setMotion(float right_g, float down_g) { s_mot_x = right_g; s_mot_y = down_g; }
void setGravity(float right_g, float down_g) { s_gr_x = right_g; s_gr_y = down_g; }
bool shattered() { return s_fr_on; }
uint32_t takeLiquidMaxUs() { const uint32_t v = s_lq_us_max; s_lq_us_max = 0; return v; }
void setCharge(int state) { s_chg = state < 0 ? 0 : state > 2 ? 2 : state; }

void setOrientation(Orient o) {
  if (o == s_orient) return;
  s_orient = o;
  const uint8_t rot = o == Orient::Portrait ? 0 : o == Orient::LandALeft ? cfg::ORIENT_ROT_A_LEFT : cfg::ORIENT_ROT_A_RIGHT;
  s_cv.setRotation(rot);   // 只換畫布的畫圖座標；緩衝區不動
  s_conv_dirty = true;     // 對話區寬度變了，重新換行
}
Orient orientation() { return s_orient; }
const char* orientName(Orient o) {
  return o == Orient::Portrait ? "直式" : o == Orient::LandALeft ? "橫式(A 左)" : "橫式(A 右)";
}

void setPreview(int idx) { s_preview = (idx >= 0 && idx <= static_cast<int>(Mood::Asleep)) ? idx : -1; }
int preview() { return s_preview; }

void render() {
  if (!s_ready) return;
  // 斷線：時間停在定格那一刻
  const uint32_t now = s_offline ? s_freeze_ms : millis();
  float dt = (now > s_last_frame_ms) ? (now - s_last_frame_ms) / 1000.0f : 0.0f;
  if (dt > 0.05f) dt = 0.05f;
  s_last_frame_ms = now;

  const Mood shown = s_preview >= 0 ? static_cast<Mood>(s_preview) : s_mood;
  s_px_battery = battery_ui::level();   // A7：狀態列右邊的電量 %（平滑後；序列埠 c 假裝電量也看得到）
  if (shown != s_shown) enterMood(shown, now);
  const float t = (now - s_t0) / 1000.0f;

  // ── 目標值 → 彈簧 ──
  float tg[K_NP];
  const Frame fr = behave(shown, t, dt, now, tg);
  for (int i = 0; i < K_NP; ++i) {
    s_sp[i].t = tg[i];
    if (dt > 0) springStep(s_sp[i], dt, i == K_HY ? 160.0f : 230.0f, i == K_HY ? 9.0f : 15.0f);
  }
  s_prev_v = fr.v;

  // ── 眨眼（睡著不眨）──
  if (shown != Mood::Asleep && !s_offline && now > s_next_blink && now > s_blink_end) {
    s_blink_end = now + 120;
    s_next_blink = now + (rnd(0, 1) < 0.25f ? 250 : (uint32_t)rnd(1600, 3600));   // 偶爾連眨兩下
  }
  const bool blink = now < s_blink_end;
  const bool wink = now < s_wink_end;

  // ── 顏色：目標色（休息臉用電量色），平滑過渡 ──
  const MoodInfo& mi = info(shown);
  Rgb t1 = {(float)mi.r1, (float)mi.g1, (float)mi.b1}, t2 = {(float)mi.r2, (float)mi.g2, (float)mi.b2};
  if (s_tint_on && (shown == Mood::Idle || shown == Mood::Sleepy || shown == Mood::Asleep)) {
    const float dim = shown == Mood::Idle ? 1.0f : shown == Mood::Sleepy ? 0.75f : 0.45f;
    // 11-20 Miles「綠色還是有點暗、要更螢光」：原本下半部只有一半亮度（×0.5），眼睛一半是暗的、嘴巴也暗。
    // 改成霓虹燈管的樣子：上緣往白色推 30%（燈管發白的芯）、下緣 ×0.85（飽和色），光暈跟著變亮。
    const Rgb tint = {s_tint_r, s_tint_g, s_tint_b};
    const Rgb core = mixc(tint, Rgb{255, 255, 255}, 0.30f);
    t1 = {core.r * dim, core.g * dim, core.b * dim};
    t2 = {s_tint_r * dim * 0.85f, s_tint_g * dim * 0.85f, s_tint_b * dim * 0.85f};
  }
  float ck = dt * 10.0f;
  if (ck > 1) ck = 1;
  s_c1 = mixc(s_c1, t1, ck);
  s_c2 = mixc(s_c2, t2, ck);
  Rgb c1 = s_c1, c2 = s_c2;
  if (s_offline) { c1 = {OFF_R, OFF_G, OFF_B}; c2 = {OFF_R * 0.6f, OFF_G * 0.6f, OFF_B * 0.6f}; }
  else if (s_low_blink && shown != Mood::Listening && shown != Mood::Thinking && shown != Mood::Speaking) {
    c1 = lowBlinkColor(c1);
    c2 = PIXEL ? c1 : mixc(BG, c1, 0.6f);
  }

  // ── 對話區展開／收起（真實時間）──
  {
    const uint32_t rn = millis();
    if (s_conv_on && s_conv_hide_at && (int32_t)(rn - s_conv_hide_at) >= 0) { s_conv_on = false; s_conv_hide_at = 0; }
    const float tgt = s_conv_on ? 1.0f : 0.0f;
    static uint32_t s_conv_last = 0;
    const uint32_t cdt = s_conv_last ? rn - s_conv_last : 0;
    s_conv_last = rn;
    float kk = cdt * 0.012f;
    if (kk > 1) kk = 1;
    s_conv_k += (tgt - s_conv_k) * kk;
    if (fabsf(tgt - s_conv_k) < 0.01f) s_conv_k = tgt;
  }

  // ── 版面：臉的區域避開 A 鍵側的對話區 ──
  const int W = s_cv.width(), H = s_cv.height();
  const bool land = landscape();
  const int cs = (int)(convExtent(true) * s_conv_k + 0.5f);   // A7：對話框一律 EVA 版
  int fx0 = 0, fw = W, fh = H;
  if (!land) fh = H - cs;
  else { fw = W - cs; if (s_orient == Orient::LandALeft) fx0 = cs; }
  const float sc = land ? cfg::FACE_LAND_SCALE : 1.0f;
  const float fcx = fx0 + fw * 0.5f;
  const float fcy = land ? H * 0.40f : fh * (0.42f + 0.06f * s_conv_k);
  const int hz = land ? (int)(H * 0.86f) : 190;   // 地平線
  s_face_l = (float)fx0; s_face_r = (float)(fx0 + fw);   // 符號左右不要超出臉的區域（橫式對話區打開時臉會變窄）
  s_sym_top = 0;
  if (PIXEL) { s_face_l = fx0 + fw * (land ? 0.76f : 0.5f); s_sym_top = 13 + 4; }   // 像素臉：符號只在右半邊、狀態列以下（左上留給狀態大字）
  auto X = [&](float d) { return fcx + d * sc; };
  auto Y = [&](float d) { return fcy + d * sc; };

  // 看方向（睡著不動）
  {
    float gk = dt * 1000.0f * GAZE_EASE_PER_MS;
    if (gk > 1) gk = 1;
    const bool asleep = shown == Mood::Asleep;
    s_gaze_x += ((asleep ? 0 : s_gaze_tx) - s_gaze_x) * gk;
    s_gaze_y += ((asleep ? 0 : s_gaze_ty) - s_gaze_y) * gk;
  }
  // 看方向的位移（設計單位；X()/Y() 會再乘 sc → 橫式先除掉 sc，像素量不縮水）
  const float gdx = s_gaze_x * cfg::PET_GAZE_MAX_X_PX * (land ? cfg::PET_GAZE_LAND_X_GAIN / sc : 1.0f);
  const float gdy = s_gaze_y * cfg::PET_GAZE_MAX_Y_PX / sc;
  const float hx = P(K_HX), hy = P(K_HY), dx = P(K_DX), dy = P(K_DY);

  // ── 繪製 ──
  const PxTheme th = pxTheme(shown, s_offline);
  const int top = 13;                                         // 狀態列下面
  const int fy1 = land ? H : fh;                              // 臉的區域下緣
  if (PIXEL) bufMapProbe();   // A4b：液體快速填色要的座標對應（轉向時才重探；之後背景會整張蓋回）
  if (!PIXEL || !pxBackground(W, H, fx0, top, fw, fy1, fade(th.accent, 0.22f), fade(th.accent, 0.16f))) {
    s_cv.fillScreen(c565(BG));
    if (PIXEL) {   // 背景快取開不出來（PSRAM 不夠）時照舊每幀畫
      s_cv.setClipRect(fx0, top, fw, fy1 - top);
      pxHexGrid(s_cv, W, H, fade(th.accent, 0.22f));
      pxWatermark(s_cv, fx0, top, fw, fy1, fade(th.accent, 0.16f));
      s_cv.clearClipRect();
    }
  }

  // 地平線霓虹網格（往前捲動，速度跟情緒與音量）
  if (!PIXEL) {
    s_grid_off += dt * mi.grid * (1 + fr.v);
    if (s_grid_off > 1000) s_grid_off -= 1000;
    // 11-20 Miles「最上面那條橫線太明顯」：地平線原本 2px 全亮洋紅 → 1px、40%；
    // 橫線改成「越遠越暗、越近越亮」（跟資訊頁快訊的地平線一樣），有景深、不搶眼睛
    s_cv.drawFastHLine(0, hz, W, fade(MG, 0.40f));
    s_cv.setClipRect(0, hz + 1, W, H - hz - 1);
    for (int k = -3; k <= 3; ++k) s_cv.drawLine((int32_t)fcx, hz, (int32_t)(fcx + k * fw * 0.42f), H, fade(MG, 0.35f));
    const float step = 12 * sc;
    const float depth = (float)(H - hz);
    for (float y = hz + 6 * sc + fmodf(s_grid_off * sc, step); y < H; y += step)
      s_cv.drawFastHLine(0, (int32_t)y, W, fade(MG, 0.18f + 0.40f * (y - hz) / depth));
    s_cv.clearClipRect();
  }

  // 聆聽：耳朵＋聲波（耳朵從畫面邊緣彈進來）
  const int es = earSide();
  if (PIXEL) { c1 = th.eye; c2 = th.eye; }   // 像素臉：五官、符號一律淡琥珀（警報時框與格子變紅）
  if (PIXEL && !s_offline && s_low_blink && shown != Mood::Listening && shown != Mood::Thinking && shown != Mood::Speaking)
    c1 = c2 = lowBlinkColor(th.eye);   // 11-21：低電量五官閃爍（語音中不閃）
  if (P(K_ED) > -40) {
    const float ex = X(es * (49 - P(K_ED)) + hx), ey = Y(3 + hy);
    const float wave_cx = X(es * (53 - P(K_ED)) + hx);
    for (int i = 0; i < 3; ++i) {
      const float a = fr.v > 0.06f ? fr.v * (0.5f + 0.5f * sinf(t * 14 - i * 1.6f)) : 0.15f + 0.1f * sinf(t * 3 - i);
      const float r = (24 + i * 9) * sc;
      const uint16_t wc = fade(i == 1 ? c1 : c2, a);
      if (es < 0) s_cv.fillArc((int32_t)wave_cx, (int32_t)ey, (int32_t)(r - 1.5f), (int32_t)(r + 1.5f), 150, 210, wc);
      else {
        s_cv.fillArc((int32_t)wave_cx, (int32_t)ey, (int32_t)(r - 1.5f), (int32_t)(r + 1.5f), 330, 360, wc);
        s_cv.fillArc((int32_t)wave_cx, (int32_t)ey, (int32_t)(r - 1.5f), (int32_t)(r + 1.5f), 0, 30, wc);
      }
    }
    const float o = -es * sc;   // 往臉的方向
    quad(ex + 8 * o, ey - 24 * sc, ex - 14 * o, ey - 26 * sc, ex - 12 * o, ey, 5 * sc, c565(c2));
    quad(ex - 12 * o, ey, ex - 10 * o, ey + 22 * sc, ex + 6 * o, ey + 24 * sc, 5 * sc, c565(c2));
    quad(ex + 2 * o, ey - 10 * sc, ex - 5 * o, ey - 2 * sc, ex + 1 * o, ey + 9 * sc, 3 * sc, c565(c1));
  }

  // ── 驚嚇彈珠台：甩的時候三顆在臉的區域裡撞來撞去，停手 0.4 秒後彈簧歸位 ──
  const float mhx = fr.mouth_dx + dx * 0.5f + hx + gdx * cfg::PET_GAZE_MOUTH_FOLLOW;
  const float mhy = 51 + hy + dy * 0.3f + gdy * cfg::PET_GAZE_MOUTH_FOLLOW;
  {
    const float mag = sqrtf(s_mot_x * s_mot_x + s_mot_y * s_mot_y);
    if (!s_offline) {
      if (mag >= cfg::PIN_START_G) { s_pin = true; s_pin_last = now; }
      else if (s_pin && mag >= cfg::PIN_QUIET_G) s_pin_last = now;
      else if (s_pin && now - s_pin_last > cfg::PIN_QUIET_MS) s_pin = false;
    }
    // 牆＝臉的區域四邊（設計單位，相對臉中心）
    const float wl = (fx0 - fcx) / sc + 2, wr = (fx0 + fw - fcx) / sc - 2;
    const float wt = -fcy / sc + 2, wb = (fh - fcy) / sc - 2;
    pinStep(s_bd[0], 0, dt, P(K_LX) + dx + hx + gdx, P(K_LY) + dy + hy + gdy, P(K_LW) / 2 + 4, P(K_LH) / 2 + 4, wl, wr, wt, wb);
    pinStep(s_bd[1], 1, dt, P(K_RX) + dx + hx + gdx, P(K_RY) + dy + hy + gdy, P(K_RW) / 2 + 4, P(K_RH) / 2 + 4, wl, wr, wt, wb);
    pinStep(s_bd[2], 2, dt, mhx, mhy, P(K_MW) / 2 + 3, fmaxf(2, P(K_MH)) / 2 + 3, wl, wr, wt, wb);
  }

  // ── A4b 充電臉：休息臉（待機／想睡／睡著）換成背景液體＋充電標籤；語音、情緒反應優先 ──
  // 液體只要插著線就一直在（搖晃、說話時照樣跟著流）；標籤、徽章、笑眼只在休息臉
  const bool chgLq = s_chg && !s_offline;
  const bool chg = chgLq && (shown == Mood::Idle || shown == Mood::Sleepy || shown == Mood::Asleep);
  s_status_ovr = chg ? (s_chg == 2 ? "充飽" : "充電中") : nullptr;
  {
    const uint32_t lq_t0 = micros();
    lqTables();
    s_lq_x0 = fx0 + 2; s_lq_x1 = fx0 + fw - 2; s_lq_y0 = top + 1; s_lq_y1 = fy1 - 1;
    const int ckey = ((s_lq_x0 * 251 + s_lq_x1) * 251 + s_lq_y0) * 251 + s_lq_y1;
    if (ckey != s_lq_key) { s_lq_key = ckey; lqRebuild(); if (s_lq_on) lqPrune(); }   // 轉向、對話框開合
    if (chgLq) {
      if (!s_lq_on) { lqClear(); s_lq_on = true; s_lq_next = now; }
      s_lq_off_t = 0;
    } else if (s_lq_on && !s_lq_off_t) { const uint32_t m = millis(); s_lq_off_t = m ? m : 1; }
    // 淡出用真實時間（29-a4g：斷線時 now 停在定格那一刻 → 淡出永遠走不完、液體卡在畫面上）
    const float alpha = s_lq_off_t ? 1.0f - (float)(millis() - s_lq_off_t) / cfg::LQ_FADE_MS : 1.0f;
    if (s_lq_on && alpha <= 0) { s_lq_on = false; lqClear(); }
    if (s_lq_on) {
      if ((int32_t)(now - s_lq_next) > 200) s_lq_next = now;   // 斷了很久（換頁回來）：不要一次補走很多步
      {   // 參考重力：方向變超過約 8°（或從平放立起來）才更新 → 亮波位置、側滑開關不受讀數雜訊影響
        const float gm0 = sqrtf(s_gr_x * s_gr_x + s_gr_y * s_gr_y);
        if (gm0 > 0.15f) {
          const float nx = s_gr_x / gm0, ny = s_gr_y / gm0;
          if (nx * s_lq_grx + ny * s_lq_gry < 0.99f) { s_lq_grx = nx; s_lq_gry = ny; s_lq_gstable_t = now; }
        }
      }
      if ((int32_t)(now - s_lq_next) >= 0) {   // 每幀最多走一步（慢的幀不補走，免得一幀卡更久）
        if (chgLq) {
          const int cap = s_lq_cap;
          const float lv = s_chg == 2 ? 1.0f : (s_px_battery < 0 ? 0.5f : s_px_battery / 100.0f);
          // 電量 0～100% → 容器 8%～92%（充飽滿到只剩一點空間；充電中跟著電量越來越滿）
          lqFill((int)lroundf(cap * (cfg::LQ_FILL_MIN + (cfg::LQ_FILL_FULL - cfg::LQ_FILL_MIN) * fminf(1.0f, fmaxf(0.0f, lv)))));
        }
        lqStep(s_pin || now - s_lq_gstable_t < cfg::LQ_SETTLE_MS);
        s_lq_next = now + (s_pin ? cfg::LQ_STEP_SHAKE_MS : cfg::LQ_STEP_MS);
      }
      // 亮度：充電中一道亮波從底部往液面爬（每 LQ_WAVE_MS 才變一次、用參考重力算位置）；
      // 充飽整片較亮，每 400ms 只換一顆亮格（11-21 實機：原本每 0.5 秒換一批 → 整片重送、每秒一次 37ms）
      const float tq = (float)(now / cfg::LQ_WAVE_MS * cfg::LQ_WAVE_MS) / 1000.0f;
      const float wave = 260 - fmodf(tq * 160, 300);
      const int spark = s_lq_n ? (int)((now / 400) * 2654435761u % (uint32_t)s_lq_n) : -1;
      for (int i = 0; i < s_lq_n; ++i) {
        const LqCell& q = s_lq[i];
        float b;
        if (s_chg == 2) b = i == spark ? 0.75f : 0.55f;
        else {
          const float depth = lqX(q.c) * s_lq_grx + lqY(q.c, q.k) * s_lq_gry;   // 越大越「下面」
          b = 0.34f + 0.45f * fmaxf(0, 1 - fabsf(depth - wave) / 14);
        }
        fillHexFast(lqX(q.c), lqY(q.c, q.k), HEX_A - 1.5f, fade(th.accent, b * fmaxf(0, alpha)));
      }
    }
    const uint32_t lq_us = micros() - lq_t0;   // j 量測行的「液體≤」
    if (lq_us > s_lq_us_max) s_lq_us_max = lq_us;
  }

  // 眼睛
  const float lx = X(P(K_LX) + dx + hx + gdx + s_bd[0].ox), ly = Y(P(K_LY) + dy + hy + gdy + s_bd[0].oy);
  const float rx = X(P(K_RX) + dx + hx + gdx + s_bd[1].ox), ry = Y(P(K_RY) + dy + hy + gdy + s_bd[1].oy);
  const float lw = P(K_LW) * sc * s_bd[0].sx, rw = P(K_RW) * sc * s_bd[1].sx;
  const float lh = (blink ? 4 : fmaxf(4, P(K_LH))) * sc * s_bd[0].sy;
  const float rh = ((blink || wink) ? 4 : fmaxf(4, P(K_RH))) * sc * s_bd[1].sy;
  // ── A8 甩碎：彈珠台一觸發就炸開（用這一幀的五官位置）；彈珠台結束就拼回 ──
  {
    const float fmx = X(mhx + s_bd[2].ox), fmy = Y(mhy + s_bd[2].oy);
    s_fr_home[0][0] = lx; s_fr_home[0][1] = ly;
    s_fr_home[1][0] = rx; s_fr_home[1][1] = ry;
    s_fr_home[2][0] = fmx; s_fr_home[2][1] = fmy;
    s_fr_wl = fx0 + 3; s_fr_wr = fx0 + fw - 3; s_fr_wt = top + 2; s_fr_wb = fy1 - 2;
    if (PIXEL && s_pin && !s_fr_on && !s_offline) {
      s_fr_n = 0; s_fr_on = true; s_fr_back = false; s_fr_calm_t = 0;
      frSpawn(0, lx, ly, lw, lh, false);
      frSpawn(1, rx, ry, rw, rh, false);
      frSpawn(2, fmx, fmy, P(K_MW) * sc, fmaxf(4, P(K_MH)) * sc + 4, false);
      for (int i = 0; i < MAX_PARTS; ++i) {   // 畫面上的符號一起炸
        Part& p = s_parts[i];
        if (!p.on || p.kind == PK::Dot) continue;
        frBadge(X(p.x), Y(p.y));
        p.on = false;
      }
      if (shown == Mood::Angry) frBadge(X(symX()), Y(symY() + 4 + hy));
    }
    if (s_fr_on) {
      // 停手（彈珠台結束）後碎片照重力散著 FR_HOLD_MS 才拼回；語音一開始就立刻拼回
      const bool voice = shown == Mood::Listening || shown == Mood::Thinking || shown == Mood::Speaking;
      if (s_pin) { s_fr_back = false; s_fr_calm_t = 0; }                // 又開始甩
      else if (!s_fr_back) {
        if (!s_fr_calm_t) s_fr_calm_t = now ? now : 1;
        if (voice || now - s_fr_calm_t >= cfg::FR_HOLD_MS) { s_fr_back = true; s_fr_back_t = now; }
      }
      frPhysics(dt);
      if (s_fr_back) {
        float worst = 0;
        for (int i = 0; i < s_fr_n; ++i) {
          const Frag& f = s_fr[i];
          if (f.feat < 3) worst = fmaxf(worst, fabsf(f.x - s_fr_home[f.feat][0] - f.ox) + fabsf(f.y - s_fr_home[f.feat][1] - f.oy));
        }
        if ((worst < 3.0f && now - s_fr_back_t > 250) || now - s_fr_back_t > cfg::FR_RETURN_MAX_MS) s_fr_on = false;
      }
    }
    if (s_fr_on) {
      Rgb ec = c1;
      if (shown == Mood::Asleep) ec = mixc(BG, ec, 0.55f);
      frDraw(c565(ec), c565(c2), th.accent, s_fr_back ? 1.0f - (now - s_fr_back_t) / 300.0f : 1.0f);
    }
  }
  if (PIXEL && !s_fr_on) {
    Rgb ec = c1;   // 平常＝th.eye；低電量時＝閃爍色（眼睛、嘴巴一起閃）
    if (shown == Mood::Asleep) ec = mixc(BG, ec, 0.55f);
    const float hap = (chg && s_chg == 2) ? 1.0f : P(K_HAP);   // A4b：充飽＝笑眼
    pxEye(lx, ly, lw, lh, -1, P(K_ANG), P(K_SLP), hap, sc, c565(ec));
    pxEye(rx, ry, rw, rh, +1, P(K_ANG), P(K_SLP), hap, sc, c565(ec));
  } else if (!PIXEL) {
    drawEye(lx, ly, lw, lh, c1, c2, -1, P(K_ANG), P(K_SLP), P(K_HAP), sc);
    drawEye(rx, ry, rw, rh, c1, c2, +1, P(K_ANG), P(K_SLP), P(K_HAP), sc);
  }

  // 眉毛
  if (P(K_BRA) > 0.05f && fr.brows && !s_fr_on) {
    const uint16_t bc = fade(c1, fminf(1, P(K_BRA)));
    if (fr.brows == 1) {   // 聆聽：只有遠側那隻挑起來
      const bool far_right = es < 0;
      const float bx = far_right ? rx : lx, bt = (far_right ? ry - rh / 2 : ly - lh / 2) - 12 * sc - P(K_BRR) * sc;
      const float fsgn = far_right ? 1 : -1;
      s_cv.drawWideLine((int32_t)(bx - 14 * sc * fsgn), (int32_t)bt, (int32_t)(bx + 14 * sc * fsgn),
                        (int32_t)(bt - 8 * sc - P(K_BRR) * 0.3f * sc), 2.0f * sc, bc);
    } else {
      const float lt = ly - lh / 2 - 10 * sc - P(K_BRL) * sc, rt = ry - rh / 2 - 10 * sc - P(K_BRR) * sc;
      s_cv.drawWideLine((int32_t)(lx - 13 * sc), (int32_t)lt, (int32_t)(lx + 11 * sc), (int32_t)(lt - 3 * sc), 2.0f * sc, bc);
      s_cv.drawWideLine((int32_t)(rx - 11 * sc), (int32_t)(rt - 3 * sc), (int32_t)(rx + 13 * sc), (int32_t)rt, 2.0f * sc, bc);
    }
  }

  // 驚嚇：冷汗（11-20 Miles：太小看不出來 → 改成漫畫式的大汗滴，壓在右眼右上角、慢慢往下滑）
  // A7：像素臉不再把汗滴掛在眼角（改成符號角的水滴徽章）



  // 嘴巴（像素臉：一樣的嘴型，畫完再挖掃描線縫）；甩碎中不畫
  if (!s_fr_on) {
    const float mx = X(mhx + s_bd[2].ox), my = Y(mhy + s_bd[2].oy);
    const float mw = P(K_MW) * sc * s_bd[2].sx, mh = fmaxf(2, P(K_MH)) * sc * s_bd[2].sy, mc = P(K_MC), sk = P(K_SK) * sc;
    const uint16_t mcol = c565(c2);
    switch (fr.mm) {
      case MouthMode::Line:
      case MouthMode::U: {
        const bool u = fr.mm == MouthMode::U;
        quad(mx - mw / 2, my - sk, mx, my + mc * mw * 0.4f, mx + mw / 2, my + sk - (u ? 4 : 2) * sc, (u ? 6 : 4) * sc, mcol);
        break;
      }
      case MouthMode::Smirk: {
        const float s = (float)-es;   // 歪向遠離耳朵的那邊
        quad(mx - 11 * sc * s, my + 1 * sc, mx, my + 5 * sc, mx + 11 * sc * s, my - 6 * sc, 4 * sc, mcol);
        break;
      }
      case MouthMode::O: {
        const float v = fr.v;
        const float rxo = fmaxf(3, mw / 2 * (1.1f - 0.2f * v)), ryo = fmaxf(3, mh / 2 * (0.9f + 0.3f * v));
        s_cv.fillEllipse((int32_t)mx, (int32_t)my, (int32_t)(rxo + 2 * sc), (int32_t)(ryo + 2 * sc), mcol);
        s_cv.fillEllipse((int32_t)mx, (int32_t)my, (int32_t)fmaxf(1, rxo - 2 * sc), (int32_t)fmaxf(1, ryo - 2 * sc), c565(BG));
        break;
      }
      case MouthMode::Ring: {
        const float r = fmaxf(2, mw / 2);
        s_cv.fillCircle((int32_t)mx, (int32_t)my, (int32_t)(r + 1.5f * sc), mcol);
        s_cv.fillCircle((int32_t)mx, (int32_t)my, (int32_t)fmaxf(1, r - 1.5f * sc), c565(BG));
        // 鼻涕泡泡：變大後破掉
        const float bc = fmodf(t, 6.0f);
        if (bc < 2.6f) {
          s_ev.popped = false;
          const float br = (1 + bc / 2.6f * 9) * sc;
          drawHexCell(mx + (10 + bc * 2) * sc, my - (6 + bc * 2) * sc, br * 1.1f, c565(th.eye));
        } else if (!s_ev.popped && !s_offline) {
          s_ev.popped = true;
          for (int i = 0; i < 4; ++i)
            emit(PK::Dot, 0, fr.mouth_dx + 16, 39, {127, 184, 180}, 1.0f, rnd(-20, 20), rnd(-20, 5), 0.5f);
        }
        break;
      }
      case MouthMode::Open: {
        s_cv.fillSmoothRoundRect((int32_t)(mx - mw / 2), (int32_t)(my - mh / 2), (int32_t)mw, (int32_t)mh,
                                 (int32_t)fminf(7 * sc, mh / 2), mcol);
        if (mh > 14 * sc)   // 舌頭
          s_cv.fillEllipse((int32_t)mx, (int32_t)(my + mh / 2 - 4 * sc), (int32_t)(mw * 0.25f), (int32_t)(3.5f * sc), PIXEL ? c565(th.accent) : c565(MG));
        break;
      }
      case MouthMode::OFill:
        s_cv.fillEllipse((int32_t)mx, (int32_t)my, (int32_t)(mw / 2), (int32_t)fmaxf(1, mh / 2), mcol);
        break;
      case MouthMode::Wave: {
        const bool scared = shown == Mood::Scared;
        const float amp = (scared ? 2.5f : 3.0f) * sc, sp = scared ? 30.0f : 4.0f;
        float px = mx - mw / 2, py = my + sinf(t * sp) * amp;
        for (float i = 2 * sc; i <= mw; i += 2 * sc) {
          const float x = mx - mw / 2 + i, y = my + sinf(i / (4 * sc) + t * sp) * amp;
          s_cv.drawWideLine((int32_t)px, (int32_t)py, (int32_t)x, (int32_t)y, 2.0f * sc, mcol);
          px = x; py = y;
        }
        break;
      }
      case MouthMode::Zig: {
        const float g = (((int)(t * 9)) % 2 ? 2 : -2) * sc;   // 磨牙
        float px = mx - mw / 2 + g, py = my + 3 * sc;
        for (int i = 1; i <= 6; ++i) {
          const float x = mx - mw / 2 + i * mw / 6 + g, y = my + (i % 2 ? -4 : 3) * sc;
          s_cv.drawWideLine((int32_t)px, (int32_t)py, (int32_t)x, (int32_t)y, 2.0f * sc, mcol);
          px = x; py = y;
        }
        break;
      }
    }
  }

  if (PIXEL && !s_fr_on) {   // 嘴巴挖掃描線縫（只挖嘴巴顏色的像素，六角格不受影響）
    const float mx = X(mhx + s_bd[2].ox), my = Y(mhy + s_bd[2].oy);
    const float mw = P(K_MW) * sc * s_bd[2].sx + 12 * sc, mh = fmaxf(2, P(K_MH)) * sc * s_bd[2].sy + 16 * sc;
    const uint16_t mc = c565(c2);
    const int y0 = (int)(my - mh / 2), y1 = (int)(my + mh / 2), x0 = (int)(mx - mw / 2), x1 = (int)(mx + mw / 2);
    // 一列一次 readRect（原本逐點 readPixel 約佔一幀 20%），連續的嘴巴色整段塗黑
    const int cx0 = x0 < 0 ? 0 : x0, cx1 = x1 >= s_cv.width() ? s_cv.width() - 1 : x1;
    lgfx::rgb565_t row[256];
    for (int y = y0; y <= y1 && cx1 >= cx0 && cx1 - cx0 < 256; ++y) {
      if (((y - y0) % 3) != 2 || y < 0 || y >= s_cv.height()) continue;
      s_cv.readRect(cx0, y, cx1 - cx0 + 1, 1, row);
      for (int i = 0, n = cx1 - cx0 + 1; i < n;) {
        if (row[i].raw != mc) { ++i; continue; }
        int j = i;
        while (j < n && row[j].raw == mc) ++j;
        s_cv.drawFastHLine(cx0 + i, y, j - i, TFT_BLACK);
        i = j;
      }
    }
  }

  // 思考：右上思考泡泡（三點輪流亮）
  if (shown == Mood::Thinking) {   // A7：三格輪流亮（MAGI），沿格子往右上排
    const int act = ((int)(t * 3)) % 3;
    float bx = X(30), by = Y(-36 + hy);
    hexSnap(bx, by);
    for (int i = 0; i < 3; ++i) {
      const float cx = bx + i * HEX_DX, cy = by - i * HEX_HH;
      fillHexCell(cx, cy, HEX_A - 1.0f, i == act ? c565(th.accent) : fade(th.accent, 0.25f));
    }
  }

  // 生氣：# 青筋跳動
  if (chg && !s_fr_on) {   // A4b：充電中閃電（亮度呼吸）、充飽星芒
    float bx = X(symX()), by = Y(symY() + 4);
    badgeFit(bx, by);
    const float pulse = s_chg == 1 ? 0.6f + 0.4f * (0.5f + 0.5f * sinf(t * 2.6f)) : 1.0f;
    drawBadge(s_chg == 2 ? HB::Spark : HB::Bolt, bx, by, pulse, 1.0f, th.accent, th.eye);
  }
  if (shown == Mood::Angry && !s_fr_on) {   // A7：紅色六角「! WARNING」，跟節奏一閃一閃
    float ax = X(symX()), ay = Y(symY() + 4 + hy);
    badgeFit(ax, ay);
    const float beat = fmaxf(0, sinf(t * 9));
    drawBadge(HB::Warn, ax, ay, 0.75f + 0.25f * beat, 1 + 0.08f * beat, th.accent, th.eye);
  }


  // 小符號（彈出放大再縮回、往上飄、淡出）
  s_cv.setFont(&fonts::efontTW_12);
  s_cv.setTextDatum(middle_center);
  for (int i = 0; i < MAX_PARTS; ++i) {
    Part& p = s_parts[i];
    if (!p.on) continue;
    p.age += dt;

    if (p.age >= p.life) { p.on = false; continue; }
    p.x += p.vx * dt;
    p.y += p.vy * dt;
    const Rgb pcol = PIXEL ? th.eye : p.col;   // 像素臉：符號一律淡琥珀、不加光暈
    const uint16_t pc = fade(pcol, 1 - p.age / p.life);
    const float px0 = X(p.x), py0 = Y(p.y), py = py0, px = px0;
    switch (p.kind) {
      case PK::Text: case PK::Heart: case PK::Spark: case PK::Note: case PK::Dots: {
        if (PIXEL && s_fr_on) {   // A8：甩碎中冒出來的符號直接炸成碎片（拼回中就不冒）
          if (!s_fr_back) frBadge(px0, py0);
          p.on = false;
          break;
        }
        if (PIXEL) {
          HB b = p.kind == PK::Heart ? HB::Heart : p.kind == PK::Spark ? HB::Spark : p.kind == PK::Note ? HB::Note
               : p.kind == PK::Dots ? HB::Dots : p.ch == '?' ? HB::Ques : p.ch == 'z' ? HB::Zed : p.ch == 'd' ? HB::Drop : HB::Bang;
          const float al = 1 - p.age / p.life;
          // 疊影：0.1／0.2 秒前的位置（吸附到格子）畫外框殘影，跟現在同一格就不畫
          float bx = px0, by = py0;
          badgeFit(bx, by);
          for (int g = 2; g >= 1; --g) {
            const float ga = p.age - g * 0.10f;
            if (ga < 0) continue;
            float gx = X(p.x - p.vx * g * 0.10f), gy = Y(p.y - p.vy * g * 0.10f);
            badgeFit(gx, gy);
            if (gx == bx && gy == by) continue;
            drawHexCell(gx, gy, BADGE_A, fade(th.accent, al * (g == 1 ? 0.45f : 0.22f)));
          }
          // 疊影：出場後一圈外框往外擴散、淡掉（0.05～0.4 秒）
          if (p.age > 0.05f && p.age < 0.4f) {
            const float e = (p.age - 0.05f) / 0.35f;
            drawHexCell(bx, by, BADGE_A * (1.1f + 0.5f * e), fade(th.accent, al * 0.6f * (1 - e)));
            if (e > 0.2f) drawHexCell(bx, by, BADGE_A * (1.0f + 0.5f * (e - 0.2f)), fade(th.accent, al * 0.3f * (1 - e)));
          }
          // 出場：前 0.18 秒閃三下，彈簧放大（衝過頭約 20% 再回彈）
          if (p.age < 0.18f && ((int)(p.age * 33)) % 2 == 1) break;
          const float sp = 1 - expf(-p.age * 12) * cosf(p.age * 26);
          drawBadge(b, bx, by, al, fmaxf(0.3f, sp), th.accent, th.eye);
          break;
        }
        break;
      }
      case PK::Dot: s_cv.fillCircle((int32_t)px, (int32_t)py, (int32_t)fmaxf(1, 1.5f * sc), pc); break;
    }
  }
  s_cv.setTextSize(1);
  s_cv.setFont(&fonts::Font0);

  if (PIXEL) {
    // 強度方格：待機呼吸 3～4 格、聆聽／說話跟音量、警報滿格閃、睡著 1 格
    float lv = 0.35f + 0.08f * sinf(t * 2.2f);
    if (shown == Mood::Listening || shown == Mood::Speaking) lv = 0.12f + 0.88f * fr.v;
    else if (shown == Mood::Angry || shown == Mood::Scared) lv = 0.85f + 0.15f * sinf(t * 12);
    else if (shown == Mood::Asleep || shown == Mood::Sleepy) lv = 0.1f;
    else if (shown == Mood::Happy || shown == Mood::Thinking) lv = 0.5f + 0.2f * sinf(t * 4);
    const int total = 8, n = (int)lroundf(lv * total);
    pxMeter(fx0 + fw - 13, fy1 - 14, total, n, c565(th.accent), 0);
    pxBrackets(fx0 + 4, top + 4, fw - 8, fy1 - top - 8, c565(th.accent));
    // 左上狀態大字最後畫、底下先塗黑：表情不會蓋到字（Miles 11-20）
    // 11-21 Miles：狀態大字改成「有底色的標籤」（跟內部／外部框一樣：實心底＋黑字），蓋到表情也不突兀
    if (const ui_labels::Bmp* lab = ui_labels::find(chg ? (s_chg == 2 ? "st_full" : "st_charge") : kPxLabel[static_cast<int>(shown)])) {
      const int bx = fx0 + 8, by = top + 6, bw = lab->w + 6, bh = lab->h + 4;
      s_cv.fillRect(bx, by, bw, bh, c565(th.accent));
      s_cv.drawBitmap(bx + 3, by + 2, lab->bits, lab->w, lab->h, TFT_BLACK);
    }
    const int lvB = s_px_battery;
    const uint16_t bc = lvB < 0 ? fade(th.accent, 0.6f) : lvB >= 60 ? s_cv.color565(0x39, 0xFF, 0x14) : lvB >= 20 ? s_cv.color565(0xFF, 0xD0, 0x00) : s_cv.color565(0xFF, 0x29, 0x23);
    pxHeader(fx0, fw, shown, th.accent, bc);
  }
  drawConv(W, H, true);    // A7：對話框兩版都用 EVA「受信」版


  ui_fx::present(&s_cv);   // A5-1：換整頁時由 ui_fx 交叉淡化
}

M5Canvas* canvas() { return &s_cv; }

}  // namespace ui_expression
