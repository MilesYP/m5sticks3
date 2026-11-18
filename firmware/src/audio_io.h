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
// B3b-2：「停止錄音」與「播放」拆開，中間可以先上傳中控、等回傳
size_t stopRecording(int16_t** out);  // 停止錄音、切頭尾、修突波、印量測行；回傳取樣數（0＝太短），
                                      // *out 指向處理後（尚未正規化）的音訊，下次 startRecording 前有效
bool playBuffer(int16_t* buf, size_t samples);  // 正規化＋淡入淡出後播出（會改寫 buf）
int16_t* rxBuffer();                  // 收中控回傳音訊用的 PSRAM 緩衝
size_t rxCapacityBytes();
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

// ── B3b-4 邊收邊播 ──
// 收到的音訊（已解成 16kHz PCM16）寫進 PSRAM 環形緩衝；緩衝到 prebuffer 就開喇叭開始播。
// 由喇叭播放 task 的「緩衝釋放回呼」自己從環形緩衝拿下一塊餵進去（不靠主迴圈，
// 實測主迴圈收一則訊息會被卡最久 2.5 秒）。斷音後要補到 1 秒才繼續，避免一直斷斷續續。
bool streamBegin();                                  // 開一輪（清空緩衝；還不開喇叭）
size_t streamWrite(const int16_t* p, size_t n);      // 主迴圈寫入；回傳寫入幾個（緩衝滿會丟掉多的並計數）
void streamEnd();                                    // 不會再有資料：剩下的播完就結束
void streamAbort();                                  // 立刻停（新一輪、離開 Desktop）
bool streamActive();                                 // 這一輪還沒播完（含等待緩衝）
bool streamStarted();                                // 已經開始出聲
uint32_t streamStartedAt();                          // 開始出聲的 millis()
void setPrebufferMs(uint32_t ms);                    // 序列埠 b<ms>
const char* streamReport();                          // 一行統計（播完時印）

}  // namespace audio_io
