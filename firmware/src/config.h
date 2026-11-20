// ─────────────────────────────────────────────────────────────────────────
//  全韌體共用的具名常數：腳位、I2C 位址、按鍵門檻、UI 時序。
//  腳位來源見 CLAUDE.md「已驗證腳位」與按鍵表，不要在其他檔案寫魔術數字。
// ─────────────────────────────────────────────────────────────────────────
#pragma once
#include <stdint.h>

namespace cfg {

// ── 按鍵（2026-11-12 實機確認；按下 = LOW，M5Unified 已設為 input）──
constexpr uint8_t PIN_BTN_A = 11;   // 正面藍色大鍵：模式內主動作／模式 2 急停
constexpr uint8_t PIN_BTN_B = 12;   // 側鍵：系統鍵（切模式／重印／急停備援）

// ── Grove I2C 配件位址（鐵則一：兩者二選一，可能都沒插）──
constexpr uint8_t I2C_ADDR_CARDKB = 0x5F;
constexpr uint8_t I2C_ADDR_SCROLL = 0x40;
constexpr uint32_t GROVE_POWER_SETTLE_MS = 300;   // 開 EXT 5V＋L3B 後等配件 MCU 開機（11-19：200→300）

// ── 側鍵 B 的時間門檻（按鍵決議 2026-11-13）──
constexpr uint32_t BTN_B_REPORT_MS = 1000;   // 按住 1～3 秒後放開 = 重印狀態報告
constexpr uint32_t BTN_B_KILL_MS   = 3000;   // 按住滿 3 秒（不等放開）= 全域急停備援

// ── 急停 task ──
constexpr uint32_t KILL_TASK_STACK = 3072;
constexpr int      KILL_TASK_CORE  = 0;      // 與 Arduino loop（core 1）分開，避免被主迴圈拖住

// ── 網路 task（B3b-5）：WebSocket 收送、重連都在這裡，不卡主迴圈 ──
// core 0：主迴圈在 core 1，兩邊互不卡；Wi-Fi／TCP 底層本來就在 core 0。
// 優先權 1：比急停 task（最高）與 M5Unified 的 Mic／Speaker task（2）低，錄放音與急停永遠先跑。
// 堆疊 10KB：TLS 交握在這裡跑（原本在 8KB 的 loopTask 跑得動）；[perf] 會印剩餘量。
constexpr uint32_t NET_TASK_STACK = 10240;
constexpr int      NET_TASK_CORE  = 0;
constexpr uint32_t NET_TASK_PRIO  = 1;

// ── 語音誤觸防護（B3b-5 補，11-19 定案）──
// 按住不到這麼久就放開 → 當誤觸：不上傳、不送 start/stop，直接回 Idle。
// 做法（Miles 選 A）：一按就開始錄（存在裝置），按滿這個時間才送 start、開始上傳。
constexpr uint32_t VOICE_MIN_PRESS_MS = 600;
// 錄音中連續這麼久沒有人聲 → 自動當作放開 A（口袋裡壓住）。不是錄音上限：一直講話不會被切。
constexpr uint32_t VOICE_AUTO_END_SILENCE_MS = 8000;
// 人聲判斷（VAD）：每 100ms 一塊算「一階差分 RMS」（與錄音量測行的「高頻底噪」同一個指標）。
// 校準（v20b 實機逐塊數據，11-19）：安靜時每塊 −58.8～−60.5 dBFS；講話時每塊 −39.8～−55（輕聲、句尾約 −49～−55）。
// 第一版用 −48（拿錄音量測行的「高頻底噪」+9.5dB 推的——那是整段 2ms 格的中位數，不是每 100ms 一塊的安靜值）→
// 停頓後輕聲接著講（每塊最高只到 −48.9～−53.4）被當成沒人聲，講話中被自動結束。
// 改 −55：高於安靜最高值 −58.8 約 4dB、低於實測輕聲講話。環境較吵（安靜也超過 −55）時只是不會自動結束，不會誤切。
constexpr float VOICE_VAD_DBFS = -55.0f;

// ── A3 桌寵互動（IMU）── 門檻全部來自 11-20 v22-a3m 實測（每 100ms 一窗的最大值；主迴圈約 21ms 一圈＝每窗約 5 筆）
// 實測（每窗最大值）：桌上靜置 偏離≤0.015g 衝擊≤0.017g 轉速≤1.1°/s｜手持不動 偏離≤0.057 衝擊≤0.073 轉速≤24
// 拿起來 偏離≤0.29 衝擊≤0.40 轉速≤389｜輕搖 偏離≤0.54 衝擊≤0.52｜用力搖 偏離 0.5～7 衝擊 0.5～5.8 轉速 190～866
// 拍機身 衝擊 0.86～7.0（拍完下一窗 0.34～0.40）｜拍旁邊桌面 偏離 0.013（感測不到）
constexpr uint32_t PET_WINDOW_MS      = 100;    // 判斷窗
constexpr float    PET_ACTIVE_DPS     = 5.0f;   // 轉速超過＝有人在動它（桌上 ≤1.1；手持常 2～24 → 拿在手上不會睡）
constexpr float    PET_ACTIVE_JERK_G  = 0.03f;  // 衝擊超過＝有人在動它（桌上 ≤0.017）
constexpr float    PET_SHAKE_DEV_G    = 0.5f;   // 搖晃：偏離 1g ≥ 這個值的窗…
constexpr uint8_t  PET_SHAKE_WINDOWS  = 5;      // …最近 10 窗裡有幾窗（用力搖 44 次觸發；輕搖、拿起、快速翻轉 0 次）
constexpr float    PET_TAP_JERK_G     = 0.7f;   // 拍打：這一窗衝擊 ≥ 0.7（輕搖最高 0.52）…
constexpr float    PET_TAP_QUIET_G    = 0.2f;   // …前 3 窗都 < 0.2（拍之前是靜的）…
constexpr float    PET_TAP_AFTER_G    = 0.5f;   // …下一窗 < 0.5（拍完馬上停；拿起晃動的機身不算）
constexpr uint32_t PET_SLEEPY_MS      = 6000;   // 沒互動多久想睡（介面規格第 2 節）
constexpr uint32_t PET_ASLEEP_MS      = 11000;  // 沒互動多久睡著
constexpr uint32_t PET_REACT_MS       = 2500;   // 驚嚇／開心／生氣持續多久回待機
// 睡著時聽聲音：待機收音每 50ms 一塊、一階差分 RMS（dBFS）。實測：安靜（Wi-Fi 省電開）最高 −49.3、
// 打 Mac 鍵盤 −43.9／−44.3、小聲講話 ≤ −49.4｜拍手 −26.5～−40.3、正常講話 −34.4～−41.3
constexpr float    PET_SOUND_DBFS     = -41.0f;
// 傾斜看方向：平均加速度（低通後）÷ 這個值＝眼睛偏移比例；最大偏移像素
constexpr float    PET_GAZE_FULL_G    = 0.6f;
constexpr int      PET_GAZE_MAX_X_PX  = 7;
constexpr int      PET_GAZE_MAX_Y_PX  = 5;
constexpr float    PET_GAZE_SIGN_X    = -1.0f;  // 實機驗收若方向相反就改正負號
constexpr float    PET_GAZE_SIGN_Y    = 1.0f;

// ── A3 休息臉顏色跟電量（Miles 11-20 選方案一）──
// 待機／想睡／睡著的眼睛顏色：≥60% 藍、40–59 綠、20–39 黃、10–19 紅、<10 紅灰閃爍（語音中不閃、充電中不閃）
// 實測電量讀數充電中會在 17～26% 間跳（v22-a3m）→ 每 5 秒讀一次、指數平滑、升級要多 3% 才升
constexpr uint32_t BAT_READ_MS        = 5000;
constexpr float    BAT_SMOOTH         = 0.2f;   // 每次讀數的權重（約 25 秒的平滑）
constexpr int      BAT_HYST_PCT       = 3;
constexpr uint32_t BAT_CHARGE_HOLD_MS = 30000;  // 讀數「充電中／未充電」會來回跳 → 30 秒內見過充電就當充電中
constexpr uint32_t BAT_BLINK_MS       = 500;    // 紅灰閃爍半週期

// ── UI ──
constexpr uint32_t UI_REFRESH_MS = 500;      // 狀態沒變時的定期重畫間隔
constexpr uint32_t LOOP_YIELD_MS = 1;        // loop 結尾讓出 CPU 的時間（非阻塞）
// B3b-4：中控斷線超過這麼久才顯示斷線提示（重連常在 1～2 秒內完成，避免圖示一閃一閃）
constexpr uint32_t OFFLINE_SHOW_MS = 2000;

}  // namespace cfg
