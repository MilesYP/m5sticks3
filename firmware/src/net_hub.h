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

}  // namespace net_hub
