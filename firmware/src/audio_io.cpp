#include "audio_io.h"

#include <M5Unified.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <atomic>

#include "config.h"

// 燒錄版本標記：開機會印出來，用來確認板子上跑的是哪一版
#define AUDIO_BUILD_TAG "30-p2k"
namespace audio_io { const char* buildTag() { return AUDIO_BUILD_TAG; } }

namespace {

constexpr uint32_t SAMPLE_RATE = 16000;
constexpr size_t   CHUNK       = SAMPLE_RATE / 10;   // 1600 取樣 = 100ms，一塊
// B3b-5：錄音改環形緩衝、不設上限（Miles 定案）。4MB＝128 秒；送出去的部分可以被覆蓋，
// 只有「斷線後從頭重送」與「網路停擺超過 128 秒」會受這個長度影響。容量必須是塊（CHUNK）的整數倍。
constexpr size_t   REC_RING    = SAMPLE_RATE * 128;
constexpr size_t   ANALYZE_MAX = SAMPLE_RATE * 6;    // 放開後的量測與 SNR 只看前 6 秒
static_assert(REC_RING % CHUNK == 0, "REC_RING 必須是 CHUNK 的整數倍");

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

// ── 串流播放的淡入淡出 ──────────────────────────────────────────────
constexpr size_t  FADE_IN       = SAMPLE_RATE / 200;  // 5ms
constexpr size_t  FADE_OUT      = SAMPLE_RATE / 100;  // 10ms

int16_t* s_rec = nullptr;   // B3b-5：錄音環形緩衝（REC_RING 個取樣）
int16_t* s_rx  = nullptr;   // B3b-2：中控回傳的音訊（只留前 6 秒算 SNR）
bool     s_ready = false;
bool     s_recording = false;
size_t   s_queued = 0;      // 已排進麥克風隊列的取樣數（這一輪的絕對位置）
// 給網路 task 讀的錄音進度（B3b-5）
std::atomic<uint32_t> s_rec_gen{0};
std::atomic<uint32_t> s_rec_queued{0};   // = s_queued
std::atomic<uint32_t> s_rec_done{0};     // 麥克風已寫完的取樣數
std::atomic<uint32_t> s_rec_end{0};      // 放開後：切尾後的結尾
std::atomic<bool>     s_rec_ended{true};
// B3b-5 補：誤觸防護
std::atomic<bool>     s_rec_armed{false};    // 已按滿最短時間，可以開始上傳
std::atomic<uint32_t> s_silence_ms{0};       // 連續沒有人聲多久
uint32_t s_vad_pos = 0;                      // 下一塊要做 VAD 的位置
float    s_vad_lo = 0.0f, s_vad_hi = -120.0f;
float    s_vad_last = -120.0f;                // A4：最新一塊的人聲指標（聆聽表情的嘴巴、聲波）

// A3：待機收音（睡著時聽聲音、量測用）。一次只排一塊 50ms：按 A 時最多擋住正式錄音 50ms，
// 落在本來就會切掉的開頭 60ms 內。抓到的聲音只算指標，不存、不上傳。
constexpr size_t SB_CHUNK = SAMPLE_RATE / 20;   // 800 取樣＝50ms
int16_t  s_sb_buf[SB_CHUNK];
bool     s_sb_on = false;
bool     s_sb_inflight = false;
float    s_sb_lo = 0.0f, s_sb_hi = -120.0f;
uint32_t s_sb_n = 0;

// 一塊（100ms）的人聲指標：一階差分 RMS（dBFS），與量測行的「高頻底噪」同一個算法
float chunkLevelDb(const int16_t* p, size_t n) {
  double e = 0;
  for (size_t i = 1; i < n; ++i) { const int32_t d = (int32_t)p[i] - (int32_t)p[i - 1]; e += (double)d * d; }
  const double rms = sqrt(e / (n - 1));
  return rms < 1e-9 ? -120.0f : (float)(20.0 * log10(rms / 32768.0));
}
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

// ── B3b-4 邊收邊播：環形緩衝＋播放 task 自己餵 ─────────────────────────
// M5Unified 0.3.0 Speaker 的排隊行為（2026-11-19 查原始碼 Speaker_Class.inl 確認）：
//   - 每個虛擬聲道只有 2 格（播放中＋排隊 1 個），isPlaying(ch) 回 0／1／2。
//   - playRaw 不指定聲道會挑「沒在播的聲道」→ 兩塊會同時混音 → 一律指定聲道 0。
//   - 隊列滿時 playRaw 會原地等 → 呼叫前先確認 isPlaying(0) < 2。
//   - 排隊的下一塊在同一次 DMA 填充裡接上，不會有空隙；一塊播完前它的記憶體不能改寫。
//   - setBufferReleaseCallback：播放 task 讀完一塊時呼叫，裡面可以再 playRaw（不會等待），
//     但不可 begin()/end()；只能在 begin() 之前或 end() 之後設定。
constexpr uint8_t  STREAM_CH      = 0;
constexpr size_t   RING_SAMPLES   = SAMPLE_RATE * 8;     // 8 秒（256KB，PSRAM）
constexpr size_t   STREAM_BLOCK   = SAMPLE_RATE / 10;    // 每次交給喇叭 100ms
constexpr uint32_t REBUFFER_MS    = 1000;                // 斷音後補到這麼多才繼續
uint32_t s_prebuf_ms = 300;                              // 開始播之前先緩衝多少（b<ms> 可改）
uint32_t s_turn_prebuf_ms = 0;                           // B3c：這一輪另外指定（0＝用 s_prebuf_ms）
inline uint32_t prebufMs() { return s_turn_prebuf_ms ? s_turn_prebuf_ms : s_prebuf_ms; }

int16_t* s_ring = nullptr;
std::atomic<uint32_t> s_wr{0};       // 已寫入的取樣總數（只增不減，取餘數得位置）
std::atomic<uint32_t> s_rd{0};       // 已交給喇叭的取樣總數
std::atomic<uint32_t> s_fr{0};       // 喇叭已讀完、可以覆寫的取樣總數
std::atomic<uint8_t>  s_inflight{0}; // 交給喇叭還沒讀完的塊數（0～2）
uint32_t s_q_len[4];                 // 在喇叭裡的各塊長度（單一生產者／單一消費者的小佇列）
float    s_q_rms[4];                 // B4：各塊的 RMS（0～1，滿幅＝1），嘴巴開合用
std::atomic<float> s_play_rms{0.0f}; // B4：喇叭正在播的那一塊的 RMS
std::atomic<uint8_t> s_q_head{0}, s_q_tail{0};
std::atomic<bool> s_feed_lock{false};
volatile bool s_st_active = false, s_st_started = false, s_st_ended = false, s_st_starved = false;
// 統計
uint32_t s_st_t_begin = 0, s_st_t_started = 0, s_st_t_starve = 0;
uint32_t s_st_underruns = 0, s_st_starve_ms = 0, s_st_dropped = 0;
volatile uint32_t s_st_min_buf = 0xFFFFFFFF;   // 播放中「還沒播的量」最低值（取樣數）
uint32_t s_st_written = 0;

// 從環形緩衝拿資料交給喇叭，直到喇叭的 2 格隊列滿或沒資料。主迴圈與播放 task 都會呼叫，
// 用 try-lock 避免兩邊同時改讀取位置；搶不到就跳過（另一邊正在餵）。
void streamFeed() {
  if (s_feed_lock.exchange(true, std::memory_order_acquire)) return;
  while (s_st_active && M5.Speaker.isPlaying(STREAM_CH) < 2) {
    const uint32_t wr = s_wr.load(std::memory_order_acquire);
    const uint32_t rd = s_rd.load(std::memory_order_relaxed);
    const uint32_t avail = wr - rd;
    if (avail == 0) break;
    const uint32_t pos = rd % RING_SAMPLES;
    uint32_t len = avail;
    if (len > STREAM_BLOCK) len = STREAM_BLOCK;
    if (len > RING_SAMPLES - pos) len = RING_SAMPLES - pos;   // 不跨過緩衝尾端
    // 還在收、喇叭裡還有東西時，不送零碎小塊（等湊滿 100ms；喇叭讀完那塊時會再來餵）
    if (len < STREAM_BLOCK && !s_st_ended && s_inflight.load() > 0 && pos + len < RING_SAMPLES) break;
    if (!M5.Speaker.playRaw(s_ring + pos, len, SAMPLE_RATE, false, 1, STREAM_CH, false)) break;
    const uint8_t t = s_q_tail.load(std::memory_order_relaxed);
    s_q_len[t & 3] = len;
    {   // B4：這一塊的 RMS（100ms＝1600 取樣，計算量很小）
      const int16_t* b = s_ring + pos;
      float acc = 0;
      for (uint32_t k = 0; k < len; ++k) acc += (float)b[k] * b[k];
      s_q_rms[t & 3] = len ? sqrtf(acc / len) / 32768.0f : 0.0f;
      if (s_inflight.load() == 0) s_play_rms.store(s_q_rms[t & 3]);   // 喇叭原本是空的 → 這塊馬上開始播
    }
    s_q_tail.store((uint8_t)(t + 1), std::memory_order_release);
    s_inflight.fetch_add(1);
    s_rd.store(rd + len, std::memory_order_release);
  }
  // 斷音：還在收、喇叭沒東西、緩衝也空了
  if (s_st_active && s_st_started && !s_st_ended && !s_st_starved &&
      s_inflight.load() == 0 && s_wr.load() == s_rd.load()) {
    s_st_starved = true;
    ++s_st_underruns;
    s_st_t_starve = millis();
  }
  s_feed_lock.store(false, std::memory_order_release);
}

// 播放 task 讀完一塊時呼叫（不是中斷；保持簡短、不可 begin()/end()）
void onSpeakerRelease(void*, const void* data, uint8_t ch) {
  if (ch != STREAM_CH || !s_st_active || s_ring == nullptr) return;
  const int16_t* p = (const int16_t*)data;
  if (p < s_ring || p >= s_ring + RING_SAMPLES) return;   // 不是串流的塊（例如本機整段播放）
  const uint8_t h = s_q_head.load(std::memory_order_relaxed);
  if (h != s_q_tail.load(std::memory_order_acquire)) {
    s_fr.fetch_add(s_q_len[h & 3]);
    s_q_head.store((uint8_t)(h + 1), std::memory_order_release);
    if (s_inflight.load() > 0) s_inflight.fetch_sub(1);
    // B4：讀完一塊 → 下一塊（新的隊首）開始播
    s_play_rms.store(s_inflight.load() > 0 ? s_q_rms[(uint8_t)(h + 1) & 3] : 0.0f);
  }
  if (!s_st_ended) {   // 收尾階段不算（本來就會見底）
    const uint32_t ahead = s_wr.load() - s_fr.load();
    if (ahead < s_st_min_buf) s_st_min_buf = ahead;
  }
  streamFeed();
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
  M5.Speaker.setBufferReleaseCallback(nullptr, onSpeakerRelease);   // B3b-4：end() 之後、begin() 之前設定
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
constexpr size_t PF_MAX  = ANALYZE_MAX / PF;           // 最多 3000 格
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
  s_rec = (int16_t*)ps_malloc(REC_RING * sizeof(int16_t));      // B3b-5：4MB 錄音環形緩衝
  s_rx  = (int16_t*)ps_malloc(ANALYZE_MAX * sizeof(int16_t));
  s_ring = (int16_t*)ps_malloc(RING_SAMPLES * sizeof(int16_t));   // B3b-4 邊收邊播
  s_ready = (s_rec != nullptr && s_rx != nullptr && s_ring != nullptr);
  if (!s_ready) {
    Serial.println("[audio] PSRAM 緩衝配置失敗");
    return;
  }
  micOn();   // 讓 ES8311 上電暖機，第一次按 A 才不會錄到一秒的無聲
  Serial.printf("[audio] 就緒 v%s（麥克風待機）\n", AUDIO_BUILD_TAG);
}

void startRecording() {
  if (!s_ready || s_recording) return;
  if (s_st_active) streamAbort();             // B3b-4：上一輪回答還在播 → 停掉
  if (M5.Speaker.isPlaying()) M5.Speaker.stop();
  if (!M5.Mic.isRunning()) micOn();
  // B3b-5：先換代（網路 task 的舊上傳工作看到就停），再重設進度。
  // 不再清零整個緩衝（4MB 約要 100ms）：只會送真的錄到的部分，尾端未寫完的那幾毫秒落在切掉的 60ms 內。
  s_sb_inflight = false;                      // A3：待機收音那塊作廢（最多再佔隊列 50ms）
  s_rec_gen.fetch_add(1, std::memory_order_acq_rel);
  s_rec_armed.store(false, std::memory_order_release);
  s_rec_ended.store(false, std::memory_order_release);
  s_silence_ms.store(0);
  s_vad_pos = 0;
  s_vad_lo = 0.0f; s_vad_hi = -120.0f;
  s_rec_end.store(0);
  s_rec_done.store(0);
  s_rec_queued.store(0);
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
    if (s_queued > 0 && M5.Mic.isRecording() == 0) {
      ++s_starved;   // 隊列見底：上一圈到這一圈之間錄音停過
    }
    // B3b-5：寫進環形緩衝，繞回就覆蓋最舊的（網路 task 以 recOldest() 判斷）。不設上限。
    while (M5.Mic.isRecording() < 2) {
      if (!M5.Mic.record(s_rec + (s_queued % REC_RING), CHUNK, SAMPLE_RATE)) break;
      s_queued += CHUNK;
    }
    // 已寫完＝已排的減掉還在隊列裡的塊（麥克風寫完一塊才把它移出隊列，見 Mic_Class.inl）
    const size_t inq = M5.Mic.isRecording();
    s_rec_queued.store((uint32_t)s_queued, std::memory_order_release);
    const uint32_t done = (uint32_t)(s_queued > inq * CHUNK ? s_queued - inq * CHUNK : 0);
    s_rec_done.store(done, std::memory_order_release);
    // B3b-5 補：按滿最短時間才允許上傳
    if (!s_rec_armed.load(std::memory_order_relaxed) && millis() - s_t0 >= cfg::VOICE_MIN_PRESS_MS)
      s_rec_armed.store(true, std::memory_order_release);
    // B3b-5 補：新寫完的每一塊做人聲判斷，累計連續靜音時間
    while (s_vad_pos + CHUNK <= done) {
      const float db = chunkLevelDb(s_rec + (s_vad_pos % REC_RING), CHUNK);
      if (db < s_vad_lo) s_vad_lo = db;
      if (db > s_vad_hi) s_vad_hi = db;
      s_vad_last = db;
      if (db >= cfg::VOICE_VAD_DBFS) s_silence_ms.store(0);
      else s_silence_ms.store(s_silence_ms.load() + (uint32_t)(CHUNK * 1000 / SAMPLE_RATE));
      s_vad_pos += CHUNK;
    }
  }

  // A3 待機收音：沒在錄音、沒在播、麥克風開著才抓；其他時候放棄手上那塊
  if (s_sb_on && !s_recording && !s_st_active && M5.Mic.isRunning()) {
    if (s_sb_inflight && M5.Mic.isRecording() == 0) {
      const float db = chunkLevelDb(s_sb_buf, SB_CHUNK);
      if (db < s_sb_lo) s_sb_lo = db;
      if (db > s_sb_hi) s_sb_hi = db;
      ++s_sb_n;
      s_sb_inflight = false;
    }
    if (!s_sb_inflight && M5.Mic.isRecording() == 0) {
      s_sb_inflight = M5.Mic.record(s_sb_buf, SB_CHUNK, SAMPLE_RATE);
    }
  } else {
    s_sb_inflight = false;
  }

  // B3b-4 邊收邊播：緩衝夠了開始播、斷音後補夠再繼續、播完切回麥克風
  if (s_st_active) {
    const uint32_t now = millis();
    const uint32_t unplayed = s_wr.load() - s_fr.load();          // 還沒播（含喇叭裡的）
    const uint32_t queued   = s_wr.load() - s_rd.load();          // 還沒交給喇叭
    if (!s_st_started) {
      if (unplayed >= prebufMs() * (SAMPLE_RATE / 1000) || (s_st_ended && unplayed > 0)) {
        if (M5.Mic.isRunning()) M5.Mic.end();   // ES8311 同時只能麥克風或喇叭其一
        speakerOn();
        s_st_started = true;
        s_st_t_started = millis();
        streamFeed();
      } else if (s_st_ended) {                  // 一點資料都沒有就結束了
        s_st_active = false;
      }
    } else {
      if (s_st_starved) {
        if (queued >= REBUFFER_MS * (SAMPLE_RATE / 1000) || s_st_ended) {
          s_st_starve_ms += now - s_st_t_starve;
          s_st_starved = false;
          streamFeed();
        }
      } else {
        streamFeed();   // 保險：播放 task 搶不到鎖時，由主迴圈補餵
      }
      if (s_st_ended && s_wr.load() == s_rd.load() && s_inflight.load() == 0 && !M5.Speaker.isPlaying()) {
        s_st_active = false;
        micOn();        // 內含 speakerOff()：先靜音、斷放大器
      }
    }
  }

}

size_t stopRecording() {
  if (!s_recording || !s_ready) return 0;
  s_recording = false;

  // 結尾用「按住多久」算，但不少於已寫完的量（網路 task 可能已送到那裡），也不超過已排的量
  const uint32_t held_ms = millis() - s_t0;
  size_t end = (size_t)((uint64_t)held_ms * SAMPLE_RATE / 1000);
  const size_t done = s_rec_done.load();
  if (end < done) end = done;
  if (end > s_queued) end = s_queued;
  const size_t a = TRIM_START_MS * SAMPLE_RATE / 1000;
  const size_t cut = TRIM_END_MS * SAMPLE_RATE / 1000;
  const size_t b = (end > cut) ? end - cut : 0;
  if (held_ms < cfg::VOICE_MIN_PRESS_MS || b <= a + FADE_IN + FADE_OUT) {   // 誤觸：太短
    // A5-1（11-20 實機：短按切資訊頁「好遲鈍」）：誤觸不關麥克風、不重開。
    // 原本 Mic.end()＋micOn()（I2S 重裝、ES8311 重設、讀回）會卡主迴圈，切頁要等它做完才畫得出來。
    // 麥克風本來就該停在待機（對講機式），還在飛的 1～2 塊（≤200ms）寫完就停，資料不用；
    // 下一輪 startRecording 會等它們寫完才排新的塊（isRecording() < 2）。
    Serial.printf("[audio] 太短（%u ms < %lu ms）→ 當誤觸，不上傳\n", (unsigned)held_ms,
                  (unsigned long)cfg::VOICE_MIN_PRESS_MS);
    s_len = 0;
    s_rec_end.store((uint32_t)a, std::memory_order_release);
    s_rec_ended.store(true, std::memory_order_release);
    return 0;
  }
  M5.Mic.end();
  // 量測：按住的時間比實際排進去的錄音長很多 → 主迴圈中途被卡住、錄音停了
  {
    const uint32_t queued_ms = (uint32_t)((uint64_t)s_queued * 1000 / SAMPLE_RATE);
    if (held_ms > queued_ms + 250) {
      Serial.printf("[audio] 錄音缺 %lu ms（按住 %lu ms、只錄到 %lu ms）→ 主迴圈被卡住\n",
                    (unsigned long)(held_ms - queued_ms), (unsigned long)held_ms,
                    (unsigned long)queued_ms);
    }
  }

  s_len = b - a;
  s_rec_armed.store(true, std::memory_order_release);        // 夠長：按住時間剛好跨過門檻、主迴圈還沒標到時補標
  s_rec_end.store((uint32_t)b, std::memory_order_release);   // 先寫結尾，再標「已放開」（網路 task 依此順序讀）
  s_rec_ended.store(true, std::memory_order_release);

  // 診斷（放開後才做、只看前 6 秒；整段太長、開頭已被覆蓋就略過）
  const int16_t* w = nullptr;
  const size_t wn = recWindow(&w);
  if (wn) {
    printClipPositions(w, wn);
    scanPeak(w, wn);
    const NoiseStats st = analyze(w, wn);
    Serial.printf("[audio v%s] 按住 %u ms，切頭尾後 %u ms（量測看前 %u ms）｜峰值 %.3f｜滿幅 %u 個｜脈衝 %.1f 次/秒｜"
                  "週期峰 %d ms (r=%.2f)｜整段 %.1f dBFS｜高頻底噪 %.1f dBFS｜斷流 %u｜%s pga=0x%02X\n",
                  AUDIO_BUILD_TAG, (unsigned)held_ms, (unsigned)(s_len * 1000 / SAMPLE_RATE),
                  (unsigned)(wn * 1000 / SAMPLE_RATE), s_peak, (unsigned)s_clip_n, st.per_sec,
                  st.period_ms, st.period_r, st.total_db, st.hp_db, (unsigned)s_starved,
                  s_label_fn ? s_label_fn() : "", s_pga);
  } else {
    Serial.printf("[audio v%s] 按住 %u ms，切頭尾後 %u ms（開頭已被覆蓋，不做量測）｜斷流 %u\n",
                  AUDIO_BUILD_TAG, (unsigned)held_ms, (unsigned)(s_len * 1000 / SAMPLE_RATE),
                  (unsigned)s_starved);
  }

  micOn();   // 等上傳／回傳的空檔先回麥克風待機，播放時再切喇叭
  return s_len;
}

// ── B3b-5 錄音環形緩衝的讀取介面（網路 task 會呼叫，只讀 atomic 與固定指標）──
uint32_t recGen() { return s_rec_gen.load(std::memory_order_acquire); }
uint32_t recFrom() { return TRIM_START_MS * SAMPLE_RATE / 1000; }
bool recEnded() { return s_rec_ended.load(std::memory_order_acquire); }
uint32_t recLimit() {
  if (s_rec_ended.load(std::memory_order_acquire)) return s_rec_end.load(std::memory_order_acquire);
  const uint32_t d = s_rec_done.load(std::memory_order_acquire);
  const uint32_t tail = TRIM_END_MS * SAMPLE_RATE / 1000;   // 尾 60ms 先不送，放開時才知道要不要切
  return d > tail ? d - tail : 0;
}
uint32_t recOldest() {
  // 麥克風正在寫（與主迴圈即將排）的塊會覆蓋「一圈之前」的位置，多留兩塊餘裕
  const uint32_t q = s_rec_queued.load(std::memory_order_acquire) + 2 * CHUNK;
  return q > REC_RING ? q - REC_RING : 0;
}
const int16_t* recPtr(uint32_t abs, size_t* contig) {
  const size_t pos = abs % REC_RING;
  if (contig) *contig = REC_RING - pos;
  return s_rec + pos;
}
bool recArmed() { return s_rec_armed.load(std::memory_order_acquire); }
uint32_t silenceMs() { return s_silence_ms.load(); }
void vadStats(float* lo, float* hi) {
  *lo = s_vad_hi < -119.0f ? -120.0f : s_vad_lo;
  *hi = s_vad_hi;
  s_vad_lo = 0.0f; s_vad_hi = -120.0f;
}

size_t recWindow(const int16_t** p) {
  *p = nullptr;
  if (s_len == 0 || s_rec == nullptr) return 0;
  const size_t a = recFrom();
  if (s_queued > a + REC_RING) return 0;   // 開頭已被新錄音覆蓋
  *p = s_rec + a;
  return s_len < ANALYZE_MAX ? s_len : ANALYZE_MAX;
}

int16_t* rxBuffer() { return s_rx; }
size_t rxCapacityBytes() { return s_rx ? ANALYZE_MAX * sizeof(int16_t) : 0; }

bool isPlaying() { return M5.Speaker.isPlaying(); }

// ── A3 待機收音 ──
void setStandbyListen(bool on) {
  s_sb_on = on;
  if (!on) s_sb_inflight = false;
  s_sb_lo = 0.0f; s_sb_hi = -120.0f; s_sb_n = 0;
}
bool standbyListen() { return s_sb_on; }
void standbyStats(float* lo, float* hi, uint32_t* n) {
  *n = s_sb_n;
  *lo = s_sb_n ? s_sb_lo : -120.0f;
  *hi = s_sb_hi;
  s_sb_lo = 0.0f; s_sb_hi = -120.0f; s_sb_n = 0;
}
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

// ── B3b-4 邊收邊播 ──────────────────────────────────────────────────────
bool streamBegin() {
  if (!s_ready) return false;
  if (s_st_active) streamAbort();
  s_wr.store(0); s_rd.store(0); s_fr.store(0);
  s_inflight.store(0); s_q_head.store(0); s_q_tail.store(0);
  s_st_started = false; s_st_ended = false; s_st_starved = false;
  s_st_t_begin = millis(); s_st_t_started = 0;
  s_st_underruns = 0; s_st_starve_ms = 0; s_st_dropped = 0; s_st_written = 0;
  s_st_min_buf = 0xFFFFFFFF;
  s_st_active = true;
  return true;
}

size_t streamWrite(const int16_t* p, size_t n) {
  if (!s_st_active || s_st_ended || p == nullptr) return 0;
  const uint32_t wr = s_wr.load(std::memory_order_relaxed);
  const uint32_t room = RING_SAMPLES - (wr - s_fr.load(std::memory_order_acquire));
  size_t m = n < room ? n : room;
  if (m < n) s_st_dropped += (uint32_t)(n - m);
  for (size_t i = 0; i < m; ++i) {
    int32_t v = p[i];
    if (s_st_written + i < FADE_IN) v = v * (int32_t)(s_st_written + i) / (int32_t)FADE_IN;   // 開頭淡入
    s_ring[(wr + i) % RING_SAMPLES] = (int16_t)v;
  }
  s_st_written += (uint32_t)m;
  s_wr.store(wr + (uint32_t)m, std::memory_order_release);
  return m;
}

void streamEnd() {
  if (!s_st_active || s_st_ended) return;
  // 結尾淡出：只能改還沒交給喇叭的部分
  while (s_feed_lock.exchange(true, std::memory_order_acquire)) {}
  const uint32_t wr = s_wr.load(), rd = s_rd.load();
  const uint32_t pend = wr - rd;
  const uint32_t nfade = pend < FADE_OUT ? pend : FADE_OUT;
  for (uint32_t i = 0; i < nfade; ++i) {
    int16_t& v = s_ring[(wr - 1 - i) % RING_SAMPLES];
    v = (int16_t)((int32_t)v * (int32_t)i / (int32_t)FADE_OUT);
  }
  s_st_ended = true;
  s_feed_lock.store(false, std::memory_order_release);
}

void streamAbort() {
  if (!s_st_active) return;
  s_st_active = false;          // 先關，回呼看到就不再餵
  M5.Speaker.stop();
  micOn();                      // 內含 speakerOff()
}

bool streamActive() { return s_st_active; }
bool streamStarted() { return s_st_active && s_st_started; }
uint32_t streamStartedAt() { return s_st_t_started; }

void setTurnPrebufferMs(uint32_t ms) { s_turn_prebuf_ms = ms; }

void setPrebufferMs(uint32_t ms) {
  if (ms > 4000) ms = 4000;
  s_prebuf_ms = ms;
  Serial.printf("[diag] 預緩衝 → %lu ms（下一輪生效）\n", (unsigned long)ms);
}

// B4：這一輪已經「播完」的音訊長度（ms）＝喇叭讀完的取樣數（還沒開始播＝0）
uint32_t streamPlayedMs() {
  if (!s_st_started) return 0;
  return s_fr.load() / (SAMPLE_RATE / 1000);
}

// B4：嘴巴開合用的播放音量（0～1）。RMS 轉 dBFS 後把 −45～−12 dBFS 線性對到 0～1；沒在播＝0。
float recLevel() {
  if (!s_recording) return 0.0f;
  const float v = (s_vad_last - cfg::VOICE_VAD_DBFS) / 30.0f;
  return v < 0 ? 0 : (v > 1 ? 1 : v);
}

float playLevel() {
  if (!s_st_active || !s_st_started) return 0.0f;
  const float r = s_play_rms.load();
  if (r <= 0.0f) return 0.0f;
  const float db = 20.0f * log10f(r);
  float v = (db + 45.0f) / 33.0f;
  return v < 0 ? 0 : (v > 1 ? 1 : v);
}

size_t streamRoom() {
  if (!s_st_active) return RING_SAMPLES;
  return RING_SAMPLES - (s_wr.load() - s_fr.load());
}

const char* streamReport() {
  static char buf[200];
  const uint32_t ms_per = SAMPLE_RATE / 1000;
  snprintf(buf, sizeof(buf),
           "[stream] 收到 %lu ms 音訊｜送出 stop 後 %lu ms 開始播（預緩衝 %lu ms）｜播放中緩衝最低 %ld ms｜斷音 %lu 次共 %lu ms｜緩衝滿丟棄 %lu 取樣",
           (unsigned long)(s_st_written / ms_per),
           s_st_t_started ? (unsigned long)(s_st_t_started - s_st_t_begin) : 0UL, (unsigned long)prebufMs(),
           s_st_min_buf == 0xFFFFFFFF ? -1L : (long)(s_st_min_buf / ms_per),
           (unsigned long)s_st_underruns, (unsigned long)s_st_starve_ms, (unsigned long)s_st_dropped);
  return buf;
}

}  // namespace audio_io
