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
}
