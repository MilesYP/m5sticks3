#include "ui_expression.h"

#include <M5Unified.h>
#include <math.h>
#include <string.h>

#include "config.h"

// ─────────────────────────────────────────────────────────────────────────
//  座標約定：表情用「設計單位」描述（直式 135×240 時 1 單位＝1px），相對於臉的中心點；
//  畫的時候乘上比例 sc（直式 1.0、橫式 0.8）再加上臉中心的像素座標。
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

constexpr uint8_t BG_R = 0x0A, BG_G = 0x04, BG_B = 0x20;   // 深藍紫底
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
bool  s_low_blink = false;
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
enum class PK : uint8_t { Text, Note, Dots, Dot };
struct Part { bool on; PK kind; char ch; float x, y, vx, vy, age, life, size; Rgb col; };
constexpr int MAX_PARTS = 6;
Part s_parts[MAX_PARTS];
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

void addSeg(const char* text, size_t len, bool bot, int maxw) {
  if (landscape()) {
    pushLine(text, 0, bot, true);            // 橫式欄太窄：前綴自己一行
    wrapInto(text, len, bot, maxw, 0);
  } else {
    const int start = s_nlines;
    wrapInto(text, len, bot, maxw, s_cv.textWidth(bot ? PFX_BOT : PFX_USER));
    if (s_nlines > start) s_lines[start].pfx = true;
  }
}

void rewrap() {
  s_cv.setFont(&fonts::efontTW_12);
  s_cv.setTextSize(1);
  const int maxw = (landscape() ? convHeight() : s_cv.width()) - CONV_PAD * 2;
  s_nlines = 0;
  if (s_user_len) addSeg(s_user, s_user_len, false, maxw);
  if (s_bot_len) addSeg(s_bot, s_bot_len, true, maxw);
  s_cv.setFont(&fonts::Font0);
  s_conv_dirty = false;
}

// 對話區：直式＝畫面下方橫條；橫式＝A 鍵側直欄
void drawConv(int W, int H) {
  const int cs = (int)(convHeight() * s_conv_k + 0.5f);
  if (cs <= 0) return;
  const bool land = landscape();
  int rx, ry, rw, rh;
  if (!land) { rx = 0; ry = H - cs; rw = W; rh = cs; }
  else { rw = cs; rh = H; ry = 0; rx = (s_orient == Orient::LandALeft) ? 0 : W - cs; }
  s_cv.fillRect(rx, ry, rw, rh, TFT_BLACK);
  const uint16_t edge = s_cv.color565(40, 52, 60);
  if (!land) s_cv.drawFastHLine(0, ry, W, edge);
  else s_cv.drawFastVLine(s_orient == Orient::LandALeft ? rx + rw - 1 : rx, 0, H, edge);
  if (s_conv_k < 0.95f) return;   // 展開中不畫字
  if (s_conv_dirty) rewrap();
  s_cv.setFont(&fonts::efontTW_12);
  s_cv.setTextSize(1);
  s_cv.setTextDatum(top_left);
  const int nl = land ? (H - CONV_PAD * 2) / 14 : CONV_LINES_PORTRAIT;
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
    const int y = ry + CONV_PAD + k * 14 + 1;
    int x = rx + CONV_PAD;
    if (L.pfx) {
      s_cv.setTextColor(L.bot ? s_cv.color565(0x00, 0xF0, 0xFF) : s_cv.color565(MG_R, MG_G, MG_B));
      const char* pf = L.bot ? PFX_BOT : PFX_USER;
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
    s_cv.setTextColor(L.bot ? s_cv.color565(241, 250, 238) : s_cv.color565(150, 160, 175));
    s_cv.drawString(tmp, x, y);
  }
  s_cv.setFont(&fonts::Font0);
}

// 右上角「Wi-Fi 加紅色斜線」
void drawWifiOff(int x, int y) {
  const uint16_t c = s_cv.color565(150, 150, 150);
  s_cv.fillArc(x, y, 13, 11, 225, 315, c);
  s_cv.fillArc(x, y, 8, 6, 225, 315, c);
  s_cv.fillCircle(x, y - 1, 2, c);
  const uint16_t red = s_cv.color565(226, 75, 74);
  for (int d = -1; d <= 1; ++d) s_cv.drawLine(x - 9 + d, y - 14, x + 9 + d, y + 2, red);
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
  const float pad = 4 * sc;
  const float r = fminf(11 * sc, fminf(w, h) * 0.5f);
  s_cv.fillSmoothRoundRect((int32_t)(ex - w / 2 - pad), (int32_t)(ey - h / 2 - pad), (int32_t)(w + 2 * pad),
                           (int32_t)(h + 2 * pad), (int32_t)(r + pad), fade(c2, 0.28f));
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
  if (m == Mood::Scared) emit(PK::Text, '!', 45, -51, MG, 26);
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
        emit(PK::Text, '?', -s * 55, -55, MG, 24, 0, -6, 1.0f);
      }
      if (talk) E.paused = false;
      if (t < E.dx_until) tg[K_DX] = E.dx;
      if (s_prev_v < 0.62f && v >= 0.62f) s_sp[K_HY].v += 70;                      // 跟節奏點頭
      if (s_prev_v < 0.95f && v >= 0.95f) { s_sp[K_ED].v -= 160; emit(PK::Text, '!', -s * 55, -55, MG, 22); }   // 大聲：耳朵一抖
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
            if (rnd(0, 1) < 0.5f) emit(PK::Note, 0, rnd(33, 57), rnd(-65, -45), MG, 1.4f, rnd(-6, 6));
            else emit(PK::Text, '!', rnd(33, 57), rnd(-65, -45), {0x00, 0xF0, 0xFF}, 20, rnd(-6, 6));
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
        emit(PK::Note, 0, left ? rnd(-61, -45) : rnd(45, 61), rnd(-41, -11),
             rnd(0, 1) < 0.5f ? MG : Rgb{0xFF, 0xE6, 0x00}, 1.4f, rnd(-8, 8), -24, 1.4f);
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
      if (t > E.x2) { E.x2 = t + 2.6f; if (t > 0.5f) emit(PK::Text, '!', 45, -51, MG, 22); }
      break;

    case Mood::Sleepy: {
      tg[K_SLP] = 1;
      const float c = fmodf(t, 5.5f);
      const bool yawn_cycle = ((int)(t / 5.5f)) % 2 == 1;
      f.mm = MouthMode::Line; tg[K_MW] = 18; tg[K_MC] = 0;
      if (c < 3) {                       // 眼皮越來越重、頭往下沉
        tg[K_LH] = tg[K_RH] = 26 - c / 3 * 18; tg[K_HY] = c / 3 * 11;
      } else if (c < 3.35f) {            // 猛然驚醒
        if (!E.snap) { E.snap = true; s_sp[K_HY].v -= 170; emit(PK::Text, '!', 45, -51, MG, 18, 0, -10, 0.7f); }
        tg[K_LH] = tg[K_RH] = 34;
      } else {
        E.snap = false;
        tg[K_LH] = tg[K_RH] = 28;
        if (yawn_cycle && c > 3.7f && c < 5.2f) {   // 打哈欠
          f.mm = MouthMode::OFill; tg[K_MW] = 12; tg[K_MH] = 4 + 12 * sinf((c - 3.7f) / 1.5f * 3.14159f);
          tg[K_LH] = tg[K_RH] = 5;
          if (!E.yawn) { E.yawn = true; emit(PK::Dots, 0, 43, -45, {0x2B, 0xB3, 0xA8}, 1.2f, 0, -8, 1.4f); }
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
      if (t > E.n) { E.n = t + 1.2f; emit(PK::Text, 'z', rnd(31, 41), -31, {127, 184, 180}, rnd(14, 22), 6, -14, 1.8f); }
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
void setLowBatteryBlink(bool on) { s_low_blink = on; }
void setGaze(float x, float y) {
  s_gaze_tx = x < -1 ? -1 : x > 1 ? 1 : x;
  s_gaze_ty = y < -1 ? -1 : y > 1 ? 1 : y;
}

void setMotion(float right_g, float down_g) { s_mot_x = right_g; s_mot_y = down_g; }

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
    t1 = {s_tint_r * dim, s_tint_g * dim, s_tint_b * dim};
    t2 = {s_tint_r * dim * 0.5f, s_tint_g * dim * 0.5f, s_tint_b * dim * 0.5f};   // 同色系較暗
  }
  float ck = dt * 10.0f;
  if (ck > 1) ck = 1;
  s_c1 = mixc(s_c1, t1, ck);
  s_c2 = mixc(s_c2, t2, ck);
  Rgb c1 = s_c1, c2 = s_c2;
  if (s_offline) { c1 = {OFF_R, OFF_G, OFF_B}; c2 = {OFF_R * 0.6f, OFF_G * 0.6f, OFF_B * 0.6f}; }
  else if (s_low_blink && shown != Mood::Listening && shown != Mood::Thinking && shown != Mood::Speaking) {
    const bool red = ((millis() / cfg::BAT_BLINK_MS) % 2) == 0;
    c1 = red ? Rgb{BLINK_RED_R, BLINK_RED_G, BLINK_RED_B} : Rgb{OFF_R, OFF_G, OFF_B};
    c2 = mixc(BG, c1, 0.6f);
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
  const int cs = (int)(convHeight() * s_conv_k + 0.5f);
  int fx0 = 0, fw = W, fh = H;
  if (!land) fh = H - cs;
  else { fw = W - cs; if (s_orient == Orient::LandALeft) fx0 = cs; }
  const float sc = land ? 0.8f : 1.0f;
  const float fcx = fx0 + fw * 0.5f;
  const float fcy = land ? H * 0.40f : fh * (0.42f + 0.06f * s_conv_k);
  const int hz = land ? (int)(H * 0.86f) : 190;   // 地平線
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
  s_cv.fillScreen(c565(BG));

  // 地平線霓虹網格（往前捲動，速度跟情緒與音量）
  {
    s_grid_off += dt * mi.grid * (1 + fr.v);
    if (s_grid_off > 1000) s_grid_off -= 1000;
    s_cv.drawFastHLine(0, hz, W, c565(MG));
    s_cv.drawFastHLine(0, hz + 1, W, c565(MG));
    s_cv.setClipRect(0, hz + 2, W, H - hz - 2);
    const uint16_t gc = fade(MG, 0.5f);
    for (int k = -3; k <= 3; ++k) s_cv.drawLine((int32_t)fcx, hz, (int32_t)(fcx + k * fw * 0.42f), H, gc);
    const float step = 12 * sc;
    for (float y = hz + 6 * sc + fmodf(s_grid_off * sc, step); y < H; y += step) s_cv.drawFastHLine(0, (int32_t)y, W, gc);
    s_cv.clearClipRect();
  }

  // 聆聽：耳朵＋聲波（耳朵從畫面邊緣彈進來）
  const int es = earSide();
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

  // 眼睛
  const float lx = X(P(K_LX) + dx + hx + gdx + s_bd[0].ox), ly = Y(P(K_LY) + dy + hy + gdy + s_bd[0].oy);
  const float rx = X(P(K_RX) + dx + hx + gdx + s_bd[1].ox), ry = Y(P(K_RY) + dy + hy + gdy + s_bd[1].oy);
  const float lw = P(K_LW) * sc * s_bd[0].sx, rw = P(K_RW) * sc * s_bd[1].sx;
  const float lh = (blink ? 4 : fmaxf(4, P(K_LH))) * sc * s_bd[0].sy;
  const float rh = ((blink || wink) ? 4 : fmaxf(4, P(K_RH))) * sc * s_bd[1].sy;
  drawEye(lx, ly, lw, lh, c1, c2, -1, P(K_ANG), P(K_SLP), P(K_HAP), sc);
  drawEye(rx, ry, rw, rh, c1, c2, +1, P(K_ANG), P(K_SLP), P(K_HAP), sc);

  // 眉毛
  if (P(K_BRA) > 0.05f && fr.brows) {
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

  // 驚嚇：冷汗
  if (shown == Mood::Scared && !s_offline) {
    const float sx = rx + rw / 2 + 8 * sc, sy = ry - 14 * sc + fmodf(t * 30, 16) * sc;
    const uint16_t sw = s_cv.color565(0x4C, 0xC9, 0xF0);
    s_cv.fillTriangle((int32_t)sx, (int32_t)(sy - 6 * sc), (int32_t)(sx - 3 * sc), (int32_t)sy, (int32_t)(sx + 3 * sc), (int32_t)sy, sw);
    s_cv.fillCircle((int32_t)sx, (int32_t)sy, (int32_t)(3.5f * sc), sw);
  }

  // 嘴巴
  {
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
          s_cv.drawCircle((int32_t)(mx + (10 + bc * 2) * sc), (int32_t)(my - (6 + bc * 2) * sc), (int32_t)br,
                          s_cv.color565(127, 184, 180));
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
          s_cv.fillEllipse((int32_t)mx, (int32_t)(my + mh / 2 - 4 * sc), (int32_t)(mw * 0.25f), (int32_t)(3.5f * sc), c565(MG));
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

  // 思考：右上思考泡泡（三點輪流亮）
  if (shown == Mood::Thinking) {
    const int act = ((int)(t * 3)) % 3;
    for (int i = 0; i < 3; ++i) {
      const float r = (2 + i) * (i == act ? 1.35f : 1.0f) * sc;
      s_cv.fillCircle((int32_t)X(41 + i * 9), (int32_t)Y(-43 - i * 10 + hy), (int32_t)r, i == act ? c565(c1) : fade(c1, 0.3f));
    }
  }
  // 生氣：# 青筋跳動
  if (shown == Mood::Angry) {
    s_cv.setFont(&fonts::efontTW_12);
    s_cv.setTextDatum(middle_center);
    s_cv.setTextSize((1.6f + 0.4f * fmaxf(0, sinf(t * 9))) * sc);
    s_cv.setTextColor(c565(MG));
    s_cv.drawString("#", (int32_t)X(51), (int32_t)Y(-61 + hy));
    s_cv.setFont(&fonts::Font0);
    s_cv.setTextSize(1);
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
    const float pop = fminf(1, p.age * 8) * (1 + 0.3f * fmaxf(0, 1 - p.age * 4));
    const uint16_t pc = fade(p.col, 1 - p.age / p.life);
    const float px = X(p.x), py = Y(p.y);
    switch (p.kind) {
      case PK::Text: {
        const char s[2] = {p.ch, 0};
        s_cv.setTextSize(fmaxf(0.5f, p.size / 12.0f * pop * sc));
        s_cv.setTextColor(pc);
        s_cv.drawString(s, (int32_t)px, (int32_t)py);
        break;
      }
      case PK::Note: drawNote(px, py, p.size * pop * sc, pc); break;
      case PK::Dots:
        for (int k = -1; k <= 1; ++k) s_cv.fillCircle((int32_t)(px + k * 6 * sc), (int32_t)py, (int32_t)fmaxf(1, 1.8f * p.size * pop * sc), pc);
        break;
      case PK::Dot: s_cv.fillCircle((int32_t)px, (int32_t)py, (int32_t)fmaxf(1, 1.5f * sc), pc); break;
    }
  }
  s_cv.setTextSize(1);
  s_cv.setFont(&fonts::Font0);

  drawConv(W, H);   // 最後畫，蓋住延伸到 A 鍵側的網格與聲波

  if (s_offline) drawWifiOff(W - 16, 20);

  s_cv.pushSprite(0, 0);
}

}  // namespace ui_expression
