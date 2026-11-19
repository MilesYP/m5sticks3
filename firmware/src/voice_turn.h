// ─────────────────────────────────────────────────────────────────────────
//  voice_turn：Desktop 模式的一輪語音（M1-B / B3b-2～B3b-5）
//
//  按下 A → Recording：開始錄音、交給網路 task 邊錄邊上傳（μ-law，B3b-5）
//  放開 A → Flushing：定好結尾，網路 task 送完剩下的就送 stop
//  → Receiving：中控一則一則回傳，邊收邊解壓邊播 → 播完回 Idle、開回省電
//
//  斷線（B3b-5，Miles 定案甲）：錄音／送尾巴時斷線，連回後網路 task 用同一個 id 從頭重送；
//  放開後 15 秒都連不回中控才放棄這輪。回答播到一半斷線 → 已收到的播完再結束。
//  錄音不設上限。表情本機決定：Listening → Thinking → Speaking → Idle。
//  B3c：模式 gpt（預設）接 GPT-Live，echo 保留原樣回傳；中控回 error quota → 印「今日額度已用完」回 Idle。
//  B4：逐字稿（text）顯示在畫面下方對話區；Speaking 時嘴巴跟著播放音量開合。
// ─────────────────────────────────────────────────────────────────────────
#pragma once

namespace voice_turn {
void begin();        // 掛上 net_hub 的回呼（音訊、audio_end、format、收件反壓）
void onPress();      // Desktop 按下 A
void onRelease();    // Desktop 放開 A
void loop();         // 每圈呼叫：印上傳進度、逾時、播完收尾；離開 Desktop 時中止
bool busy();         // 這一輪還沒結束
void setDownFormat(int f);      // 下載格式 0=pcm16、1=adpcm、2=mulaw、3=adpcm8k（預設），序列埠 f0～f3
void setUpFormat(int f);        // B3b-5：上傳格式 0=pcm16、1=mulaw（預設），序列埠 u0/u1
void setPaceRealtime(bool rt);  // 中控送法 false=全速（預設）、true=照真實速度，序列埠 r0/r1
void setGptMode(bool gpt);      // B3c：true＝GPT-Live 對話（預設）、false＝echo 原樣回傳（回歸測試），序列埠 m1/m0
bool gptMode();
}
