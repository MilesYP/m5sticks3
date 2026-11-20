#include "voice_turn.h"

#include <M5Unified.h>
#include <string.h>
#include <math.h>

#include "adpcm.h"
#include "config.h"
#include "audio_io.h"
#include "mode_manager.h"
#include "net_hub.h"
#include "ui_expression.h"

namespace {

enum class St : uint8_t { Idle, Recording, Flushing, Receiving };

// 「多久沒收到任何回傳資料」就不再等（已收到的照樣播完）。B3b-2 實測固定總時長會誤判，改看進展。
constexpr uint32_t WAIT_IDLE_MS       = 5000;
// B3c：gpt 模式放開後要等「補靜音＋模型反應」才有第一則回覆，開口前最多等 20 秒；開口後 8 秒沒資料才放棄
// （中控會在輸出停頓超過門檻時送 audio_end，這裡只是保險）
constexpr uint32_t GPT_WAIT_FIRST_MS  = 20000;
constexpr uint32_t GPT_WAIT_IDLE_MS   = 8000;
// B3c 實機（11-20）：GPT-Live 的回覆照「即時速度」送來，預緩衝 0.3 秒遇到熱點延遲抖動（RTT 0.5～1 秒）
// 就見底斷音（每輪 1～3 次、共 1.2～2 秒）→ gpt 模式先存 1.2 秒再開始播（多等約 0.9 秒換不斷音）
constexpr uint32_t GPT_PREBUF_MS      = 1200;
constexpr uint32_t FLUSH_LINK_LOST_MS = 15000;   // 放開後連不回中控多久就放棄這輪（B3b-5 定案）
constexpr uint32_t UP_REPORT_MS       = 500;     // 錄音／送尾巴時每 0.5 秒印一次上傳進度
constexpr size_t   DEC_CHUNK          = 1600;    // 解碼暫存：一次處理 1600 取樣
constexpr uint32_t SR                 = 16000;

St       s_st = St::Idle;
uint32_t s_turn_id = 0;
size_t   s_rec_samples = 0;   // 切頭尾後的錄音長度
uint32_t s_sent_samples = 0;  // 上傳完成時，裝置送出的取樣數（跟中控報的比對）
size_t   s_rx_bytes = 0;      // 收到的位元組（壓縮後的大小）
size_t   s_rx_samples = 0;    // 解出來的 16kHz 取樣數
bool     s_rx_overflow = false;
bool     s_ended = false;     // 不會再有資料（收到 audio_end、斷線或逾時）
bool     s_spoke = false;     // 已切到 Speaking
uint32_t s_t_press = 0, s_t_release = 0;
uint32_t s_t_wait = 0;        // 送出 stop 或最後一次收到資料的時刻（算逾時）
uint32_t s_t_first = 0, s_t_last = 0;   // 第一／最後一則回傳資料到達的時刻（算下載速度）
uint32_t s_t_link_lost = 0;   // Flushing 時斷線的時刻
uint32_t s_t_report = 0, s_rep_tx = 0, s_tx0 = 0;

// 下載格式：s_want＝要求（序列埠 f0～f3），s_down＝中控回 format 確認的
enum class Fmt : uint8_t { Pcm16, Adpcm, Mulaw, Adpcm8k };
const char* fmtName(Fmt f) {
  switch (f) {
    case Fmt::Adpcm:   return "adpcm";
    case Fmt::Mulaw:   return "mulaw";
    case Fmt::Adpcm8k: return "adpcm8k";
    default:           return "pcm16";
  }
}
// 下載預設 ADPCM 8kHz（B3b-4 定案，約 4KB/s）；上傳預設 μ-law（B3b-5 定案，16KB/s，OpenAI 本身接受的電話格式）
// 11-20 B3c 實聽：ADPCM 8kHz 有「老電話沙沙聲」（頻寬只到 4kHz）→ Miles 選 μ-law 16kHz（f2，需 16KB/s；網路差時 f1／f3 備用）
Fmt      s_want = Fmt::Mulaw;
Fmt      s_down = Fmt::Pcm16;
bool     s_up_mulaw = true;
bool     s_pace_rt = false;
bool     s_gpt = true;           // B3c：預設接 GPT-Live（m0 改 echo）
bool     s_turn_gpt = true;      // 這一輪實際用的模式（按下 A 時決定）
uint32_t s_text_user = 0, s_text_bot = 0;   // 這一輪收到的逐字稿 bytes

// ── B4 實機補（11-20）：Friday 的字幕對齊聲音 ──
// 中控給每段字標上「對應回覆音訊第幾 ms」，先存在這裡，喇叭播到那裡才顯示。
constexpr size_t   CAP_POOL = 2048;
constexpr int      CAP_SEGS = 64;
struct Seg { uint32_t ms; uint16_t off, len; };
char     s_pool[CAP_POOL];
Seg      s_segs[CAP_SEGS];
int      s_seg_head = 0, s_seg_n = 0;
size_t   s_pool_used = 0;

void capReset() { s_seg_head = 0; s_seg_n = 0; s_pool_used = 0; }
void capShow(const Seg& g) {
  char tmp[400];
  const size_t n = g.len < sizeof(tmp) - 1 ? g.len : sizeof(tmp) - 1;
  memcpy(tmp, s_pool + g.off, n);
  tmp[n] = '\0';
  ui_expression::convText(true, tmp);
}
// 顯示「播到的位置 ≥ 標記」的字；all＝全部顯示（播完／結束時）
void capRelease(bool all) {
  if (!all && !audio_io::streamStarted()) return;   // 喇叭還沒出聲：一個字都先不顯示
  const uint32_t played = audio_io::streamPlayedMs();
  while (s_seg_n > 0 && (all || s_segs[s_seg_head].ms <= played)) {
    capShow(s_segs[s_seg_head]);
    s_seg_head = (s_seg_head + 1) % CAP_SEGS;
    --s_seg_n;
  }
  if (s_seg_n == 0) s_pool_used = 0;
}
bool capPush(const char* str, uint32_t ms) {
  const size_t n = strlen(str);
  if (s_seg_n >= CAP_SEGS || s_pool_used + n > CAP_POOL) return false;
  memcpy(s_pool + s_pool_used, str, n);
  s_segs[(s_seg_head + s_seg_n) % CAP_SEGS] = {ms, (uint16_t)s_pool_used, (uint16_t)n};
  s_pool_used += n;
  ++s_seg_n;
  return true;
}
adpcm::Upsampler2x s_up2x;

// ── 量測：主迴圈每圈多久（相鄰兩次 voice_turn::loop 的間隔）──
uint32_t s_last_loop_us = 0, s_lp_n = 0, s_lp_max = 0, s_phase_t0 = 0;
uint64_t s_lp_sum = 0;
void perfReset() {
  s_lp_n = 0; s_lp_sum = 0; s_lp_max = 0; s_phase_t0 = millis();
  net_hub::perfReset();
}
void perfPrint(const char* phase) {
  Serial.printf("[perf] %s %lu ms｜主迴圈 平均 %.1f／最長 %lu ms（%lu 圈）｜%s\n", phase,
                (unsigned long)(millis() - s_phase_t0),
                s_lp_n ? (float)(s_lp_sum / s_lp_n) / 1000.0f : 0.0f,
                (unsigned long)(s_lp_max / 1000), (unsigned long)s_lp_n, net_hub::perfLine());
  perfReset();
}

void toIdle() {
  capRelease(true);   // B4：還沒顯示的字一次補上
  s_st = St::Idle;
  ui_expression::setSpeakLevel(-1.0f);
  ui_expression::setListenLevel(-1.0f);
  ui_expression::convEnd();      // B4：對話區過幾秒收起
  ui_expression::setMood(Mood::Idle);
  net_hub::voiceActive(false);   // B3.6：一輪結束才開回 Wi-Fi 省電
}

// [up] 已錄｜已送｜待送｜上傳速度（速度＝這 0.5 秒內實際送出的 bytes，含重送）
void reportUp() {
  const uint32_t now = millis();
  const uint32_t from = audio_io::recFrom();
  const uint32_t lim = audio_io::recLimit();
  const uint32_t rec = lim > from ? lim - from : 0;
  const uint32_t sent = net_hub::uploadSentSamples();
  const uint32_t bps = s_up_mulaw ? 1 : 2;
  const uint32_t tx = net_hub::txBytesTotal();
  const uint32_t dt = now - s_t_report;
  const float rate = dt ? (tx - s_rep_tx) / 1024.0f / (dt / 1000.0f) : 0.0f;
  float lo = 0, hi = 0;
  audio_io::vadStats(&lo, &hi);
  Serial.printf("[up] 已錄 %.1f s｜已送 %.1f s｜待送 %.1f KB｜上傳 %.1f KB/s｜人聲指標 %.1f～%.1f dBFS（門檻 %.0f）｜靜音 %.1f s%s%s\n",
                rec / (float)SR, sent / (float)SR, (rec > sent ? rec - sent : 0) * bps / 1024.0f, rate,
                lo, hi, cfg::VOICE_VAD_DBFS, audio_io::silenceMs() / 1000.0f,
                audio_io::recArmed() ? "" : "｜未滿最短時間，還沒上傳",
                net_hub::linkUp() ? "" : "｜中控斷線中（連回後從頭重送）");
  s_t_report = now;
  s_rep_tx = tx;
}

// stop 已送出：開始等回傳
void enterReceiving() {
  const uint32_t now = millis();
  s_sent_samples = net_hub::uploadSentSamples();
  const uint32_t bytes = net_hub::txBytesTotal() - s_tx0;
  const uint32_t span = now - s_t_press;
  Serial.printf("[voice] 上傳完成：%lu 取樣（%.1f s，%s）｜放開後 %lu ms 送完｜整輪送出 %.1f KB、平均 %.1f KB/s｜"
                "開機以來重送 %lu 次、被覆蓋 %lu 取樣\n",
                (unsigned long)s_sent_samples, s_sent_samples / (float)SR, s_up_mulaw ? "mulaw" : "pcm16",
                (unsigned long)(now - s_t_release), bytes / 1024.0f,
                span ? bytes / 1024.0f / (span / 1000.0f) : 0.0f,
                (unsigned long)net_hub::uploadResends(), (unsigned long)net_hub::uploadLostSamples());
  perfPrint("送尾巴");
  s_ended = false; s_spoke = false;
  s_t_first = 0; s_t_last = 0;
  s_t_wait = now;
  s_up2x.reset();
  audio_io::setTurnPrebufferMs(s_turn_gpt ? GPT_PREBUF_MS : 0);
  audio_io::streamBegin();
  s_st = St::Receiving;
}

// 解好的 16kHz 音訊：寫進串流緩衝（播放），並留前 6 秒到 rxBuffer（播完算 SNR 驗收）
void pushPcm(const int16_t* p, size_t n) {
  if (n == 0) return;
  int16_t* rx = audio_io::rxBuffer();
  const size_t cap = audio_io::rxCapacityBytes() / sizeof(int16_t);
  if (rx && s_rx_samples < cap) {
    const size_t m = (cap - s_rx_samples < n) ? cap - s_rx_samples : n;
    memcpy(rx + s_rx_samples, p, m * sizeof(int16_t));
  }
  if (audio_io::streamWrite(p, n) < n) s_rx_overflow = true;
  s_rx_samples += n;
}

// 收件反壓：這則音訊解開後放得進串流緩衝嗎？放不下就先留在 net_hub 的佇列
bool canTake(size_t len) {
  if (s_st == St::Flushing && net_hub::uploadDone()) return true;   // 下面 onBin 會先切到 Receiving；緩衝是空的
  if (s_st != St::Receiving || s_ended) return true;                // 會被丟掉，不必等
  size_t need;
  const size_t frames = (len + adpcm::FRAME_BYTES - 1) / adpcm::FRAME_BYTES;
  switch (s_down) {
    case Fmt::Adpcm8k: need = frames * adpcm::FRAME_SAMPLES * 2 + 8; break;
    case Fmt::Adpcm:   need = frames * adpcm::FRAME_SAMPLES; break;
    case Fmt::Mulaw:   need = len; break;
    default:           need = len / 2; break;
  }
  return audio_io::streamRoom() >= need;
}

void onBin(const uint8_t* data, size_t len) {
  if (s_st == St::Flushing && net_hub::uploadDone()) enterReceiving();   // 回傳比主迴圈切狀態還早到
  if (s_st != St::Receiving || s_ended) return;   // 不是這一輪的，丟掉
  static int16_t dec[DEC_CHUNK];
  static int16_t up[DEC_CHUNK * 2 + 8];
  if (s_t_first == 0) s_t_first = millis();
  if (s_down == Fmt::Adpcm || s_down == Fmt::Adpcm8k) {
    // 一則訊息含多個固定 804 bytes 的幀（最後一幀可能較短），逐幀解壓
    for (size_t o = 0; o < len; o += adpcm::FRAME_BYTES) {
      const size_t fl = (len - o < adpcm::FRAME_BYTES) ? len - o : adpcm::FRAME_BYTES;
      const size_t n = adpcm::decodeFrame(data + o, fl, dec, DEC_CHUNK);
      if (n == 0 && fl > adpcm::HEADER_BYTES) s_rx_overflow = true;
      if (s_down == Fmt::Adpcm8k) pushPcm(up, s_up2x.push(dec, n, up));   // 8kHz → 16kHz
      else pushPcm(dec, n);
    }
  } else if (s_down == Fmt::Mulaw) {
    for (size_t o = 0; o < len; o += DEC_CHUNK) {
      const size_t n = (len - o < DEC_CHUNK) ? len - o : DEC_CHUNK;
      for (size_t k = 0; k < n; ++k) dec[k] = adpcm::mulawToPcm(data[o + k]);
      pushPcm(dec, n);
    }
  } else {
    // PCM16：訊息位址不一定對齊 2 bytes，先複製再用
    for (size_t o = 0; o + 1 < len; o += DEC_CHUNK * 2) {
      const size_t nb = (len - o < DEC_CHUNK * 2) ? ((len - o) & ~(size_t)1) : DEC_CHUNK * 2;
      memcpy(dec, data + o, nb);
      pushPcm(dec, nb / 2);
    }
  }
  s_rx_bytes += len;
  s_t_last = s_t_wait = millis();   // 有進展就重新計時
}

// 不會再有資料：把升取樣器裡剩的推出去，讓串流播完剩下的
void endStream(const char* why) {
  if (s_ended) return;
  s_ended = true;
  if (s_down == Fmt::Adpcm8k) {
    int16_t tail[8];
    pushPcm(tail, s_up2x.flush(tail));
  }
  audio_io::streamEnd();
  if (why) Serial.printf("[voice] %s（已收 %u bytes，%lu ms 音訊）→ 已收到的播完就結束\n", why,
                         (unsigned)s_rx_bytes, (unsigned long)(s_rx_samples / 16));
}

// 解壓結果對原始錄音的訊噪比（SNR，dB）。中控有正規化，先估出增益再比，才不會把音量差當成誤差。
float snrDb(const int16_t* ref, const int16_t* got, size_t n) {
  double sxy = 0, sxx = 0;
  for (size_t i = 0; i < n; ++i) { sxy += (double)ref[i] * got[i]; sxx += (double)ref[i] * ref[i]; }
  if (sxx <= 0) return 0.0f;
  const double k = sxy / sxx;
  double sig = 0, err = 0;
  for (size_t i = 0; i < n; ++i) {
    const double r = k * ref[i], e = (double)got[i] - r;
    sig += r * r; err += e * e;
  }
  if (err <= 0) return 99.0f;
  return (float)(10.0 * log10(sig / err));
}

void onFormat(const char* up, const char* down) {
  if (strcmp(down, "adpcm8k") == 0) s_down = Fmt::Adpcm8k;
  else if (strcmp(down, "adpcm") == 0) s_down = Fmt::Adpcm;
  else if (strcmp(down, "mulaw") == 0) s_down = Fmt::Mulaw;
  else s_down = Fmt::Pcm16;
  Serial.printf("[voice] 中控確認格式：上傳 %s、下載 %s\n", up, down);
  if (strcmp(up, s_up_mulaw ? "mulaw" : "pcm16") != 0)
    Serial.printf("[voice] ⚠ 中控不認得上傳格式 %s（舊版中控？要先 wrangler deploy）\n", s_up_mulaw ? "mulaw" : "pcm16");
}

void onEnd(uint32_t n, uint32_t pcm_bytes, float gain, const char* fmt) {
  if (s_st == St::Flushing && net_hub::uploadDone()) enterReceiving();
  if (s_st != St::Receiving || s_ended) return;
  endStream(nullptr);
  if (fmt && fmt[0] && strcmp(fmt, fmtName(s_down)) != 0) {
    Serial.printf("[voice] ⚠ 格式不符：中控實際送 %s、裝置照 %s 解（播出來會是雜音）\n", fmt, fmtName(s_down));
  }
  perfPrint("等待回傳");
  // B3b-5 驗收：中控收到的取樣數必須等於裝置送出的（不掉資料）
  const uint32_t hub_samples = pcm_bytes / 2;
  Serial.printf("[voice] 完整性：裝置送出 %lu 取樣、中控收到 %lu 取樣 → %s\n", (unsigned long)s_sent_samples,
                (unsigned long)hub_samples, hub_samples == s_sent_samples ? "一致 ✓" : "不一致 ✗");
  // 下載速度：第一則到最後一則之間（全速送時＝網路實際速度）。格式需求＝每秒音訊要幾 bytes。
  const float audio_s = s_rx_samples / (float)SR;
  const float need = audio_s > 0 ? s_rx_bytes / 1024.0f / audio_s : 0.0f;
  const uint32_t span = s_t_last - s_t_first;
  const float rate = span > 0 ? (s_rx_bytes / 1024.0f) / (span / 1000.0f) : 0.0f;
  const int16_t* ref = nullptr;
  size_t m = audio_io::recWindow(&ref);
  const size_t cap = audio_io::rxCapacityBytes() / sizeof(int16_t);
  if (m > s_rx_samples) m = s_rx_samples;
  if (m > cap) m = cap;
  if (s_turn_gpt) {
    // B3c：回覆是 GPT 的聲音、不是原音 → 不算 SNR
    Serial.printf("[voice] 下載 %s %u bytes（中控報 %lu）→ 解出 %u 取樣＝回覆 %.1f 秒｜逐字稿 你 %lu／Friday %lu bytes%s\n",
                  fmtName(s_down), (unsigned)s_rx_bytes, (unsigned long)n, (unsigned)s_rx_samples, s_rx_samples / (float)SR,
                  (unsigned long)s_text_user, (unsigned long)s_text_bot, s_rx_overflow ? "｜緩衝滿或幀錯誤！" : "");
  } else {
  Serial.printf("[voice] 下載 %s %u bytes（中控報 %lu）→ 解出 %u 取樣｜中控增益 ×%.2f｜前 %u ms SNR %.1f dB（含上傳 %s、下載壓縮）%s\n",
                fmtName(s_down), (unsigned)s_rx_bytes, (unsigned long)n, (unsigned)s_rx_samples, gain,
                (unsigned)(m / 16), m ? snrDb(ref, audio_io::rxBuffer(), m) : 0.0f,
                s_up_mulaw ? "μ-law" : "PCM", s_rx_overflow ? "｜緩衝滿或幀錯誤！" : "");
  }
  if (span > 0) {
    Serial.printf("[voice] 實測下載 %.1f KB/s（%lu ms 收完）｜這個格式需要 %.1f KB/s｜餘裕 %.1f 倍｜節奏 %s\n",
                  rate, (unsigned long)span, need, need > 0 ? rate / need : 0.0f, s_pace_rt ? "rt" : "fast");
  }
}

// B3c：中控回報錯誤
void onError(const char* code, const char* s) {
  if (strcmp(code, "quota") == 0) {
    Serial.printf("[voice] 今日額度已用完（%s）→ 這輪不送，回待機\n", s);
    ui_expression::convText(true, "（今日額度已用完）");
    if (s_st == St::Recording) audio_io::stopRecording();
    if (s_st == St::Recording || s_st == St::Flushing) net_hub::uploadAbort();
    if (s_st == St::Receiving) audio_io::streamAbort();
    if (s_st != St::Idle) toIdle();
    return;
  }
  Serial.printf("[voice] 中控錯誤 code=%s：%s\n", code[0] ? code : "-", s);
}

// B4：逐字稿片段 → 畫面對話區；序列埠逐段印
void onText(const char* role, const char* s, uint32_t ms) {
  if (s_st == St::Idle) return;   // 上一輪的晚到文字
  const bool bot = strcmp(role, "assistant") == 0;
  (bot ? s_text_bot : s_text_user) += strlen(s);
  // Friday 的字：存起來等播到 ms 才顯示（序列埠照樣馬上印）；存不下就直接顯示
  if (!bot || !capPush(s, ms)) ui_expression::convText(bot, s);
  Serial.printf("[text] %s：%s\n", bot ? "Friday" : "你", s);
}

}  // namespace

namespace voice_turn {

void begin() {
  net_hub::onBinary(onBin);
  net_hub::onAudioEnd(onEnd);
  net_hub::onFormat(onFormat);
  net_hub::onBinGate(canTake);
  net_hub::onError(onError);
  net_hub::onText(onText);
}

void setGptMode(bool gpt) {
  s_gpt = gpt;
  Serial.printf("[diag] 對話模式 → %s（下一輪生效）\n", gpt ? "gpt（GPT-Live）" : "echo（原樣回傳，回歸測試）");
}
bool gptMode() { return s_gpt; }

bool busy() { return s_st != St::Idle; }
bool faceWanted() {
  if (s_st == St::Idle) return false;
  if (s_st == St::Recording && !audio_io::recArmed()) return false;   // 可能只是短按（切頁），先不搶畫面
  return true;
}
const char* stateName() {
  switch (s_st) {
    case St::Recording: return "REC";
    case St::Flushing:  return "SEND";
    case St::Receiving: return "PLAY";
    default:            return "IDLE";
  }
}

void setDownFormat(int f) {
  s_want = f == 1 ? Fmt::Adpcm : (f == 2 ? Fmt::Mulaw : (f == 3 ? Fmt::Adpcm8k : Fmt::Pcm16));
  Serial.printf("[diag] 下載格式 → %s（下一輪生效）\n", fmtName(s_want));
}

void setUpFormat(int f) {
  s_up_mulaw = (f != 0);
  Serial.printf("[diag] 上傳格式 → %s（下一輪生效）\n", s_up_mulaw ? "mulaw" : "pcm16");
}

void setPaceRealtime(bool rt) {
  s_pace_rt = rt;
  Serial.printf("[diag] 中控送法 → %s（下一輪生效）\n", rt ? "rt 照真實速度（壓力測試）" : "fast 全速");
}

void onPress() {
  // 上一輪還在跑（送尾巴／等待／播放中）就直接放棄，開始新的一輪
  // （startRecording 會停掉串流並換代，網路 task 的舊上傳工作看到就停）
  net_hub::voiceActive(true);    // B3.6：錄放音期間關 Wi-Fi 省電，否則有滋滋聲
  audio_io::startRecording();
  ui_expression::setMood(Mood::Listening);
  s_down = Fmt::Pcm16;   // 等中控回 format 確認才用壓縮格式解
  ++s_turn_id;
  s_turn_gpt = s_gpt;
  // 下載格式照序列埠 f0～f3（預設 f3＝ADPCM 8kHz）。gpt 模式中控支援 f1 ADPCM 16kHz／f2 μ-law 16kHz／f3，
  // 不支援的（f0 PCM）中控會退回 ADPCM 8kHz 並在 format 回覆裡告知（11-20 加：比較「老電話沙沙聲」）
  net_hub::uploadBegin(s_turn_id, s_up_mulaw ? "mulaw" : "pcm16", fmtName(s_want),
                       s_pace_rt ? "rt" : "fast", s_turn_gpt ? "gpt" : "echo");
  s_text_user = 0; s_text_bot = 0;
  capReset();
  ui_expression::convBegin();    // B4：開對話區（清掉上一輪的字）
  if (!net_hub::linkUp()) Serial.println("[voice] 中控未連線：先錄著，連上後從頭送");
  s_rx_bytes = 0; s_rx_samples = 0; s_rx_overflow = false;
  s_t_press = millis();
  s_t_report = s_t_press;
  s_tx0 = net_hub::txBytesTotal();
  s_rep_tx = s_tx0;
  s_st = St::Recording;
  perfReset();
}

bool onRelease() {
  if (s_st != St::Recording) return false;
  s_rec_samples = audio_io::stopRecording();
  s_t_release = millis();
  perfPrint("錄音");
  if (s_rec_samples == 0) {             // 誤觸（太短）：start 還沒送出，中控完全不知道
    net_hub::uploadAbort();
    toIdle();
    return true;
  }
  net_hub::uploadFinish();
  ui_expression::setMood(Mood::Thinking);
  s_t_link_lost = 0;
  s_st = St::Flushing;
  reportUp();
  return false;
}

void loop() {
  {
    const uint32_t now = micros();
    if (s_last_loop_us != 0) {
      const uint32_t d = now - s_last_loop_us;
      ++s_lp_n; s_lp_sum += d; if (d > s_lp_max) s_lp_max = d;
    }
    s_last_loop_us = now;
  }
  // 中途離開 Desktop（切模式、急停）→ 中止這一輪
  if (s_st != St::Idle && mode_manager::current() != Mode::Desktop) {
    if (s_st == St::Recording) audio_io::stopRecording();
    if (s_st == St::Recording || s_st == St::Flushing) net_hub::uploadAbort();
    if (s_st == St::Receiving) audio_io::streamAbort();
    Serial.println("[voice] 離開 Desktop，中止這一輪");
    s_st = St::Idle;
    net_hub::voiceActive(false);
    return;
  }

  const uint32_t now = millis();
  switch (s_st) {
    case St::Recording:
      ui_expression::setListenLevel(audio_io::recLevel());   // A4：聆聽表情的 o 嘴與聲波跟你的音量
      if (now - s_t_report >= UP_REPORT_MS) reportUp();
      // B3b-5 補：連續沒有人聲 → 自動當作放開 A（之後真的放開時 onRelease 會因狀態不是 Recording 而略過）
      if (audio_io::silenceMs() >= cfg::VOICE_AUTO_END_SILENCE_MS) {
        Serial.printf("[voice] 連續 %.1f 秒沒有人聲 → 自動結束這輪（當作放開 A）\n", audio_io::silenceMs() / 1000.0f);
        onRelease();
      }
      break;
    case St::Flushing:
      if (net_hub::uploadDone()) { enterReceiving(); break; }
      if (!net_hub::linkUp()) {
        if (s_t_link_lost == 0) s_t_link_lost = now | 1;
        else if (now - s_t_link_lost > FLUSH_LINK_LOST_MS) {
          Serial.printf("[voice] 放開後 %lu 秒連不回中控 → 這輪結束\n", (unsigned long)(FLUSH_LINK_LOST_MS / 1000));
          net_hub::uploadAbort();
          toIdle();
          break;
        }
      } else {
        s_t_link_lost = 0;
      }
      if (now - s_t_report >= UP_REPORT_MS) reportUp();
      break;
    case St::Receiving:
      if (!s_spoke && audio_io::streamStarted()) {
        s_spoke = true;
        ui_expression::setMood(Mood::Speaking);
        Serial.printf("[voice] 開始播放：放開 A 後 %lu ms（第一則回傳在放開後 %lu ms 到達）\n",
                      (unsigned long)(audio_io::streamStartedAt() - s_t_release),
                      s_t_first ? (unsigned long)(s_t_first - s_t_release) : 0UL);
      }
      if (!s_ended && !net_hub::linkUp()) endStream("回答途中斷線");
      {
        const uint32_t lim = !s_turn_gpt ? WAIT_IDLE_MS : (s_t_first ? GPT_WAIT_IDLE_MS : GPT_WAIT_FIRST_MS);
        if (!s_ended && now - s_t_wait > lim) {
          static char why[48];
          snprintf(why, sizeof(why), "%lu 秒沒有新的回傳資料", (unsigned long)(lim / 1000));
          endStream(why);
        }
      }
      // B4：嘴巴跟著播放音量（播放 task 每 100ms 一塊算的 RMS）
      if (s_spoke) ui_expression::setSpeakLevel(audio_io::playLevel());
      capRelease(false);   // B4：字幕跟著播放進度出現
      if (!audio_io::streamActive()) {
        Serial.println(audio_io::streamReport());
        toIdle();
      }
      break;
    default:
      break;
  }
}

}  // namespace voice_turn
