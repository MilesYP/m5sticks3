#include "voice_turn.h"

#include <M5Unified.h>
#include <string.h>
#include <math.h>

#include "adpcm.h"
#include "audio_io.h"
#include "mode_manager.h"
#include "net_hub.h"
#include "ui_expression.h"

namespace {

// Receiving＝送出 stop 之後：等中控回傳＋邊收邊播，直到播完
enum class St : uint8_t { Idle, Recording, Uploading, Receiving };

constexpr size_t   FRAME_BYTES      = 3200;   // 上傳 100ms：16kHz × 2 bytes × 0.1s
constexpr int      FRAMES_PER_LOOP  = 2;      // 每圈最多送幾塊，避免主迴圈被 TLS 寫入卡住
// 「多久沒收到任何回傳資料」就不再等（已收到的照樣播完）。B3b-2 實測固定總時長會誤判，改看進展。
constexpr uint32_t WAIT_IDLE_MS     = 5000;
constexpr size_t   DEC_CHUNK        = 1600;   // 解碼暫存：一次處理 1600 取樣

St       s_st = St::Idle;
int16_t* s_up = nullptr;      // 要上傳的音訊（audio_io 的錄音緩衝，未正規化）
size_t   s_up_bytes = 0;
size_t   s_sent = 0;
size_t   s_rx_bytes = 0;      // 收到的位元組（壓縮後的大小）
size_t   s_rx_samples = 0;    // 解出來的 16kHz 取樣數
bool     s_rx_overflow = false;
bool     s_ended = false;     // 不會再有資料（收到 audio_end、斷線或逾時）
bool     s_spoke = false;     // 已切到 Speaking
uint32_t s_t_release = 0;     // 放開 A 的時刻
uint32_t s_t_wait = 0;        // 送出 stop 或最後一次收到資料的時刻（算逾時）
uint32_t s_t_first = 0, s_t_last = 0;   // 第一／最後一則回傳資料到達的時刻（算下載速度）

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
// B3b-4（Miles 定案）：預設 ADPCM 8kHz（約 4KB/s）。11-19 熱點實測下載只有約 8.8KB/s，μ-law（16KB/s）跟不上即時。
Fmt      s_want = Fmt::Adpcm8k;
Fmt      s_down = Fmt::Pcm16;
bool     s_pace_rt = false;
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
  s_st = St::Idle;
  ui_expression::setMood(Mood::Idle);
  net_hub::voiceActive(false);   // B3.6：一輪結束才開回 Wi-Fi 省電
}

// 解好的 16kHz 音訊：寫進串流緩衝（播放），並留一份到 rxBuffer（播完算 SNR 驗收）
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

void onBin(const uint8_t* data, size_t len) {
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
}

void onEnd(uint32_t n, uint32_t pcm_bytes, float gain, const char* fmt) {
  if (s_st != St::Receiving || s_ended) return;
  endStream(nullptr);
  // 11-19 爆音：中控 DO 休眠後忘了格式、照 pcm16 回傳，裝置照 ADPCM 解。中控已修；這裡留著當警報
  if (fmt && fmt[0] && strcmp(fmt, fmtName(s_down)) != 0) {
    Serial.printf("[voice] ⚠ 格式不符：中控實際送 %s、裝置照 %s 解（播出來會是雜音）\n", fmt, fmtName(s_down));
  }
  perfPrint("等待回傳");
  // 下載速度：第一則到最後一則之間（全速送時＝網路實際速度）。格式需求＝每秒音訊要幾 bytes。
  const float audio_s = s_rx_samples / 16000.0f;
  const float need = audio_s > 0 ? s_rx_bytes / 1024.0f / audio_s : 0.0f;
  const uint32_t span = s_t_last - s_t_first;
  const float rate = span > 0 ? (s_rx_bytes / 1024.0f) / (span / 1000.0f) : 0.0f;
  const size_t up_samples = s_up_bytes / sizeof(int16_t);
  const size_t cap = audio_io::rxCapacityBytes() / sizeof(int16_t);
  size_t m = s_rx_samples < up_samples ? s_rx_samples : up_samples;
  if (m > cap) m = cap;
  Serial.printf("[voice] 下載 %s %u bytes（中控報 %lu，原始 %lu）→ 解出 %u 取樣（上傳 %u）｜中控增益 ×%.2f｜SNR %.1f dB%s%s\n",
                fmtName(s_down), (unsigned)s_rx_bytes, (unsigned long)n, (unsigned long)pcm_bytes,
                (unsigned)s_rx_samples, (unsigned)up_samples, gain,
                m ? snrDb(s_up, audio_io::rxBuffer(), m) : 0.0f,
                s_down == Fmt::Adpcm8k ? "（含 8kHz 頻寬損失）" : "",
                s_rx_overflow ? "｜緩衝滿或幀錯誤！" : "");
  if (span > 0) {
    Serial.printf("[voice] 實測下載 %.1f KB/s（%lu ms 收完）｜這個格式需要 %.1f KB/s｜餘裕 %.1f 倍｜節奏 %s\n",
                  rate, (unsigned long)span, need, need > 0 ? rate / need : 0.0f, s_pace_rt ? "rt" : "fast");
  } else {
    Serial.println("[voice] 只收到一則，無法算下載速度");
  }
}

}  // namespace

namespace voice_turn {

void begin() {
  net_hub::onBinary(onBin);
  net_hub::onAudioEnd(onEnd);
  net_hub::onFormat(onFormat);
}

bool busy() { return s_st != St::Idle; }

void setDownFormat(int f) {
  s_want = f == 1 ? Fmt::Adpcm : (f == 2 ? Fmt::Mulaw : (f == 3 ? Fmt::Adpcm8k : Fmt::Pcm16));
  Serial.printf("[diag] 下載格式 → %s（下一輪生效）\n", fmtName(s_want));
}

void setPaceRealtime(bool rt) {
  s_pace_rt = rt;
  Serial.printf("[diag] 中控送法 → %s（下一輪生效）\n", rt ? "rt 照真實速度（壓力測試）" : "fast 全速");
}

void onPress() {
  // 上一輪還在跑（上傳／等待／播放中）就直接放棄，開始新的一輪（startRecording 會停掉串流）
  net_hub::voiceActive(true);    // B3.6：錄放音期間關 Wi-Fi 省電，否則有滋滋聲
  audio_io::startRecording();
  ui_expression::setMood(Mood::Listening);
  s_down = Fmt::Pcm16;   // 等中控回 format 確認才用壓縮格式解（舊版中控不回 → 照 PCM 處理）
  if (net_hub::linkUp()) net_hub::sendStartFmt("pcm16", fmtName(s_want), s_pace_rt ? "rt" : "fast");
  else Serial.println("[voice] 中控未連線（放開後這輪直接結束）");
  s_rx_bytes = 0; s_rx_samples = 0; s_rx_overflow = false;
  s_st = St::Recording;
  perfReset();
}

void onRelease() {
  if (s_st != St::Recording) return;
  int16_t* p = nullptr;
  const size_t n = audio_io::stopRecording(&p);
  s_t_release = millis();
  perfPrint("錄音");
  if (n == 0 || p == nullptr) {             // 按太短
    if (net_hub::linkUp()) net_hub::sendStop();   // 讓中控也結束這輪（會回 audio_end n=0）
    toIdle();
    return;
  }
  s_up = p;
  s_up_bytes = n * sizeof(int16_t);
  s_sent = 0;
  if (!net_hub::linkUp()) {   // B3b-4（Miles 選定）：不再本機回放
    Serial.println("[voice] 中控未連線 → 這輪結束（不回放）");
    toIdle();
    return;
  }
  ui_expression::setMood(Mood::Thinking);
  s_st = St::Uploading;
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
    if (s_st == St::Recording) audio_io::stopRecording(nullptr);
    if (s_st == St::Receiving) audio_io::streamAbort();
    Serial.println("[voice] 離開 Desktop，中止這一輪");
    s_st = St::Idle;
    net_hub::voiceActive(false);
    return;
  }

  switch (s_st) {
    case St::Uploading: {
      if (!net_hub::linkUp()) {
        Serial.printf("[voice] 上傳中斷線（已送 %u／%u bytes）→ 這輪結束（不回放）\n",
                      (unsigned)s_sent, (unsigned)s_up_bytes);
        toIdle();
        break;
      }
      for (int i = 0; i < FRAMES_PER_LOOP && s_sent < s_up_bytes; ++i) {
        size_t len = s_up_bytes - s_sent;
        if (len > FRAME_BYTES) len = FRAME_BYTES;
        if (!net_hub::sendBinary((const uint8_t*)s_up + s_sent, len)) {
          Serial.println("[voice] 上傳失敗 → 這輪結束（不回放）");
          toIdle();
          return;
        }
        s_sent += len;
      }
      if (s_sent >= s_up_bytes) {
        net_hub::sendStop();
        Serial.printf("[voice] 上傳完成 %u bytes（%u 塊），%lu ms\n", (unsigned)s_up_bytes,
                      (unsigned)((s_up_bytes + FRAME_BYTES - 1) / FRAME_BYTES),
                      (unsigned long)(millis() - s_t_release));
        perfPrint("上傳");
        s_ended = false; s_spoke = false;
        s_t_first = 0; s_t_last = 0;
        s_t_wait = millis();
        s_up2x.reset();
        audio_io::streamBegin();
        s_st = St::Receiving;
      }
      break;
    }
    case St::Receiving:
      if (!s_spoke && audio_io::streamStarted()) {
        s_spoke = true;
        ui_expression::setMood(Mood::Speaking);
        Serial.printf("[voice] 開始播放：放開 A 後 %lu ms（第一則回傳在放開後 %lu ms 到達）\n",
                      (unsigned long)(audio_io::streamStartedAt() - s_t_release),
                      s_t_first ? (unsigned long)(s_t_first - s_t_release) : 0UL);
      }
      if (!s_ended && !net_hub::linkUp()) endStream("回答途中斷線");
      if (!s_ended && millis() - s_t_wait > WAIT_IDLE_MS) endStream("5 秒沒有新的回傳資料");
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
