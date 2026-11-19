#include "ui_expression.h"

#include <M5Unified.h>
#include <math.h>
#include <string.h>

#include "config.h"

namespace {

// 全螢幕畫布（放 PSRAM，避免佔用內部 RAM；與 ui_status 的畫布各自獨立）
M5Canvas s_cv(&M5.Display);
bool s_ready = false;

// ── 每種情緒的「目標值」：顏色固定對應情緒，不隨機 ──
struct MoodSpec {
  const char* name;
  float r, g, b;      // 眼睛顏色
  float h_scale;      // 眼高倍率（1 = 正常）
  float w_scale;      // 眼寬倍率
  float lid_angry;    // 0～1：內側下壓的眼皮（生氣）
  float lid_sleepy;   // 0～1：外側下垂的眼皮（想睡）
  float lid_happy;    // 0～1：下方弧形遮罩（笑眼）
  float shake;        // 0～1：發抖＋冒汗（驚嚇）
  float think;        // 0～1：眼睛往上看並左右游移＋下方三個跳動的點（思考中）
  float listen;       // 0～1：從臉中央往外擴散的漣漪（聆聽中）
  float speak;        // 0～1：下方隨音量開合的嘴巴（說話中）
};

// 順序必須與 enum class Mood 一致
const MoodSpec kMoods[] = {
  {"Idle",       76, 240, 232, 1.00f, 1.00f, 0, 0, 0, 0, 0, 0, 0},
  {"Listening",  87, 204, 153, 1.08f, 1.00f, 0, 0, 0, 0, 0, 1, 0},
  {"Thinking",   96, 165, 250, 0.85f, 1.00f, 0, 0, 0, 0, 1, 0, 0},
  {"Speaking",  241, 250, 238, 1.00f, 1.00f, 0, 0, 0, 0, 0, 0, 1},
  {"Happy",     255, 209, 102, 1.00f, 1.00f, 0, 0, 1, 0, 0, 0, 0},
  {"Angry",     255, 107, 107, 0.90f, 1.00f, 1, 0, 0, 0, 0, 0, 0},
  {"Scared",    179, 136, 235, 0.72f, 0.72f, 0, 0, 0, 1, 0, 0, 0},
  {"Sleepy",    127, 184, 180, 0.50f, 1.00f, 0, 1, 0, 0, 0, 0, 0},
  {"Asleep",     61, 107, 105, 0.07f, 1.00f, 0, 0, 0, 0, 0, 0, 0},
};
constexpr uint8_t kMoodCount = sizeof(kMoods) / sizeof(kMoods[0]);

Mood s_mood = Mood::Idle;
MoodSpec s_cur = kMoods[0];        // 目前畫面上的值，每幀往目標值靠近（平滑過渡）
uint32_t s_last_frame_ms = 0;
float s_speak_level = -1.0f;       // <0 = 用內建模擬；M1-B 改由喇叭音量驅動

// 眨眼狀態機
bool     s_blinking = false;
uint32_t s_blink_start_ms = 0;
uint32_t s_last_blink_end_ms = 0;
uint32_t s_next_gap_ms = 2800;

constexpr uint32_t BLINK_DUR_MS = 120;     // 一次眨眼壓扁多久
constexpr float    EASE_PER_MS  = 0.010f;  // 過渡速度：越大換表情越快

// B3b-4 斷線提示
bool     s_offline = false;

// ── B4 對話區 ──
// 字型：M5GFX 內建繁中點陣 efontTW_12（12px）。一行約 10 個中文字（135px 扣左右邊界）。
constexpr int      CONV_LINES   = 5;
constexpr int      CONV_PAD     = 3;
constexpr uint32_t CONV_HOLD_MS = 5000;     // 這輪結束後對話區再留多久
constexpr size_t   USER_CAP     = 480;      // UTF-8 bytes
constexpr size_t   BOT_CAP      = 1500;
char     s_user[USER_CAP + 1] = "";
char     s_bot[BOT_CAP + 1] = "";
size_t   s_user_len = 0, s_bot_len = 0;
bool     s_conv_on = false;                 // 這輪對話區要顯示
uint32_t s_conv_hide_at = 0;                // convEnd 之後的收起時刻（0＝不收）
float    s_conv_k = 0.0f;                   // 0＝收起、1＝展開（平滑過渡）
bool     s_conv_dirty = false;
// 換好行的結果：每行是 s_user 或 s_bot 的一段
struct ConvLine { const char* p; uint16_t len; bool bot; };
constexpr int MAX_LINES = 96;
ConvLine s_lines[MAX_LINES];
int      s_nlines = 0;

size_t utf8Len(uint8_t lead) { return lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1; }

// 追加文字；超過容量就從前面整字丟掉
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

// 把一段文字依像素寬度換行，加進 s_lines（prefix 只算進第一行的寬度）
void wrapInto(const char* text, size_t len, bool bot, int maxw, int prefix_w) {
  const char* line = text;
  int w = prefix_w;
  const char* p = text;
  const char* end = text + len;
  char ch[5];
  while (p < end) {
    if (*p == '\n') {
      if (s_nlines < MAX_LINES) s_lines[s_nlines++] = {line, (uint16_t)(p - line), bot};
      ++p; line = p; w = 0; continue;
    }
    const size_t n = utf8Len((uint8_t)*p);
    memcpy(ch, p, n); ch[n] = '\0';
    const int cw = s_cv.textWidth(ch);
    if (w + cw > maxw && p > line) {
      if (s_nlines < MAX_LINES) s_lines[s_nlines++] = {line, (uint16_t)(p - line), bot};
      line = p; w = 0;
    }
    w += cw;
    p += n;
  }
  if (p > line && s_nlines < MAX_LINES) s_lines[s_nlines++] = {line, (uint16_t)(p - line), bot};
}

const char* const PFX_USER = "你：";
const char* const PFX_BOT  = "Friday：";   // 11-20：助理名 Friday（小S 是機器）

void rewrap(int W) {
  s_cv.setFont(&fonts::efontTW_12);
  s_cv.setTextSize(1);
  const int maxw = W - CONV_PAD * 2;
  s_nlines = 0;
  if (s_user_len) wrapInto(s_user, s_user_len, false, maxw, s_cv.textWidth(PFX_USER));
  if (s_bot_len) wrapInto(s_bot, s_bot_len, true, maxw, s_cv.textWidth(PFX_BOT));
  // 每段的第一行要畫前綴：記在 len 的最高位元不方便，改成畫的時候判斷「是不是該段開頭」
  s_cv.setFont(&fonts::Font0);
  s_conv_dirty = false;
}

int convHeight() {
  return CONV_LINES * 14 + CONV_PAD * 2;   // efontTW_12 每行 12px＋行距 2px
}

void drawConv(int W, int H) {
  const int ch = (int)(convHeight() * s_conv_k + 0.5f);
  if (ch <= 0) return;
  const int top = H - ch;
  s_cv.fillRect(0, top, W, ch, TFT_BLACK);
  s_cv.drawFastHLine(0, top, W, s_cv.color565(40, 52, 60));
  if (s_conv_k < 0.95f) return;   // 展開過程中不畫字（避免字被切一半）
  if (s_conv_dirty) rewrap(W);
  s_cv.setFont(&fonts::efontTW_12);
  s_cv.setTextSize(1);
  s_cv.setTextDatum(top_left);
  // 要畫哪幾行：小S 還沒有字 → 你的話捲動顯示最後 5 行；
  // 小S 有字 → 你的話固定前 USER_KEEP 行（太長在結尾加「…」），其餘行數給小S 捲動顯示最新的
  constexpr int USER_KEEP = 2;
  int idx[CONV_LINES];
  int nshow = 0, nuser = 0;
  for (int i = 0; i < s_nlines; ++i) if (!s_lines[i].bot) ++nuser;
  const int nbot = s_nlines - nuser;
  bool user_cut = false;
  if (nbot == 0) {
    for (int i = (s_nlines > CONV_LINES ? s_nlines - CONV_LINES : 0); i < s_nlines; ++i) idx[nshow++] = i;
  } else {
    const int ku = nuser < USER_KEEP ? nuser : USER_KEEP;
    user_cut = nuser > ku;
    for (int i = 0; i < ku; ++i) idx[nshow++] = i;           // 使用者的行排在前面（rewrap 先排你的再排小S）
    const int room = CONV_LINES - ku;
    for (int i = (nbot > room ? s_nlines - room : nuser); i < s_nlines; ++i) idx[nshow++] = i;
  }
  char tmp[256];
  for (int k = 0; k < nshow; ++k) {
    const int i = idx[k];
    const ConvLine& L = s_lines[i];
    const int y = top + CONV_PAD + k * 14 + 1;
    int x = CONV_PAD;
    const bool seg_start = (L.p == (L.bot ? s_bot : s_user));
    const uint16_t col = L.bot ? s_cv.color565(241, 250, 238) : s_cv.color565(120, 150, 160);
    if (seg_start) {
      s_cv.setTextColor(L.bot ? s_cv.color565(76, 240, 232) : s_cv.color565(90, 110, 120));
      const char* pf = L.bot ? PFX_BOT : PFX_USER;
      s_cv.drawString(pf, x, y);
      x += s_cv.textWidth(pf);
    }
    size_t n = L.len < sizeof(tmp) - 5 ? L.len : sizeof(tmp) - 5;
    if (user_cut && !L.bot && (k + 1 < nshow ? s_lines[idx[k + 1]].bot : true)) {
      // 你的話被截掉：這一行去掉最後一個字、補「…」
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
    s_cv.setTextColor(col);
    s_cv.drawString(tmp, x, y);
  }
  s_cv.setFont(&fonts::Font0);
}
uint32_t s_freeze_ms = 0;      // 定格那一刻；斷線期間所有動畫都用這個時間算
constexpr uint8_t OFF_R = 95, OFF_G = 94, OFF_B = 90;   // 斷線時眼睛的灰色

// 右上角「Wi-Fi 加紅色斜線」：(x, y) 是扇形的圓心（最下面那個點）
void drawWifiOff(int x, int y) {
  const uint16_t c = s_cv.color565(150, 150, 150);
  s_cv.fillArc(x, y, 13, 11, 225, 315, c);   // 角度：0°＝右、順時針，270°＝正上方
  s_cv.fillArc(x, y, 8, 6, 225, 315, c);
  s_cv.fillCircle(x, y - 1, 2, c);
  const uint16_t red = s_cv.color565(226, 75, 74);
  for (int d = -1; d <= 1; ++d) s_cv.drawLine(x - 9 + d, y - 14, x + 9 + d, y + 2, red);
}

inline float approach(float cur, float target, float k) { return cur + (target - cur) * k; }

const MoodSpec& target() { return kMoods[static_cast<uint8_t>(s_mood)]; }

}  // namespace

namespace ui_expression {

void begin() {
  s_cv.setPsram(true);
  s_cv.setColorDepth(16);
  s_cv.createSprite(M5.Display.width(), M5.Display.height());
  s_ready = (s_cv.getBuffer() != nullptr);
  s_last_blink_end_ms = millis();
  s_last_frame_ms = millis();
}

void setMood(Mood m) { s_mood = m; }
Mood mood() { return s_mood; }
const char* moodName() { return target().name; }

void setSpeakLevel(float level01) { s_speak_level = level01; }

void setOffline(bool off) {
  if (off && !s_offline) {
    s_freeze_ms = millis();
    s_blinking = false;          // 不要定格在眨眼的那一下
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
  // 沒有任何字（誤觸、echo 模式）就馬上收起；有字就留幾秒
  s_conv_hide_at = millis() + ((s_user_len || s_bot_len) ? CONV_HOLD_MS : 0) + 1;
}

void debugNextMood() {
  s_mood = static_cast<Mood>((static_cast<uint8_t>(s_mood) + 1) % kMoodCount);
}

void render() {
  if (!s_ready) return;
  // B3b-4：斷線時時間停在定格那一刻 → 呼吸、眨眼、漣漪、過渡全部不動
  const uint32_t now = s_offline ? s_freeze_ms : millis();
  const uint32_t dt = (now > s_last_frame_ms) ? now - s_last_frame_ms : 0;
  s_last_frame_ms = now;

  // ── 平滑過渡：目前值往目標情緒靠近 ──
  float k = dt * EASE_PER_MS;
  if (k > 1.0f) k = 1.0f;
  const MoodSpec& t = target();
  s_cur.r = approach(s_cur.r, t.r, k);
  s_cur.g = approach(s_cur.g, t.g, k);
  s_cur.b = approach(s_cur.b, t.b, k);
  s_cur.h_scale    = approach(s_cur.h_scale, t.h_scale, k);
  s_cur.w_scale    = approach(s_cur.w_scale, t.w_scale, k);
  s_cur.lid_angry  = approach(s_cur.lid_angry, t.lid_angry, k);
  s_cur.lid_sleepy = approach(s_cur.lid_sleepy, t.lid_sleepy, k);
  s_cur.lid_happy  = approach(s_cur.lid_happy, t.lid_happy, k);
  s_cur.shake      = approach(s_cur.shake, t.shake, k);
  s_cur.think      = approach(s_cur.think, t.think, k);
  s_cur.listen     = approach(s_cur.listen, t.listen, k);
  s_cur.speak      = approach(s_cur.speak, t.speak, k);

  // ── B4 對話區展開／收起（用真實時間，斷線定格時也照常收）──
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

  // ── 眨眼排程（睡著時不眨）──
  if (s_mood != Mood::Asleep) {
    if (!s_blinking && now - s_last_blink_end_ms >= s_next_gap_ms) {
      s_blinking = true;
      s_blink_start_ms = now;
    }
    if (s_blinking && now - s_blink_start_ms >= BLINK_DUR_MS) {
      s_blinking = false;
      s_last_blink_end_ms = now;
      s_next_gap_ms = 2500 + (now % 1800);   // 2.5～4.3 秒隨機
    }
  } else {
    s_blinking = false;
  }

  // ── 幾何 ──
  const int W = s_cv.width();
  const int H = s_cv.height();
  const int cx = W / 2;
  // B4：對話區展開時，表情在剩下的高度裡置中（嘴巴在眼睛下方 52px，要留在對話區上面）
  const int Hf = H - (int)(convHeight() * s_conv_k);
  const int cy = (int)(Hf * (0.42f + 0.06f * s_conv_k));
  const int gap = 28;                                  // 中心到各眼中心的距離
  const int ew = (int)(32 * s_cur.w_scale);            // 眼寬
  int eh = (int)(46 * s_cur.h_scale);                  // 眼高
  if (eh < 3) eh = 3;
  if (s_blinking) eh = 5;                              // 眨眼：壓扁
  int r = 11;                                          // 圓角（不可超過寬高的一半）
  if (r > eh / 2) r = eh / 2;
  if (r > ew / 2) r = ew / 2;

  // 呼吸：整組眼睛上下平移（約 2 秒一個來回、±9px）；睡著時放慢
  const float breathe_div = (s_mood == Mood::Asleep) ? 640.0f : 318.0f;
  const int breathe = (int)(sinf(now / breathe_div) * 9.0f);
  // 驚嚇：左右快速發抖
  const int jitter = (int)(sinf(now * 0.09f) * 2.5f * s_cur.shake);

  // 思考中：眼睛往上抬，並緩慢左右游移（像在想事情）
  const int think_up = (int)(12.0f * s_cur.think);
  const int think_dx = (int)(sinf(now / 700.0f) * 7.0f * s_cur.think);

  const int ey = cy - eh / 2 + breathe - think_up;     // 眼睛上緣 y
  // B3b-4：斷線時眼睛（含漣漪、嘴巴）一律灰色
  const float cr = s_offline ? OFF_R : s_cur.r, cg = s_offline ? OFF_G : s_cur.g, cb = s_offline ? OFF_B : s_cur.b;
  const uint16_t col = s_cv.color565((uint8_t)cr, (uint8_t)cg, (uint8_t)cb);

  // ── 繪製 ──
  s_cv.fillScreen(TFT_BLACK);

  // 聆聽中：從臉中央往外擴散的三圈漣漪（畫在眼睛後面，越外圈越暗）
  if (s_cur.listen > 0.3f) {
    for (int i = 0; i < 3; ++i) {
      const int prog = (int)((now / 14 + i * 22) % 66);          // 0～65
      const float fade = (1.0f - prog / 66.0f) * s_cur.listen;
      const uint16_t rc = s_cv.color565((uint8_t)(cr * fade * 0.6f),
                                        (uint8_t)(cg * fade * 0.6f),
                                        (uint8_t)(cb * fade * 0.6f));
      s_cv.drawCircle(cx, cy + breathe, 24 + prog, rc);
      s_cv.drawCircle(cx, cy + breathe, 25 + prog, rc);
    }
  }

  for (int side = -1; side <= 1; side += 2) {          // -1 = 左眼, +1 = 右眼
    const int ex = cx + side * gap + jitter + think_dx; // 眼睛中心 x
    const int L = ex - ew / 2;
    const int R = ex + ew / 2;
    s_cv.fillSmoothRoundRect(L, ey, ew, eh, r, col);

    // 眼皮都用黑色形狀蓋在眼睛上，amount 0→1 逐漸蓋上去
    if (s_cur.lid_angry > 0.02f) {                     // 生氣：內側往下壓（八字眉）
      const int drop = (int)(eh * 0.50f * s_cur.lid_angry);
      if (side < 0) s_cv.fillTriangle(L - 1, ey - 1, R + 1, ey - 1, R + 1, ey + drop, TFT_BLACK);
      else          s_cv.fillTriangle(L - 1, ey - 1, R + 1, ey - 1, L - 1, ey + drop, TFT_BLACK);
    }
    if (s_cur.lid_sleepy > 0.02f) {                    // 想睡：外側往下垂
      const int drop = (int)(eh * 0.65f * s_cur.lid_sleepy);
      if (side < 0) s_cv.fillTriangle(L - 1, ey - 1, R + 1, ey - 1, L - 1, ey + drop, TFT_BLACK);
      else          s_cv.fillTriangle(L - 1, ey - 1, R + 1, ey - 1, R + 1, ey + drop, TFT_BLACK);
    }
    if (s_cur.lid_happy > 0.02f) {                     // 開心：下方弧形遮罩，剩上半成笑眼
      const int oy = ey + (int)(eh * (0.62f + (1.0f - s_cur.lid_happy)));
      s_cv.fillEllipse(ex, oy, (int)(ew * 0.72f), (int)(eh * 0.50f), TFT_BLACK);
    }
  }

  // 驚嚇：右眼旁一滴往下滑的汗
  if (s_cur.shake > 0.5f) {
    const int sx = cx + gap + ew / 2 + 7 + jitter;
    const int sy = ey - 2 + (int)((now / 40) % 12);
    const uint16_t sweat = s_cv.color565(76, 201, 240);
    s_cv.fillTriangle(sx, sy - 6, sx - 3, sy, sx + 3, sy, sweat);
    s_cv.fillCircle(sx, sy, 3, sweat);
  }

  // 說話中：眼睛下方的嘴巴，高度隨音量開合
  if (s_cur.speak > 0.3f) {
    float lv = s_speak_level;
    if (lv < 0) lv = fabsf(sinf(now / 90.0f) * sinf(now / 233.0f));   // 模擬音量
    if (lv > 1) lv = 1;
    const int mh = 4 + (int)(lv * 20.0f * s_cur.speak);
    const int mw = 38;
    s_cv.fillSmoothRoundRect(cx - mw / 2, cy + 52 - mh / 2, mw, mh, mh / 2 > 6 ? 6 : mh / 2, col);
  }

  // 思考中：眼睛下方三個輪流跳動的點
  if (s_cur.think > 0.5f) {
    const int active = (now / 300) % 3;
    const int dy = cy + 52;
    for (int i = 0; i < 3; ++i) {
      const bool on = (i == active);
      s_cv.fillCircle(cx + (i - 1) * 14, dy - (on ? 4 : 0), on ? 4 : 3,
                      on ? col : s_cv.color565(40, 70, 110));
    }
  }

  // 睡著：右上方飄出 Z
  if (s_mood == Mood::Asleep && s_cur.h_scale < 0.2f) {
    const float p = (now % 1600) / 1600.0f;            // 0→1 循環
    s_cv.setTextColor(s_cv.color565(127, 184, 180));
    s_cv.setTextSize(1);
    s_cv.drawString("z", cx + gap + 4, cy - 26 - (int)(p * 14));
    s_cv.setTextSize(2);
    s_cv.drawString("Z", cx + gap + 14, cy - 46 - (int)(p * 14));
  }

  drawConv(W, H);   // B4：最後畫，蓋住延伸到下方的漣漪

  if (s_offline) drawWifiOff(W - 16, 20);

  s_cv.pushSprite(0, 0);
}

}  // namespace ui_expression
