#include "ui_expression.h"

#include <M5Unified.h>
#include <math.h>

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
};

// 順序必須與 enum class Mood 一致
const MoodSpec kMoods[] = {
  {"Idle",    76, 240, 232, 1.00f, 1.00f, 0, 0, 0, 0},
  {"Happy",  255, 209, 102, 1.00f, 1.00f, 0, 0, 1, 0},
  {"Angry",  255, 107, 107, 0.90f, 1.00f, 1, 0, 0, 0},
  {"Scared", 179, 136, 235, 0.72f, 0.72f, 0, 0, 0, 1},
  {"Sleepy", 127, 184, 180, 0.50f, 1.00f, 0, 1, 0, 0},
  {"Asleep",  61, 107, 105, 0.07f, 1.00f, 0, 0, 0, 0},
};
constexpr uint8_t kMoodCount = sizeof(kMoods) / sizeof(kMoods[0]);

Mood s_mood = Mood::Idle;
MoodSpec s_cur = kMoods[0];        // 目前畫面上的值，每幀往目標值靠近（平滑過渡）
uint32_t s_last_frame_ms = 0;

// 眨眼狀態機
bool     s_blinking = false;
uint32_t s_blink_start_ms = 0;
uint32_t s_last_blink_end_ms = 0;
uint32_t s_next_gap_ms = 2800;

constexpr uint32_t BLINK_DUR_MS = 120;     // 一次眨眼壓扁多久
constexpr float    EASE_PER_MS  = 0.010f;  // 過渡速度：越大換表情越快

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

void debugNextMood() {
  s_mood = static_cast<Mood>((static_cast<uint8_t>(s_mood) + 1) % kMoodCount);
}

void render() {
  if (!s_ready) return;
  const uint32_t now = millis();
  const uint32_t dt = now - s_last_frame_ms;
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
  const int cy = (int)(H * 0.42f);
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

  const int ey = cy - eh / 2 + breathe;                // 眼睛上緣 y
  const uint16_t col = s_cv.color565((uint8_t)s_cur.r, (uint8_t)s_cur.g, (uint8_t)s_cur.b);

  // ── 繪製 ──
  s_cv.fillScreen(TFT_BLACK);
  for (int side = -1; side <= 1; side += 2) {          // -1 = 左眼, +1 = 右眼
    const int ex = cx + side * gap + jitter;           // 眼睛中心 x
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

  // 睡著：右上方飄出 Z
  if (s_mood == Mood::Asleep && s_cur.h_scale < 0.2f) {
    const float p = (now % 1600) / 1600.0f;            // 0→1 循環
    s_cv.setTextColor(s_cv.color565(127, 184, 180));
    s_cv.setTextSize(1);
    s_cv.drawString("z", cx + gap + 4, cy - 26 - (int)(p * 14));
    s_cv.setTextSize(2);
    s_cv.drawString("Z", cx + gap + 14, cy - 46 - (int)(p * 14));
  }

  s_cv.pushSprite(0, 0);
}

}  // namespace ui_expression
