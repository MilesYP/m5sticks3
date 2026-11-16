// ─────────────────────────────────────────────────────────────────────────
//  audio_io：麥克風錄音 / 喇叭放音（M1-B / B3b-1 回放測試）
//
//  ES8311 同一時間只能麥克風或喇叭其一（對講機式剛好規避）：
//  錄音前切到麥克風、放音前切到喇叭。B3b-1 先做本機回放，驗證收放音硬體。
//  緩衝放 PSRAM。16kHz、單聲道、PCM16。
// ─────────────────────────────────────────────────────────────────────────
#pragma once
#include <stdint.h>

namespace audio_io {

void begin();                  // 配置 PSRAM 緩衝（開機呼叫一次）
void startRecording();         // 切到麥克風、開始錄
void feedRecording();          // 每圈呼叫：把下一塊錄進緩衝（錄音中才動作）
void stopRecordingAndPlay();   // 停止錄音、切到喇叭、把剛錄的播出來
bool isPlaying();              // 喇叭是否還在放
uint32_t recordedSamples();    // 這次錄到幾個取樣（除錯用）

}  // namespace audio_io
