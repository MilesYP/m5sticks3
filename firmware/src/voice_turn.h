// ─────────────────────────────────────────────────────────────────────────
//  voice_turn：Desktop 模式的一輪語音（M1-B / B3b-2）
//
//  按下 A → 錄音（關 Wi-Fi 省電、送 start）
//  放開 A → 切成 100ms 一塊上傳中控（每圈最多 2 塊，不卡主迴圈）→ 送 stop
//  中控原樣回傳 → 收到 audio_end → 播放 → 播完回 Idle、開回省電
//  每輪印「上傳／收回 bytes 與 CRC32、來回時間」，兩邊一致＝傳輸無誤。
//
//  表情由本機決定（Miles 選定）：Listening → Thinking → Speaking → Idle；
//  中控回的 state 只印在序列埠。
//  中控沒連上（或中途斷線、上傳失敗）→ 退回本機回放（B3b-1 行為）。
//  B3b-3：start 帶 up=pcm16、down=mulaw（預設）；中控回 format 確認後，回傳幀用 ADPCM 解壓，
//        驗收印 SNR（解壓結果對原始錄音）。
// ─────────────────────────────────────────────────────────────────────────
#pragma once

namespace voice_turn {
void begin();        // 掛上 net_hub 的二進位／audio_end 回呼
void onPress();      // Desktop 按下 A
void onRelease();    // Desktop 放開 A
void loop();         // 每圈呼叫：上傳、逾時、播完收尾；離開 Desktop 時中止
bool busy();         // 這一輪還沒結束
void setDownFormat(int f);    // B3b-3：下載格式 0=pcm16、1=adpcm、2=mulaw（預設），序列埠 f0/f1/f2
}
