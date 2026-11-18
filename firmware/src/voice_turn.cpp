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

enum class St : uint8_t { Idle, Recording, Uploading, Waiting, Playing };

constexpr size_t   FRAME_BYTES      = 3200;   // 100ms：16kHz × 2 bytes × 0.1s
constexpr int      FRAMES_PER_LOOP  = 2;      // 每圈最多送幾塊，避免主迴圈被 TLS 寫入卡住
// 送出 stop 後「多久沒收到任何回傳資料」就放棄。B3b-2 v13 實測固定 8 秒會誤判：
// 6 秒錄音在熱點上要下載約 7.8 秒；改成看有沒有進展。
constexpr uint32_t WAIT_IDLE_MS     = 5000;

St       s_st = St::Idle;
int16_t* s_up = nullptr;      // 要上傳的音訊（audio_io 的錄音緩衝）
size_t   s_up_bytes = 0;
size_t   s_sent = 0;
uint32_t s_up_crc = 0;
size_t   s_rx_bytes = 0;      // 收到的位元組（ADPCM 時是壓縮後的大小）
size_t   s_rx_samples = 0;    // 解出來的取樣數（寫進 rxBuffer）
uint32_t s_rx_crc = 0;
// B3b-3 下載格式：s_want＝要求（序列埠 f0/f1/f2 可切），s_down＝中控回 format 確認的
enum class Fmt : uint8_t { Pcm16, Adpcm, Mulaw };
const char* fmtName(Fmt f) { return f == Fmt::Adpcm ? "adpcm" : (f == Fmt::Mulaw ? "mulaw" : "pcm16"); }
// 預設 μ-law（B3b-3 實聽：μ-law 與原音聽不出差、ADPCM 較糊；下載 16KB/s，熱點約 25KB/s 夠用）
Fmt      s_want = Fmt::Mulaw;
Fmt      s_down = Fmt::Pcm16;
bool     s_rx_overflow = false;
uint32_t s_t_release = 0;     // 放開 A 的時刻（算來回時間）
uint32_t s_t_wait = 0;        // 送出 stop 或最後一次收到回傳資料的時刻（算逾時）

// ── B3b-2 量測：主迴圈每圈多久（相鄰兩次 voice_turn::loop 的間隔）──
uint32_t s_last_loop_us = 0, s_lp_n = 0, s_lp_max = 0, s_phase_t0 = 0;
uint64_t s_lp_sum = 0;
void perfReset() {
  s_lp_n = 0; s_lp_sum = 0; s_lp_max = 0; s_phase_t0 = millis();
  net_hub::perfReset();
}
// 印這個階段的量測，然後歸零給下一個階段
void perfPrint(const char* phase) {
  Serial.printf("[perf] %s %lu ms｜主迴圈 平均 %.1f／最長 %lu ms（%lu 圈）｜%s\n", phase,
                (unsigned long)(millis() - s_phase_t0),
                s_lp_n ? (float)(s_lp_sum / s_lp_n) / 1000.0f : 0.0f,
                (unsigned long)(s_lp_max / 1000), (unsigned long)s_lp_n, net_hub::perfLine());
  perfReset();
}

// CRC32（IEEE，與 zlib 相同）。逐位元算法：192KB 約十幾 ms，只在一輪結束時算一次。
uint32_t crc32Update(uint32_t crc, const uint8_t* p, size_t n) {
  crc = ~crc;
  while (n--) {
    crc ^= *p++;
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

void toIdle() {
  s_st = St::Idle;
  ui_expression::setMood(Mood::Idle);
  net_hub::voiceActive(false);   // B3.6：一輪結束才開回 Wi-Fi 省電
}

void play(int16_t* buf, size_t bytes, const char* what) {
  if (audio_io::playBuffer(buf, bytes / sizeof(int16_t))) {
    Serial.printf("[voice] 播放%s（%u ms）\n", what, (unsigned)(bytes / 32));
    s_st = St::Playing;
    ui_expression::setMood(Mood::Speaking);
  } else {
    toIdle();
  }
}

// 退回本機回放：中控沒連上、上傳失敗、中途斷線
void fallbackLocal(const char* why) {
  Serial.printf("[voice] %s → 改本機回放\n", why);
  play(s_up, s_up_bytes, "本機錄音");
}

void onBin(const uint8_t* data, size_t len) {
  if (s_st != St::Waiting && s_st != St::Uploading) return;   // 不是這一輪的，丟掉
  int16_t* rx = audio_io::rxBuffer();
  const size_t cap = audio_io::rxCapacityBytes() / sizeof(int16_t);
  if (!rx) { s_rx_overflow = true; return; }
  if (s_down == Fmt::Adpcm) {
    // B3b-3：一則訊息含多個固定 804 bytes 的幀（最後一幀可能較短），逐幀解壓接在後面
    for (size_t o = 0; o < len; o += adpcm::FRAME_BYTES) {
      const size_t fl = (len - o < adpcm::FRAME_BYTES) ? len - o : adpcm::FRAME_BYTES;
      const size_t n = adpcm::decodeFrame(data + o, fl, rx + s_rx_samples, cap - s_rx_samples);
      if (n == 0 && fl > adpcm::HEADER_BYTES) s_rx_overflow = true;
      s_rx_samples += n;
    }
  } else if (s_down == Fmt::Mulaw) {
    for (size_t k = 0; k < len && s_rx_samples < cap; ++k) rx[s_rx_samples++] = adpcm::mulawToPcm(data[k]);
    if (s_rx_samples >= cap && len) s_rx_overflow = true;
  } else {
    if (s_rx_bytes + len > cap * sizeof(int16_t)) { s_rx_overflow = true; return; }
    memcpy((uint8_t*)rx + s_rx_bytes, data, len);
    s_rx_samples = (s_rx_bytes + len) / sizeof(int16_t);
  }
  s_rx_bytes += len;
  s_t_wait = millis();   // 有進展就重新計時
}

// 解壓結果對原始錄音的訊噪比（SNR，dB）：越高越接近原音
float snrDb(const int16_t* ref, const int16_t* got, size_t n) {
  double sig = 0, err = 0;
  for (size_t i = 0; i < n; ++i) {
    const double e = (double)got[i] - ref[i];
    sig += (double)ref[i] * ref[i];
    err += e * e;
  }
  if (err <= 0) return 99.0f;
  return (float)(10.0 * log10(sig / err));
}

void onFormat(const char* up, const char* down) {
  s_down = strcmp(down, "adpcm") == 0 ? Fmt::Adpcm : (strcmp(down, "mulaw") == 0 ? Fmt::Mulaw : Fmt::Pcm16);
  Serial.printf("[voice] 中控確認格式：上傳 %s、下載 %s\n", up, down);
}

void onEnd(uint32_t n, uint32_t pcm_bytes) {
  if (s_st != St::Waiting) return;
  perfPrint("等待回傳");
  const uint32_t rt = millis() - s_t_release;
  if (s_down != Fmt::Pcm16) {
    const size_t up_samples = s_up_bytes / sizeof(int16_t);
    const size_t m = s_rx_samples < up_samples ? s_rx_samples : up_samples;
    Serial.printf("[voice] 下載 %s %u bytes（中控報 %lu，原始 %lu）→ 解出 %u 取樣（上傳 %u）｜壓縮比 %.2f｜SNR %.1f dB%s｜來回 %lu ms\n",
                  fmtName(s_down), (unsigned)s_rx_bytes, (unsigned long)n, (unsigned long)pcm_bytes,
                  (unsigned)s_rx_samples, (unsigned)up_samples,
                  s_rx_bytes ? (float)s_up_bytes / (float)s_rx_bytes : 0.0f,
                  m ? snrDb(s_up, audio_io::rxBuffer(), m) : 0.0f,
                  s_rx_overflow ? "｜收回超量或幀錯誤！" : "", (unsigned long)rt);
  } else {
    s_rx_crc = crc32Update(0, (const uint8_t*)audio_io::rxBuffer(), s_rx_bytes);
    const bool same = !s_rx_overflow && s_rx_bytes == s_up_bytes && s_rx_crc == s_up_crc;
    Serial.printf("[voice] 下載 PCM：上傳 %u bytes CRC %08lX｜收回 %u bytes CRC %08lX（中控報 %lu）｜%s｜來回 %lu ms\n",
                  (unsigned)s_up_bytes, (unsigned long)s_up_crc,
                  (unsigned)s_rx_bytes, (unsigned long)s_rx_crc, (unsigned long)n,
                  same ? "一致" : (s_rx_overflow ? "收回超量" : "不一致！"), (unsigned long)rt);
  }
  if (s_rx_samples == 0) { Serial.println("[voice] 中控沒有回傳音訊"); toIdle(); return; }
  play(audio_io::rxBuffer(), s_rx_samples * sizeof(int16_t), s_down == Fmt::Adpcm ? "中控回傳（ADPCM 解壓）" :
       (s_down == Fmt::Mulaw ? "中控回傳（μ-law 解壓）" : "中控回傳（PCM）"));
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
  s_want = f == 1 ? Fmt::Adpcm : (f == 2 ? Fmt::Mulaw : Fmt::Pcm16);
  Serial.printf("[diag] 下載格式 → %s（下一輪生效）\n", fmtName(s_want));
}

void onPress() {
  // 上一輪還在跑（上傳／等待／播放中）就直接放棄，開始新的一輪
  if (s_st == St::Playing) M5.Speaker.stop();
  net_hub::voiceActive(true);    // B3.6：錄放音期間關 Wi-Fi 省電，否則有滋滋聲
  audio_io::startRecording();
  ui_expression::setMood(Mood::Listening);
  s_down = Fmt::Pcm16;   // 等中控回 format 確認才用壓縮格式解（舊版中控不回 → 照 PCM 處理）
  if (net_hub::linkUp()) net_hub::sendStartFmt("pcm16", fmtName(s_want));
  else Serial.println("[voice] 中控未連線（放開後會改本機回放）");
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
  s_up_crc = crc32Update(0, (const uint8_t*)p, s_up_bytes);
  s_sent = 0;
  if (!net_hub::linkUp()) { fallbackLocal("中控未連線"); return; }
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
    if (s_st == St::Playing) M5.Speaker.stop();
    Serial.println("[voice] 離開 Desktop，中止這一輪");
    s_st = St::Idle;
    net_hub::voiceActive(false);
    return;
  }

  switch (s_st) {
    case St::Uploading: {
      if (!net_hub::linkUp()) { fallbackLocal("上傳中斷線"); break; }
      for (int i = 0; i < FRAMES_PER_LOOP && s_sent < s_up_bytes; ++i) {
        size_t len = s_up_bytes - s_sent;
        if (len > FRAME_BYTES) len = FRAME_BYTES;
        if (!net_hub::sendBinary((const uint8_t*)s_up + s_sent, len)) {
          fallbackLocal("上傳失敗");
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
        s_t_wait = millis();
        s_st = St::Waiting;
      }
      break;
    }
    case St::Waiting:
      if (!net_hub::linkUp()) { fallbackLocal("等待回傳時斷線"); break; }
      if (millis() - s_t_wait > WAIT_IDLE_MS) {
        Serial.printf("[voice] %lu ms 沒有新的回傳資料（已收 %u／%u bytes），放棄這一輪\n",
                      (unsigned long)WAIT_IDLE_MS, (unsigned)s_rx_bytes, (unsigned)s_up_bytes);
        toIdle();
      }
      break;
    case St::Playing:
      if (!audio_io::isPlaying()) toIdle();
      break;
    default:
      break;
  }
}

}  // namespace voice_turn
