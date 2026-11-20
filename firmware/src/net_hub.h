// ─────────────────────────────────────────────────────────────────────────
//  net_hub：連 Wi-Fi、連雲端中控的 WebSocket（M1-B）
//
//  B3b-5 起：所有 WebSocket 操作（收、送、重連）都在「網路 task」（core 0）執行，
//  主迴圈不再被 ws.loop()／sendBIN／TLS 交握卡住。跨 task 的交接：
//    主迴圈 → 網路 task：指令佇列（uploadBegin／uploadFinish／ping／斷線…）
//    網路 task → 主迴圈：收件佇列（PSRAM 1MB，文字與音訊同一條、保持順序），
//                        由 loop() 依序取出並呼叫 onBinary／onAudioEnd／onFormat／onState 回呼
//    網路 task 的 log：先進 log 佇列，由 loop() 代印（序列埠只有主迴圈在寫）
//  上傳音訊由網路 task 直接從 audio_io 的錄音環形緩衝讀、編 μ-law、每則 8000 bytes 送出。
//  機密（Wi-Fi 帳密、中控位址、token）放 src/secrets.h（不進 repo）。
// ─────────────────────────────────────────────────────────────────────────
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace net_hub {

typedef void (*StateCb)(const char* state);   // 收到中控 state 時呼叫（傳 "listening" 等）

void begin();           // 開機呼叫一次：連 Wi-Fi、建網路 task
void loop();            // 主迴圈每圈呼叫：代印 log、依序處理收到的訊息（回呼都在主迴圈執行）
bool wifiUp();
bool linkUp();          // 中控 WebSocket 是否連上
void onState(StateCb cb);

// ── 收到的訊息（回呼在主迴圈執行）──
typedef void (*BinCb)(const uint8_t* data, size_t len);   // 中控回傳的音訊
typedef void (*AudioEndCb)(uint32_t bytes, uint32_t pcm_bytes, float gain, const char* fmt);
typedef void (*FormatCb)(const char* up, const char* down);
// B3c：中控回報錯誤（code＝"quota" 今日額度用完、"openai" 連不上 OpenAI…；s＝說明）
typedef void (*ErrorCb)(const char* code, const char* s);
// B4：逐字稿片段（role＝"user"／"assistant"，s＝新增的文字，UTF-8，已解開 JSON 跳脫）
// ms（B4 實機補）：Friday 的字對應到這輪回覆音訊的第幾 ms（播到那裡才顯示）；沒有＝0
typedef void (*TextCb)(const char* role, const char* s, uint32_t ms);
typedef bool (*BinGate)(size_t len);   // B3b-5 反壓：現在收不收得下這則音訊；false → 留在佇列，下一圈再試
void onBinary(BinCb cb);
void onAudioEnd(AudioEndCb cb);
void onFormat(FormatCb cb);
void onError(ErrorCb cb);
void onText(TextCb cb);
void onBinGate(BinGate g);

// ── B3b-5 上傳工作（網路 task 執行）──
// uploadBegin：先呼叫 audio_io::startRecording() 再呼叫。中控沒連上也可以，連上後才送。
// 斷線重連後網路 task 會用同一個 id 重送 start，再從頭重送整段（Miles 定案「甲」）。
// mode（B3c）："gpt"（接 GPT-Live）或 "echo"（B3b-5 原樣回傳，回歸測試用）
void uploadBegin(uint32_t id, const char* up, const char* down, const char* pace, const char* mode);
void uploadFinish();            // 放開 A（audio_io::stopRecording() 之後）：送完剩下的就送 stop
void uploadAbort();             // 這輪不要了（不送 stop）
bool uploadDone();              // stop 已送出
uint32_t uploadSentSamples();   // 這輪已送出幾個取樣（從切頭後算起；重送時歸零重算）
uint32_t uploadResends();       // 開機以來因斷線從頭重送的次數
uint32_t uploadLostSamples();   // 開機以來沒送就被錄音覆蓋掉的取樣數（網路停擺超過約 128 秒才會發生）
uint32_t txBytesTotal();        // 開機以來送出的音訊 bytes（算上傳速度用）

// ── 量測（每個階段開始 reset，結束時印）──
void perfReset();
const char* perfLine();

// ── B3.6 Wi-Fi 干擾量測（序列埠指令，見 diag_cmd）──
void setWifiEnabled(bool on);        // 整個 Wi-Fi 關／開
void setPowerSave(bool on);          // 手動：Wi-Fi 省電開／關；之後自動切換暫停，直到 setPowerSaveAuto()
void setPowerSaveAuto();
void voiceActive(bool active);       // true＝錄放音中（關省電，避免滋滋聲）；false＝結束（開回省電）
void setTxPowerDbm(int dbm);
void pingNow();
void dropLink();                     // B3b-5：序列埠 x，只斷 WebSocket（模擬連線重置，測重送）
const char* configLabel();

// ── B3b-4 斷線量測與畫面提示 ──
uint32_t linkDownMs();               // 中控斷線多久了（ms）；連著時回 0
void setPingInterval(uint32_t sec);  // 序列埠 i<秒>：ping 間隔（0＝不 ping）

}  // namespace net_hub
