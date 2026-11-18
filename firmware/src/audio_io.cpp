#include "audio_io.h"

#include <M5Unified.h>
#include <string.h>
#include <math.h>
#include <algorithm>

// 燒錄版本標記：開機會印出來，用來確認板子上跑的是哪一版
#define AUDIO_BUILD_TAG "17-b3b3"

namespace {

constexpr uint32_t SAMPLE_RATE = 16000;
constexpr size_t   CHUNK       = SAMPLE_RATE / 10;   // 1600 取樣 = 100ms，一塊
constexpr size_t   CAP_SAMPLES = SAMPLE_RATE * 6;    // 最長 6 秒

// ── 麥克風 ────────────────────────────────────────────────────────────────
// 夾波的真正位置在 ES8311 晶片內部，不在這一層：StickS3 的設定是
// 「模擬 PGA 最小（0x14=0x10）＋數位增益最大（0x17=0xFF，約 +32dB）」，
// 訊號進 ESP32 前就已滿幅，所以這裡填 16 或 8 都一樣被截在 32767。
// 下面 micOn() 會把那兩顆暫存器改成「模擬 +21dB ＋數位 0dB」——
// 模擬放大的訊噪比優於數位放大，這組值取自 M5Unified 對別塊板子的設定。
constexpr uint8_t MIC_MAGNIFICATION = 8;
// ES8311 麥克風增益暫存器（值的來源見上）
constexpr uint8_t ES8311_REG_PGA = 0x14;  // 模擬 PGA；0x10=最小，0x17=+21dB
constexpr uint8_t ES8311_REG_ADC = 0x17;  // 數位增益；0xBF=0dB，每階 0.5dB
constexpr uint8_t PGA_21DB  = 0x17;
constexpr uint8_t PGA_MIN   = 0x10;   // 0dB
constexpr uint8_t PGA_MAX   = 0x1A;   // +30dB（esp-adf 的 es8311 驅動用 0x1A 當最大 PGA）
// B3b-2：預設改 0x15（+15dB，以每階 3dB 推算）。B3.6 實聽：背景嘶聲比 0x17 明顯少；
// 人聲變小由正規化補回。序列埠 g17 可切回舊值對照。
constexpr uint8_t PGA_DEFAULT = 0x15;
uint8_t s_pga = PGA_DEFAULT;          // 可由序列埠改（diag_cmd 的 g 指令）
constexpr uint8_t ADC_0DB   = 0xBF;
// 一階 IIR 低通（M5Unified 的 noise_filter_level）：
// v = 本次 × (256-L)/256 + 前次 × L/256。L=64 的 -3dB 截止約 4.3kHz。
constexpr uint8_t MIC_NOISE_FILTER = 64;

// ── 喇叭 ──────────────────────────────────────────────────────────────────
// StickS3 板子設定把輸出取樣率覆寫成 22050。播 16000 的音訊要做 1.378 倍的
// 非整數倍重取樣，插值誤差比整數倍大得多；設成和音訊一樣就完全不重取樣。
constexpr uint32_t SPK_SAMPLE_RATE = SAMPLE_RATE;
// 板子設定的輸出乘數只有 1（一般板子預設 16），這是音量上不去的主因。
constexpr uint8_t SPK_MAGNIFICATION = 4;
constexpr uint8_t SPK_VOLUME = 255;   // 主音量；實際增益是這個值的平方

// ── 開關喇叭的「噠」(pop) ──────────────────────────────────────────────────
// 來源是喇叭功率放大器的電源通斷：M5Unified 在 Speaker.begin()/end() 裡開關它，
// 而放大器通電時偏壓建立、斷電時偏壓崩塌，都會讓喇叭動一下。
// 2026-11-17 的教訓：自己再加一輪「斷電→等→通電」只是多製造一次跳變，更響。
// 現在只做能做的部分 —— 用 ES8311 的 DAC 音量把輸出壓到零，再讓 M5Unified
// 自己去切電源，不另外插手。
constexpr uint8_t  ES8311_ADDR    = 0x18;
constexpr uint8_t  ES8311_REG_DAC = 0x32;    // DAC 音量暫存器
constexpr uint8_t  DAC_VOL_0DB    = 0xBF;    // ±0 dB（M5Unified 用的值）
constexpr uint8_t  DAC_VOL_MUTE   = 0x00;
constexpr uint32_t I2C_FREQ        = 100000;
constexpr uint32_t CODEC_SETTLE_MS = 40;     // codec 重置後等它穩定

// ── 音量正規化與淡入淡出 ──────────────────────────────────────────────────
constexpr float   TARGET_PEAK   = 0.85f;  // 正規化目標：滿幅的 85%，留餘裕不夾波
constexpr int32_t NORM_MIN_PEAK = 200;    // 低於此視為沒講話，不放大（免得放大底噪）
constexpr float   NORM_MAX_GAIN = 8.0f;
constexpr size_t  FADE_IN       = SAMPLE_RATE / 200;  // 5ms
constexpr size_t  FADE_OUT      = SAMPLE_RATE / 100;  // 10ms

int16_t* s_buf = nullptr;
int16_t* s_rx  = nullptr;   // B3b-2：中控回傳的音訊
bool     s_ready = false;
bool     s_recording = false;
bool     s_playing = false;
size_t   s_queued = 0;      // 已排進麥克風隊列的取樣數
size_t   s_len = 0;
uint32_t s_t0 = 0;          // 按下 A 的時刻，用來算實際按住多久
// 切掉頭尾的按鍵聲：A 鍵就在麥克風旁，按下／放開的機械「喀」聲近距離錄進去
// 會撞滿幅，播出來就是頭尾各一個「噠」。實測滿幅最早出現在按下後約 50ms。
constexpr uint32_t TRIM_START_MS = 60;
constexpr uint32_t TRIM_END_MS   = 60;
float    s_peak = 0.0f;
float    s_head_peak = 0.0f;

// DAC 靜音／解除。輸出為零時改音量不會造成跳變（音量 × 0 還是 0）。
void dacMute(bool mute) {
  M5.In_I2C.writeRegister8(ES8311_ADDR, ES8311_REG_DAC,
                           mute ? DAC_VOL_MUTE : DAC_VOL_0DB, I2C_FREQ);
}

// 關喇叭：先讓 DAC 靜音、輸出歸零，再交給 end() 去斷放大器。
// 不自己斷電——放大器每次通斷本身就是一次跳變，自己多做一輪只會更響。
void speakerOff() {
  if (!M5.Speaker.isRunning()) return;
  dacMute(true);
  M5.delay(5);
  M5.Speaker.end();   // 它的 callback 會自己把放大器斷電
}

// 開喇叭：begin() 的 callback 是同步執行的（已對照原始碼確認），回來時放大器
// 已通電、codec 已重設完。接著立刻靜音壓住剛設定好的瞬態，等穩定再解除；
// 此時 I2S 還沒送資料，解除靜音不會出聲。全程不自己動放大器電源。
void speakerOn() {
  speakerOff();                       // 從乾淨狀態開始，也讓下面的 config 改得進去
  auto cfg = M5.Speaker.config();
  cfg.sample_rate   = SPK_SAMPLE_RATE;
  cfg.magnification = SPK_MAGNIFICATION;
  M5.Speaker.config(cfg);
  M5.Speaker.begin();
  dacMute(true);
  M5.delay(CODEC_SETTLE_MS);
  dacMute(false);
  M5.Speaker.setVolume(SPK_VOLUME);
}

void micOn() {
  speakerOff();
  auto cfg = M5.Mic.config();
  cfg.sample_rate        = SAMPLE_RATE;
  cfg.magnification      = MIC_MAGNIFICATION;
  cfg.noise_filter_level = MIC_NOISE_FILTER;
  M5.Mic.config(cfg);
  M5.Mic.begin();
  // begin() 的 callback 剛把增益寫成「模擬最小＋數位最大」，在這裡改掉：
  // 改用模擬 +21dB、數位 0dB，訊號才不會進 ESP32 前就夾波。
  bool w1 = M5.In_I2C.writeRegister8(ES8311_ADDR, ES8311_REG_PGA, s_pga, I2C_FREQ);
  bool w2 = M5.In_I2C.writeRegister8(ES8311_ADDR, ES8311_REG_ADC, ADC_0DB,  I2C_FREQ);
  // 診斷：讀回確認寫入有沒有生效（0x14 應為 0x17、0x17 應為 0xBF）
  uint8_t r14 = 0xEE, r17 = 0xEE;
  M5.In_I2C.readRegister(ES8311_ADDR, ES8311_REG_PGA, &r14, 1, I2C_FREQ);
  M5.In_I2C.readRegister(ES8311_ADDR, ES8311_REG_ADC, &r17, 1, I2C_FREQ);
  Serial.printf("[audio] ES8311 寫入%s%s 讀回 0x14=%02X 0x17=%02X\n",
                w1 ? "ok" : "失敗", w2 ? "/ok" : "/失敗", r14, r17);
}

// 峰值掃描：回傳 0~32767。順便記開頭 0.5 秒的峰值，看 codec 暖機是否吃掉開頭。
// 去除孤立尖峰（數位突波）：實測每次錄音有 10～13 個樣本突然跳到 32752，
// 前後樣本卻很小——那不是聲音。播出來就是「噠」，也會把峰值頂到 1.000
// 讓正規化失效。判斷：本身接近滿幅、且與前後鄰居差距極大 → 用鄰居內插補上。
// 跑兩輪，連續兩個樣本的尖峰也抹得掉。回傳修掉幾個。
constexpr int32_t SPIKE_LEVEL = 28000;   // 夠接近滿幅才算候選
constexpr int32_t SPIKE_JUMP  = 16000;   // 與鄰居差這麼多才算突波
uint32_t despike(int16_t* buf, size_t len) {
  uint32_t fixed = 0;
  if (len < 5) return 0;
  for (int pass = 0; pass < 2; ++pass) {
    for (size_t i = 2; i + 2 < len; ++i) {
      int32_t v = buf[i];
      int32_t a = (v < 0) ? -v : v;
      if (a < SPIKE_LEVEL) continue;
      // 鄰居：單點尖峰看 i±1；兩點尖峰時 i±1 可能也是尖峰，改看 i±2
      int32_t l = buf[i - 1], r = buf[i + 1];
      int32_t la = l < 0 ? -l : l, ra = r < 0 ? -r : r;
      if (la >= SPIKE_LEVEL) l = buf[i - 2];
      if (ra >= SPIKE_LEVEL) r = buf[i + 2];
      if ((v - l > SPIKE_JUMP || l - v > SPIKE_JUMP) &&
          (v - r > SPIKE_JUMP || r - v > SPIKE_JUMP)) {
        buf[i] = (int16_t)((l + r) / 2);
        ++fixed;
      }
    }
  }
  return fixed;
}

// 診斷：列出滿幅樣本出現的位置（ms），確認是否集中在頭尾（按鍵聲）
void printClipPositions(const int16_t* buf, size_t len) {
  char line[160];
  int n = snprintf(line, sizeof(line), "[audio] 滿幅位置(ms)：");
  int32_t last = -1000;
  int shown = 0;
  for (size_t i = 0; i < len && shown < 12; ++i) {
    int32_t v = buf[i] < 0 ? -(int32_t)buf[i] : (int32_t)buf[i];
    if (v < 32000) continue;
    int32_t ms = (int32_t)(i * 1000 / SAMPLE_RATE);
    if (ms - last < 5) continue;            // 同一聲只記一次
    last = ms;
    n += snprintf(line + n, sizeof(line) - n, "%d ", (int)ms);
    if (n >= (int)sizeof(line) - 8) break;
    ++shown;
  }
  Serial.printf("%s（總長 %u ms）\n", line, (unsigned)(len * 1000 / SAMPLE_RATE));
}

uint32_t s_clip_n = 0;     // 滿幅（|x| ≥ 32000）的樣本數
int32_t  s_clip_first = -1; // 第一個滿幅樣本的位置（ms）
int32_t  s_first100 = 0;    // 前 100ms 的峰值
int32_t  s_rest = 0;        // 100ms 之後的峰值

int32_t scanPeak(const int16_t* buf, size_t len) {
  int32_t peak = 0, head = 0;
  const size_t head_len = (len < SAMPLE_RATE / 2) ? len : SAMPLE_RATE / 2;
  const size_t n100 = SAMPLE_RATE / 10;
  s_clip_n = 0; s_clip_first = -1; s_first100 = 0; s_rest = 0;
  for (size_t i = 0; i < len; ++i) {
    int32_t v = buf[i] < 0 ? -(int32_t)buf[i] : (int32_t)buf[i];
    if (v > peak) peak = v;
    if (i < head_len && v > head) head = v;
    if (i < n100) { if (v > s_first100) s_first100 = v; }
    else          { if (v > s_rest)     s_rest = v; }
    if (v >= 32000) {
      ++s_clip_n;
      if (s_clip_first < 0) s_clip_first = (int32_t)(i * 1000 / SAMPLE_RATE);
    }
  }
  s_peak = peak / 32768.0f;
  s_head_peak = head / 32768.0f;
  return peak;
}

// 數位正規化：把峰值拉到接近滿幅。比調大喇叭音量好——放大的是訊號本身，
// 不是 DAC 與功率放大器輸出端的所有東西（含它們自己的底噪）。
void normalize(int16_t* buf, size_t len, int32_t peak) {
  if (peak < NORM_MIN_PEAK) return;
  float g = (TARGET_PEAK * 32767.0f) / (float)peak;
  if (g <= 1.0f) return;
  if (g > NORM_MAX_GAIN) g = NORM_MAX_GAIN;
  for (size_t i = 0; i < len; ++i) {
    int32_t v = (int32_t)(buf[i] * g);
    if (v >  32767) v =  32767;
    if (v < -32768) v = -32768;
    buf[i] = (int16_t)v;
  }
  Serial.printf("[audio] 正規化增益 ×%.2f\n", g);
}

// 頭尾各做一小段淡入淡出：波形從零平滑長起來、再平滑回零，
// 消掉「資料從零突然跳到訊號」造成的 click（與放大器的 pop 是兩回事）。
void applyFades(int16_t* buf, size_t len) {
  if (len < FADE_IN + FADE_OUT + 1) return;
  for (size_t i = 0; i < FADE_IN; ++i) {
    buf[i] = (int16_t)((int32_t)buf[i] * (int32_t)i / (int32_t)FADE_IN);
  }
  for (size_t i = 0; i < FADE_OUT; ++i) {
    size_t j = len - 1 - i;
    buf[j] = (int16_t)((int32_t)buf[j] * (int32_t)i / (int32_t)FADE_OUT);
  }
}

// ── B3.6 Wi-Fi 干擾量測（v10：脈衝偵測）──────────────────────────────────
// 量的是「正規化之前」的原始資料，才能在不同設定之間比較。
// v9 的教訓：滋滋聲是短促脈衝，平均音量（RMS）被麥克風本身的穩定底噪蓋過，
// 四組只差 1～3dB 分不出來；10ms 自相關又永遠落在搜尋下限（相鄰格本來就像）。
// v10 改成直接找脈衝：
//   1. 一階差分 d[n] = x[n] - x[n-1]（簡易高通，壓掉低頻、凸顯「喀」一下的突變）
//   2. 每 2ms 一格算差分 RMS，取全段中位數當「平常」
//   3. 某格 > 中位數 × PULSE_K 就算脈衝格；連續的脈衝格合併成一次事件
//   4. 印每秒幾次、相鄰事件間隔的中位數、間隔落在 90～115ms 的比例
//      （省電模式的 beacon 間隔 102.4ms；若是它造成的，比例應明顯偏高）
//   5. 週期：2ms 包絡的自相關，在 40～300ms 找「真正的峰」（比左右鄰居都高），
//      找不到就印 0
// 斷流：錄音中麥克風隊列見底的次數（> 0 代表主迴圈被卡住、錄音有空檔）。
uint32_t s_starved = 0;
audio_io::LabelFn s_label_fn = nullptr;

constexpr size_t PF      = SAMPLE_RATE / 500;          // 2ms = 32 取樣
constexpr size_t PF_MAX  = CAP_SAMPLES / PF;           // 最多 3000 格
constexpr float  PULSE_K = 3.0f;                       // 比平常大 3 倍（約 +9.5dB）才算脈衝
float s_env[PF_MAX];    // 每格差分 RMS
float s_tmp[PF_MAX];    // 算中位數用的副本

float toDbfs(double rms) { return rms < 1e-9 ? -120.0f : (float)(20.0 * log10(rms / 32768.0)); }

float medianOf(float* v, size_t n) {
  std::nth_element(v, v + n / 2, v + n);
  return v[n / 2];
}

struct NoiseStats {
  float total_db = -120.0f;   // 整段 RMS
  float hp_db    = -120.0f;   // 差分（高頻）中位數，代表平常的嘶嘶底噪
  float per_sec  = 0.0f;      // 每秒脈衝事件數
  int   gap_ms   = 0;         // 相鄰事件間隔中位數
  int   near100  = 0;         // 間隔落在 90～115ms 的比例（%）
  int   period_ms = 0;        // 自相關真峰的位置；0＝沒有
  float period_r  = 0.0f;
};

NoiseStats analyze(const int16_t* buf, size_t len) {
  NoiseStats st;
  if (len < PF * 50) return st;
  double total = 0;
  for (size_t i = 0; i < len; ++i) total += (double)buf[i] * buf[i];
  st.total_db = toDbfs(sqrt(total / len));

  const size_t nf = len / PF;
  for (size_t f = 0; f < nf; ++f) {
    double e = 0;
    for (size_t i = f * PF; i < (f + 1) * PF; ++i) {
      const int32_t d = (i == 0) ? 0 : (int32_t)buf[i] - (int32_t)buf[i - 1];
      e += (double)d * d;
    }
    s_env[f] = (float)sqrt(e / PF);
    s_tmp[f] = s_env[f];
  }
  const float med = medianOf(s_tmp, nf);
  st.hp_db = toDbfs(med);

  // 脈衝事件：連續超過門檻的格算一次，記起點
  static uint16_t starts[PF_MAX];
  size_t ne = 0;
  bool in = false;
  const float th = med * PULSE_K;
  for (size_t f = 0; f < nf; ++f) {
    const bool hot = s_env[f] > th;
    if (hot && !in && ne < PF_MAX) starts[ne++] = (uint16_t)f;
    in = hot;
  }
  st.per_sec = ne * (float)SAMPLE_RATE / (float)len;
  if (ne >= 3) {
    size_t ng = 0, hit = 0;
    for (size_t k = 1; k < ne; ++k) {
      const float g = (starts[k] - starts[k - 1]) * 2.0f;   // ms
      s_tmp[ng++] = g;
      if (g >= 90.0f && g <= 115.0f) ++hit;
    }
    st.gap_ms  = (int)medianOf(s_tmp, ng);
    st.near100 = (int)(hit * 100 / ng);
  }

  // 週期：2ms 包絡去平均後的自相關，40～300ms 找真峰
  double mean = 0;
  for (size_t f = 0; f < nf; ++f) mean += s_env[f];
  mean /= nf;
  double var = 0;
  for (size_t f = 0; f < nf; ++f) { s_tmp[f] = s_env[f] - (float)mean; var += (double)s_tmp[f] * s_tmp[f]; }
  if (var <= 0) return st;
  constexpr size_t L0 = 20, L1 = 150;   // 40ms～300ms（每格 2ms）
  float r[L1 + 2];
  for (size_t lag = L0 - 1; lag <= L1 + 1 && lag < nf / 2; ++lag) {
    double acc = 0;
    for (size_t f = 0; f + lag < nf; ++f) acc += (double)s_tmp[f] * s_tmp[f + lag];
    r[lag] = (float)(acc / var * nf / (nf - lag));
  }
  for (size_t lag = L0; lag <= L1 && lag + 1 < nf / 2; ++lag) {
    if (r[lag] > r[lag - 1] && r[lag] >= r[lag + 1] && r[lag] > st.period_r) {
      st.period_r = r[lag];
      st.period_ms = (int)(lag * 2);
    }
  }
  return st;
}

}  // namespace

namespace audio_io {

void begin() {
  s_buf = (int16_t*)ps_malloc(CAP_SAMPLES * sizeof(int16_t));
  s_rx  = (int16_t*)ps_malloc(CAP_SAMPLES * sizeof(int16_t));
  s_ready = (s_buf != nullptr && s_rx != nullptr);
  if (!s_ready) {
    Serial.println("[audio] PSRAM 緩衝配置失敗");
    return;
  }
  memset(s_buf, 0, CAP_SAMPLES * sizeof(int16_t));
  micOn();   // 讓 ES8311 上電暖機，第一次按 A 才不會錄到一秒的無聲
  Serial.printf("[audio] 就緒 v%s（麥克風待機）\n", AUDIO_BUILD_TAG);
}

void startRecording() {
  if (!s_ready || s_recording) return;
  if (s_playing || M5.Speaker.isPlaying()) {   // 上一段還在播就先收掉
    M5.Speaker.stop();
    s_playing = false;
  }
  if (!M5.Mic.isRunning()) micOn();
  // 清零：沒錄到的尾段就是靜音，不會播出未初始化記憶體的雜訊
  memset(s_buf, 0, CAP_SAMPLES * sizeof(int16_t));
  s_queued = 0;
  s_len = 0;
  s_starved = 0;
  s_recording = true;
  s_t0 = millis();
  loop();   // 立刻排頭兩塊
  Serial.println("[audio] 開始錄音");
}

void loop() {
  if (!s_ready) return;

  // 錄音中：隊列未滿就預排下一塊。保持兩塊在飛，塊與塊之間才不會有空檔
  //（M5Unified 保證「前一塊完成時已排隊的請求」是連續的）。
  if (s_recording) {
    if (s_queued > 0 && s_queued + CHUNK <= CAP_SAMPLES && M5.Mic.isRecording() == 0) {
      ++s_starved;   // 隊列見底：上一圈到這一圈之間錄音停過
    }
    while (M5.Mic.isRecording() < 2 && s_queued + CHUNK <= CAP_SAMPLES) {
      if (!M5.Mic.record(s_buf + s_queued, CHUNK, SAMPLE_RATE)) break;
      s_queued += CHUNK;
    }
  }

  // 播完了就收喇叭、切回麥克風等下一次講話
  if (s_playing && !M5.Speaker.isPlaying()) {
    s_playing = false;
    micOn();   // 內含 speakerOff()：先靜音、斷放大器，再停 I2S
  }
}

size_t stopRecording(int16_t** out) {
  if (out) *out = nullptr;
  if (!s_recording || !s_ready) return 0;
  s_recording = false;

  // 實際長度用「按住多久」算；佇列可能比實際多排最多兩塊（那段是清零的靜音）
  const uint32_t held_ms = millis() - s_t0;
  size_t end = (size_t)((uint64_t)held_ms * SAMPLE_RATE / 1000);
  if (end > s_queued) end = s_queued;
  M5.Mic.end();
  // B3b-2 量測：按住的時間比實際排進去的錄音長很多 → 主迴圈中途被卡住、錄音停了
  {
    const uint32_t queued_ms = (uint32_t)(s_queued * 1000 / SAMPLE_RATE);
    if (s_queued + CHUNK <= CAP_SAMPLES && held_ms > queued_ms + 250) {
      Serial.printf("[audio] 錄音缺 %lu ms（按住 %lu ms、只錄到 %lu ms）→ 主迴圈被卡住\n",
                    (unsigned long)(held_ms - queued_ms), (unsigned long)held_ms,
                    (unsigned long)queued_ms);
    }
  }

  printClipPositions(s_buf, end);   // 診斷：切之前的滿幅位置

  const size_t a = TRIM_START_MS * SAMPLE_RATE / 1000;
  const size_t cut = TRIM_END_MS * SAMPLE_RATE / 1000;
  const size_t b = (end > cut) ? end - cut : 0;
  if (b <= a + FADE_IN + FADE_OUT) {         // 按太短，切完不剩
    Serial.printf("[audio] 太短（%u ms），不送不播\n", (unsigned)held_ms);
    s_len = 0;
    micOn();
    return 0;
  }
  int16_t* p = s_buf + a;
  s_len = b - a;

  despike(p, s_len);
  scanPeak(p, s_len);
  Serial.printf("[audio v%s] 按住 %u ms，切頭尾後 %u ms，峰值 %.3f，滿幅 %u 個\n",
                AUDIO_BUILD_TAG, (unsigned)held_ms,
                (unsigned)(s_len * 1000 / SAMPLE_RATE), s_peak, (unsigned)s_clip_n);
  {
    const NoiseStats st = analyze(p, s_len);
    Serial.printf("[audio v%s] 脈衝 %.1f 次/秒｜間隔中位 %d ms｜約100ms %d%%｜週期峰 %d ms (r=%.2f)｜"
                  "整段 %.1f dBFS｜高頻底噪 %.1f dBFS｜斷流 %u｜%s pga=0x%02X\n",
                  AUDIO_BUILD_TAG, st.per_sec, st.gap_ms, st.near100, st.period_ms, st.period_r,
                  st.total_db, st.hp_db, (unsigned)s_starved,
                  s_label_fn ? s_label_fn() : "", s_pga);
  }

  micOn();   // 等上傳／回傳的空檔先回麥克風待機，播放時再切喇叭
  if (out) *out = p;
  return s_len;
}

bool playBuffer(int16_t* buf, size_t samples) {
  if (!s_ready || buf == nullptr || samples < FADE_IN + FADE_OUT + 1) return false;
  if (M5.Mic.isRunning()) M5.Mic.end();     // ES8311 同時只能麥克風或喇叭其一
  int32_t peak = 0;
  for (size_t i = 0; i < samples; ++i) {
    int32_t v = buf[i] < 0 ? -(int32_t)buf[i] : (int32_t)buf[i];
    if (v > peak) peak = v;
  }
  normalize(buf, samples, peak);
  applyFades(buf, samples);
  speakerOn();
  M5.Speaker.playRaw(buf, samples, SAMPLE_RATE);
  s_playing = true;
  return true;
}

int16_t* rxBuffer() { return s_rx; }
size_t rxCapacityBytes() { return s_rx ? CAP_SAMPLES * sizeof(int16_t) : 0; }

bool isPlaying() { return M5.Speaker.isPlaying(); }
uint32_t recordedSamples() { return (uint32_t)s_len; }
float lastPeak() { return s_peak; }
float headPeak() { return s_head_peak; }

void setPga(uint8_t reg14) {
  if (reg14 < PGA_MIN) reg14 = PGA_MIN;
  if (reg14 > PGA_MAX) reg14 = PGA_MAX;
  s_pga = reg14;
  if (M5.Mic.isRunning()) {
    M5.In_I2C.writeRegister8(ES8311_ADDR, ES8311_REG_PGA, s_pga, I2C_FREQ);
  }
  Serial.printf("[diag] PGA → 0x%02X（+%d dB）\n", s_pga, (s_pga - PGA_MIN) * 3);
}
uint8_t pga() { return s_pga; }
void setDiagLabel(LabelFn fn) { s_label_fn = fn; }

}  // namespace audio_io
