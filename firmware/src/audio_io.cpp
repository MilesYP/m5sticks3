#include "audio_io.h"

#include <M5Unified.h>

namespace {

constexpr uint32_t SAMPLE_RATE = 16000;
constexpr size_t   CAP_SAMPLES = SAMPLE_RATE * 6;   // 最長 6 秒
constexpr size_t   CHUNK       = 1600;              // 每塊 0.1 秒

int16_t* s_buf = nullptr;
bool     s_ready = false;
bool     s_recording = false;
size_t   s_len = 0;        // 已完成的取樣數

}  // namespace

namespace audio_io {

void begin() {
  s_buf = (int16_t*)ps_malloc(CAP_SAMPLES * sizeof(int16_t));
  s_ready = (s_buf != nullptr);
  if (!s_ready) Serial.println("[audio] PSRAM 緩衝配置失敗");
}

void startRecording() {
  if (!s_ready) return;
  M5.Speaker.end();          // 先關喇叭，釋放 ES8311
  M5.Mic.begin();            // 切到麥克風
  s_len = 0;
  s_recording = true;
  M5.Mic.record(s_buf, CHUNK, SAMPLE_RATE);   // 第一塊
  Serial.println("[audio] 開始錄音");
}

void feedRecording() {
  if (!s_recording || !s_ready) return;
  if (M5.Mic.isRecording()) return;           // 這塊還在錄
  s_len += CHUNK;                             // 這塊完成
  if (s_len + CHUNK > CAP_SAMPLES) return;    // 滿了，停止再排新塊（等放開播放）
  M5.Mic.record(s_buf + s_len, CHUNK, SAMPLE_RATE);
}

void stopRecordingAndPlay() {
  if (!s_recording || !s_ready) return;
  s_recording = false;
  while (M5.Mic.isRecording()) { delay(1); }   // 等最後一塊錄完
  s_len += CHUNK;
  if (s_len > CAP_SAMPLES) s_len = CAP_SAMPLES;
  M5.Mic.end();              // 關麥克風
  M5.Speaker.begin();        // 切到喇叭
  Serial.printf("[audio] 錄到 %u 取樣，開始播放\n", (unsigned)s_len);
  if (s_len > 0) M5.Speaker.playRaw(s_buf, s_len, SAMPLE_RATE);
}

bool isPlaying() { return M5.Speaker.isPlaying(); }
uint32_t recordedSamples() { return (uint32_t)s_len; }

}  // namespace audio_io
