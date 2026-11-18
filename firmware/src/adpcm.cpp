#include "adpcm.h"

namespace {
const int16_t STEP[89] = {
  7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
  50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
  253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
  1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
  3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
  11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};
const int8_t INDEX[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
}  // namespace

namespace adpcm {

size_t decodeFrame(const uint8_t* f, size_t len, int16_t* out, size_t max_samples) {
  if (len <= HEADER_BYTES || f == nullptr || out == nullptr) return 0;
  int32_t pred = (int16_t)(f[0] | (f[1] << 8));
  int32_t idx = f[2];
  if (idx > 88) return 0;   // 幀頭不合理：不是 ADPCM 幀
  size_t n = (len - HEADER_BYTES) * 2;
  if (n > max_samples) n = max_samples;
  for (size_t i = 0; i < n; ++i) {
    const uint8_t code = (f[HEADER_BYTES + (i >> 1)] >> ((i & 1) ? 4 : 0)) & 0x0F;
    const int32_t step = STEP[idx];
    int32_t vpdiff = step >> 3;
    if (code & 4) vpdiff += step;
    if (code & 2) vpdiff += step >> 1;
    if (code & 1) vpdiff += step >> 2;
    pred += (code & 8) ? -vpdiff : vpdiff;
    if (pred > 32767) pred = 32767; else if (pred < -32768) pred = -32768;
    idx += INDEX[code];
    if (idx < 0) idx = 0; else if (idx > 88) idx = 88;
    out[i] = (int16_t)pred;
  }
  return n;
}

int16_t mulawToPcm(uint8_t u) {
  u = ~u;
  int32_t t = ((u & 0x0F) << 3) + 0x84;
  t <<= (u & 0x70) >> 4;
  return (int16_t)((u & 0x80) ? (0x84 - t) : (t - 0x84));
}

}  // namespace adpcm
