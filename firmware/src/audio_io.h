// ─────────────────────────────────────────────────────────────────────────
//  audio_io：麥克風錄音 / 喇叭放音（M1-B / B3b-1 回放測試）
//
//  ES8311 同一時間只能麥克風或喇叭其一（對講機式剛好規避）：
//  待機與錄音時停在麥克風，只有放音時才切到喇叭，播完立刻切回。
//  （codec 上電後約一秒內擷取值可能全為零，所以開機就讓它先暖機。）
//  緩衝放 PSRAM。16kHz、單聲道、PCM16。
// ─────────────────────────────────────────────────────────────────────────
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace audio_io {

void begin();                  // 配置 PSRAM 緩衝、讓 ES8311 上電暖機（開機呼叫一次）
void loop();                   // 每圈呼叫：預排錄音區塊、放音結束後切回麥克風
void startRecording();         // 開始錄（麥克風已開著，只是重設緩衝與計數）
void stopRecordingAndPlay();   // 停止錄音、正規化音量、切到喇叭播出來
bool isPlaying();              // 喇叭是否還在放
uint32_t recordedSamples();    // 這次錄到幾個取樣（除錯用）

// ── 診斷用（B3b-1 判斷音質問題的來源）──
float lastPeak();              // 上次錄音的原始峰值，0～1（太小＝麥克風收不到）
float headPeak();              // 開頭 0.5 秒的峰值，0～1（遠小於 lastPeak ＝ codec 暖機吃掉開頭）
// ── B3.6 Wi-Fi 干擾量測 ──
void setPga(uint8_t reg14);    // ES8311 0x14 類比 PGA（0x10=0dB … 0x17=+21dB，每階 3dB），立即生效
uint8_t pga();
typedef const char* (*LabelFn)();
void setDiagLabel(LabelFn fn); // 每次錄音那一行附上的設定標籤（例：Wi-Fi 省電／發射功率）

}  // namespace audio_io
