// ─────────────────────────────────────────────────────────────────────────
//  voice_turn：Desktop 模式的一輪語音（M1-B / B3b-2～B3b-4）
//
//  按下 A → 錄音（關 Wi-Fi 省電、送 start）
//  放開 A → 切成 100ms 一塊上傳中控（每圈最多 2 塊）→ 送 stop
//  中控一則一則回傳 → 邊收邊解壓、寫進 audio_io 的串流緩衝 → 緩衝夠就開始播（B3b-4）
//  → 收到 audio_end 且播完 → 回 Idle、開回省電
//
//  表情由本機決定（Miles 選定）：Listening → Thinking → Speaking（開始出聲時）→ Idle。
//  B3b-4（Miles 選定）：中控沒連上、上傳或等待中斷線 → 不回放、這輪結束（畫面由斷線提示接手）；
//  回答播到一半斷線 → 已收到的播完再結束。
//  下載格式：start 帶 down，預設 adpcm8k（降到 8kHz 再 ADPCM，約 4KB/s）；pace 預設 fast。
// ─────────────────────────────────────────────────────────────────────────
#pragma once

namespace voice_turn {
void begin();        // 掛上 net_hub 的二進位／audio_end／format 回呼
void onPress();      // Desktop 按下 A
void onRelease();    // Desktop 放開 A
void loop();         // 每圈呼叫：上傳、逾時、播完收尾；離開 Desktop 時中止
bool busy();         // 這一輪還沒結束
void setDownFormat(int f);    // 下載格式 0=pcm16、1=adpcm、2=mulaw、3=adpcm8k（預設），序列埠 f0～f3
void setPaceRealtime(bool rt);  // B3b-4：中控送法 false=全速（預設）、true=照真實速度，序列埠 r0/r1
}
