// IMA ADPCM 編碼（B3b-3：中控 → 裝置的下載壓縮）
//
// 16kHz PCM16 → 每個取樣 4 bit（資料量 1/4，8KB/s）。
// 幀格式（每幀 100ms＝1600 取樣，可獨立解壓；裝置端 firmware/src/adpcm.cpp 對應解碼）：
//   [0..1] int16 LE  predictor：編這一幀之前的預測值
//   [2]    uint8     step index：編這一幀之前的步階索引（0..88）
//   [3]    uint8     保留（0）
//   [4..]  每 byte 兩個取樣，先低 4 bit 再高 4 bit；取樣數為奇數時最後補 0
// 編碼狀態跨幀延續，幀頭只是狀態快照 → 中間掉一幀也不會讓後面全部解錯。

const STEP = [
  7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
  50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
  253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
  1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
  3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
  11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
];
const INDEX = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8];

export const ADPCM_FRAME_SAMPLES = 1600;   // 100ms @16kHz

// 把多個 PCM16 LE 的 ArrayBuffer 接成一個 Int16Array
export function concatPcm16(chunks) {
  let bytes = 0;
  for (const c of chunks) bytes += c.byteLength;
  const out = new Int16Array(bytes >> 1);
  let k = 0;
  for (const c of chunks) {
    const dv = new DataView(c);
    for (let i = 0; i + 1 < c.byteLength; i += 2) out[k++] = dv.getInt16(i, true);
  }
  return out;
}

// 編碼整段 PCM，回傳 ArrayBuffer 陣列（每個是一幀）
export function encodeAdpcmFrames(pcm) {
  const frames = [];
  let pred = 0, idx = 0;
  for (let s0 = 0; s0 < pcm.length; s0 += ADPCM_FRAME_SAMPLES) {
    const n = Math.min(ADPCM_FRAME_SAMPLES, pcm.length - s0);
    const buf = new ArrayBuffer(4 + ((n + 1) >> 1));
    const dv = new DataView(buf);
    dv.setInt16(0, pred, true);
    dv.setUint8(2, idx);
    dv.setUint8(3, 0);
    const u8 = new Uint8Array(buf);
    for (let i = 0; i < n; i++) {
      let diff = pcm[s0 + i] - pred;
      let code = 0;
      if (diff < 0) { code = 8; diff = -diff; }
      let step = STEP[idx];
      let vpdiff = step >> 3;
      if (diff >= step) { code |= 4; diff -= step; vpdiff += step; }
      step >>= 1;
      if (diff >= step) { code |= 2; diff -= step; vpdiff += step; }
      step >>= 1;
      if (diff >= step) { code |= 1; vpdiff += step; }
      pred += (code & 8) ? -vpdiff : vpdiff;
      if (pred > 32767) pred = 32767; else if (pred < -32768) pred = -32768;
      idx += INDEX[code];
      if (idx < 0) idx = 0; else if (idx > 88) idx = 88;
      const b = 4 + (i >> 1);
      if (i & 1) u8[b] |= code << 4; else u8[b] = code;
    }
    frames.push(buf);
  }
  return frames;
}

// 解碼（中控自測用；裝置端用 C 版本，演算法相同）
export function decodeAdpcmFrame(buf) {
  const dv = new DataView(buf);
  let pred = dv.getInt16(0, true), idx = dv.getUint8(2);
  const u8 = new Uint8Array(buf);
  const n = (buf.byteLength - 4) * 2;
  const out = new Int16Array(n);
  for (let i = 0; i < n; i++) {
    const code = (u8[4 + (i >> 1)] >> ((i & 1) ? 4 : 0)) & 15;
    const step = STEP[idx];
    let vpdiff = step >> 3;
    if (code & 4) vpdiff += step;
    if (code & 2) vpdiff += step >> 1;
    if (code & 1) vpdiff += step >> 2;
    pred += (code & 8) ? -vpdiff : vpdiff;
    if (pred > 32767) pred = 32767; else if (pred < -32768) pred = -32768;
    idx += INDEX[code];
    if (idx < 0) idx = 0; else if (idx > 88) idx = 88;
    out[i] = pred;
  }
  return out;
}

// ── B3b-3 追加：G.711 μ-law（16kHz，每取樣 8 bit，資料量 1/2）──
// 無狀態、逐取樣編碼，所以任意切段都能獨立解。裝置端解碼見 firmware/src/adpcm.cpp。
export function encodeMulaw(pcm) {
  const out = new Uint8Array(pcm.length);
  for (let i = 0; i < pcm.length; i++) {
    let s = pcm[i];
    const sign = s < 0 ? 0x80 : 0;
    if (sign) s = -s;
    if (s > 32635) s = 32635;
    s += 0x84;
    let exp = 7;
    for (let mask = 0x4000; (s & mask) === 0 && exp > 0; exp--, mask >>= 1) {}
    const mant = (s >> (exp + 3)) & 0x0f;
    out[i] = ~(sign | (exp << 4) | mant) & 0xff;
  }
  return out;
}

export function decodeMulaw(u8) {
  const out = new Int16Array(u8.length);
  for (let i = 0; i < u8.length; i++) {
    const u = ~u8[i] & 0xff;
    let t = ((u & 0x0f) << 3) + 0x84;
    t <<= (u & 0x70) >> 4;
    out[i] = (u & 0x80) ? (0x84 - t) : (t - 0x84);
  }
  return out;
}

// ── B3b-3 追加：把多段資料合併成「每則不超過 maxBytes」的 WebSocket 訊息 ──
// v15 實測每則訊息有約 67ms 的固定成本（804 bytes 的 ADPCM 幀下載只剩 12KB/s），
// 合併成約 8KB 一則以減少則數。ADPCM 幀固定 804 bytes（最後一幀可能較短），
// 裝置依固定大小拆回；μ-law／PCM 無幀頭，直接切段（PCM 切在偶數位置）。
export function packMessages(parts, maxBytes) {
  const msgs = [];
  let cur = [], curBytes = 0;
  const flush = () => {
    if (!curBytes) return;
    const m = new Uint8Array(curBytes);
    let o = 0;
    for (const p of cur) { m.set(new Uint8Array(p), o); o += p.byteLength; }
    msgs.push(m.buffer);
    cur = []; curBytes = 0;
  };
  for (const p of parts) {
    if (curBytes + p.byteLength > maxBytes) flush();
    cur.push(p); curBytes += p.byteLength;
  }
  flush();
  return msgs;
}

export function sliceBytes(u8, maxBytes) {
  const msgs = [];
  for (let o = 0; o < u8.length; o += maxBytes) msgs.push(u8.slice(o, o + maxBytes).buffer);
  return msgs;
}
