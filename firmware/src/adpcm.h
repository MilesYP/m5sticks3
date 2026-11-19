// ─────────────────────────────────────────────────────────────────────────
//  adpcm：IMA ADPCM 解碼（M1-B / B3b-3 下載壓縮）
//
//  中控（cloud/src/adpcm.js）把 16kHz PCM16 壓成每取樣 4 bit，資料量 1/4。
//  幀格式：[int16 LE predictor][uint8 step index][uint8 保留][每 byte 兩取樣，先低 4 bit]
//  幀頭是編這一幀之前的狀態快照 → 每幀可獨立解壓。
// ─────────────────────────────────────────────────────────────────────────
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace adpcm {
constexpr size_t HEADER_BYTES = 4;
constexpr size_t FRAME_SAMPLES = 1600;                          // 100ms @16kHz
constexpr size_t FRAME_BYTES = HEADER_BYTES + FRAME_SAMPLES / 2;  // 804；中控把多幀合成一則訊息，依此拆回
// 解一幀，寫到 out（最多 max_samples 個），回傳寫了幾個取樣；幀太短或格式不對回 0
size_t decodeFrame(const uint8_t* frame, size_t len, int16_t* out, size_t max_samples);
// B3b-3 追加：G.711 μ-law 解碼（每 byte 一個取樣，無狀態）
int16_t mulawToPcm(uint8_t u);
// B3b-5 追加：G.711 μ-law 編碼（上傳用；與中控 cloud/src/adpcm.js 的 encodeMulaw／decodeMulaw 同一套）
uint8_t pcmToMulaw(int16_t pcm);

// B3b-4 追加：8kHz → 16kHz 升取樣（ADPCM 8kHz 下載用）。
// 偶數點＝原取樣，奇數點用 8 階半帶濾波器內插（node 實測：只做降升取樣 SNR 約 42dB，
// 線性內插只有 25dB）。要看前後各 4 點 → 輸出比輸入晚 4 個取樣；串流結束呼叫 flush() 補齊，
// 總輸出剛好是輸入的 2 倍、與原音對齊。
class Upsampler2x {
 public:
  void reset();
  size_t push(const int16_t* in, size_t n, int16_t* out);   // out 至少 2n 個；回傳寫了幾個
  size_t flush(int16_t* out);                                // out 至少 8 個；回傳寫了幾個
 private:
  int16_t h_[8] = {0};   // 最近 8 個輸入，h_[7] 最新
  uint32_t count_ = 0;
  size_t step(int16_t s, int16_t* out);
};
}
