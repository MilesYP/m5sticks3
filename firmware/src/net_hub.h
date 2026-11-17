// ─────────────────────────────────────────────────────────────────────────
//  net_hub：連 Wi-Fi、連雲端中控的 WebSocket（M1-B / B2）
//
//  B2a：連線＋定時 ping，狀態印到序列埠。
//  B2b：長按 A → sendStart()/sendStop()，收中控的 state → onState() 切表情。
//  機密（Wi-Fi 帳密、中控位址）放 src/secrets.h（不進 repo）。
// ─────────────────────────────────────────────────────────────────────────
#pragma once
#include <stddef.h>

namespace net_hub {

typedef void (*StateCb)(const char* state);   // 收到中控 state 時呼叫（傳 "listening" 等）

void begin();
void loop();            // 每圈呼叫，驅動 WebSocket；非阻塞
bool wifiUp();
bool linkUp();          // 中控 WebSocket 是否連上
void sendStart();
void sendStop();
void onState(StateCb cb);

// ── B3.6 Wi-Fi 干擾量測（序列埠指令切換，見 diag_cmd）──
void setWifiEnabled(bool on);        // 整個 Wi-Fi 關／開（量測基準；開回來會自動重連中控）
void setPowerSave(bool on);          // 手動：Wi-Fi 省電（modem sleep）開／關；之後自動切換暫停，直到 setPowerSaveAuto()
void setPowerSaveAuto();            // 恢復自動切換（並先回到省電開）
// B3.6 定案：省電模式開著時錄音有滋滋聲 → 錄放音期間關省電，結束再開（方案 B）
void voiceActive(bool active);       // true＝開始錄音（關省電）；false＝播完／離開（開回省電）
void setTxPowerDbm(int dbm);         // 發射功率，取最接近的檔位：20/15/11/8/5/2
void pingNow();                      // 立刻送一次 ping（看發射封包是否對應雜訊）
const char* configLabel();           // 例："wifi=WS ps=on tx=19.5dBm"（讀回實際值，不含機密）

}  // namespace net_hub
