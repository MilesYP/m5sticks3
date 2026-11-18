// ─────────────────────────────────────────────────────────────────────────
//  net_hub：連 Wi-Fi、連雲端中控的 WebSocket（M1-B / B2）
//
//  B2a：連線＋定時 ping，狀態印到序列埠。
//  B2b：長按 A → sendStart()/sendStop()，收中控的 state → onState() 切表情。
//  機密（Wi-Fi 帳密、中控位址）放 src/secrets.h（不進 repo）。
// ─────────────────────────────────────────────────────────────────────────
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace net_hub {

typedef void (*StateCb)(const char* state);   // 收到中控 state 時呼叫（傳 "listening" 等）

void begin();
void loop();            // 每圈呼叫，驅動 WebSocket；非阻塞
bool wifiUp();
bool linkUp();          // 中控 WebSocket 是否連上
void sendStart();
void sendStop();
void onState(StateCb cb);

// ── B3b-2 音訊傳輸 ──
typedef void (*BinCb)(const uint8_t* data, size_t len);   // 收到二進位幀（中控回傳的音訊）
typedef void (*AudioEndCb)(uint32_t bytes, uint32_t pcm_bytes, float gain, const char* fmt);  // {"t":"audio_end","n":...,"pcm":...,"g":...}（B3b-4 加 g＝中控正規化增益）
typedef void (*FormatCb)(const char* up, const char* down);   // B3b-3：{"t":"format","up":...,"down":...}
bool sendBinary(const uint8_t* data, size_t len);        // 送一個二進位幀；未連線或失敗回 false
void onBinary(BinCb cb);
void onAudioEnd(AudioEndCb cb);
void onFormat(FormatCb cb);
void sendStartFmt(const char* up, const char* down, const char* pace = nullptr);   // B3b-3：{"t":"start","up":..,"down":..}；B3b-4 加 pace（fast／rt）

// ── B3b-2 量測：網路處理花多久（每個階段開始時 reset，結束時印）──
void perfReset();
const char* perfLine();   // 例："ws.loop 平均 0.4／最長 3120 ms（812 次）｜送幀 …｜收幀 …"

// ── B3.6 Wi-Fi 干擾量測（序列埠指令切換，見 diag_cmd）──
void setWifiEnabled(bool on);        // 整個 Wi-Fi 關／開（量測基準；開回來會自動重連中控）
void setPowerSave(bool on);          // 手動：Wi-Fi 省電（modem sleep）開／關；之後自動切換暫停，直到 setPowerSaveAuto()
void setPowerSaveAuto();            // 恢復自動切換（並先回到省電開）
// B3.6 定案：省電模式開著時錄音有滋滋聲 → 錄放音期間關省電，結束再開（方案 B）
void voiceActive(bool active);       // true＝開始錄音（關省電）；false＝播完／離開（開回省電）
void setTxPowerDbm(int dbm);         // 發射功率，取最接近的檔位：20/15/11/8/5/2
void pingNow();                      // 立刻送一次 ping（看發射封包是否對應雜訊）
const char* configLabel();           // 例："wifi=WS ps=on tx=19.5dBm"（讀回實際值，不含機密）

// ── B3b-4 斷線量測與畫面提示 ──
uint32_t linkDownMs();               // 中控斷線多久了（ms）；連著時回 0。開機尚未連上也算斷線（從開機起算）
void setPingInterval(uint32_t sec);  // 序列埠 i<秒>：ping 間隔（0＝不 ping），量測連線重置用

}  // namespace net_hub
