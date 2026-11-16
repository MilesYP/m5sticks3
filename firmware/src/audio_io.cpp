#include "audio_io.h"

#include <M5Unified.h>

namespace {

constexpr uint32_t SAMPLE_RATE = 16000;
constexpr size_t   CAP_SAMPLES = SAMPLE_RATE * 6;   // 最長 6 秒
constexpr uint8_t  SPK_VOLUME  = 220;               // 喇叭音量 0~255（放大回放）

int16_t* s_buf = nullptr;
bool     s_ready = false;
bool     s_recording = false;
uint32_t s_t0 = 0;        // 開始錄音時刻（用經過時間推算取樣數，避免分塊掉音）
size_t   s_len = 0;

}  // namespace

namespace audio_io {

void begin() {
  s_buf = (int16_t*)ps_malloc(CAP_SAMPLES * sizeof(int16_t));
  s_ready = (s_buf != nullptr);
  if (!s_ready) Serial.println("[audio] PSRAM 緩衝配置失敗");
}

void startRecording() {
  if (!s_ready) return;
  if (M5.Speaker.isEnabled()) M5.Speaker.end();   // 釋放 ES8311（沒開就不動作，避免 I2S 錯誤）
  M5.Mic.begin();
  s_len = 0;
  s_recording = true;
  s_t0 = millis();
  // 一次連續錄滿整個緩衝（DMA 背景填，不分塊 → 無空檔、音質連續）
  M5.Mic.record(s_buf, CAP_SAMPLES, SAMPLE_RATE);
  Serial.println("[audio] 開始錄音");
}

void feedRecording() {
  // 連續錄音模式下不需每圈動作（保留介面）
}

void stopRecordingAndPlay() {
  if (!s_recording || !s_ready) return;
  s_recording = false;

  // 用按住時間推算實際錄到的取樣數（連續錄音，時間 × 取樣率）
  uint32_t held_ms = millis() - s_t0;
  s_len = (size_t)((uint64_t)held_ms * SAMPLE_RATE / 1000);
  if (s_len > CAP_SAMPLES) s_len = CAP_SAMPLES;

  M5.Mic.end();
  M5.Speaker.begin();
  M5.Speaker.setVolume(SPK_VOLUME);
  Serial.printf("[audio] 錄到約 %u 取樣（%lu ms），開始播放\n", (unsigned)s_len, (unsigned long)held_ms);
  if (s_len > 0) M5.Speaker.playRaw(s_buf, s_len, SAMPLE_RATE);
}

bool isPlaying() { return M5.Speaker.isPlaying(); }
uint32_t recordedSamples() { return (uint32_t)s_len; }

}  // namespace audio_io
