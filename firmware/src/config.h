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
// 拍打（A4 11-20 改，v23-a4 橫拿實測）：手拿著連拍 3 下（間隔約 0.3 秒）舊規則 0 次觸發——
//   拍的衝擊 0.67／1.21／1.03g，但「前 3 窗 < 0.2」被上一下拍打（0.67）與手握餘震（0.15～0.22）擋掉；有一下的力道分在相鄰兩窗（0.48→0.67）。
// 新規則：這一窗衝擊 ≥ 0.6、轉速 < 400°/s、比上一窗大（上升沿）；
//   再往前 2 窗（跳過緊鄰的那窗）< max(0.2, 這一窗×0.6)（相對安靜即可，連拍不互擋）；距上次拍打 ≥ 3 窗；下一窗 < 0.5（拍完馬上停）。
// 重播 11-20 三份 log：橫拿連拍 3 下 → 3 次；先前兩次單拍照樣中；轉向、拿起翻轉（衝擊 0.65、轉速 472）、用力搖、放下都不中。
// 拿起來衝擊 ≤ 0.40（A3）本來就到不了 0.6。
constexpr float    PET_TAP_JERK_G     = 0.6f;
constexpr float    PET_TAP_QUIET_G    = 0.2f;
constexpr float    PET_TAP_QUIET_RATIO = 0.6f;
constexpr float    PET_TAP_MAX_DPS    = 400.0f;
constexpr uint8_t  PET_TAP_GAP_WINDOWS = 3;
constexpr float    PET_TAP_AFTER_G    = 0.5f;   // 下一窗 < 0.5（拍完馬上停；拿起晃動的機身不算）
constexpr uint32_t PET_SLEEPY_MS      = 6000;   // 沒互動多久想睡（介面規格第 2 節）
constexpr uint32_t PET_ASLEEP_MS      = 11000;  // 沒互動多久睡著
constexpr uint32_t PET_REACT_MS       = 2500;   // 驚嚇／開心／生氣持續多久回待機
// 睡著時聽聲音：待機收音每 50ms 一塊、一階差分 RMS（dBFS）。實測：安靜（Wi-Fi 省電開）最高 −49.3、
// 打 Mac 鍵盤 −43.9／−44.3、小聲講話 ≤ −49.4｜拍手 −26.5～−40.3、正常講話 −34.4～−41.3
constexpr float    PET_SOUND_DBFS     = -41.0f;
// 傾斜看方向：平均加速度（低通後）÷ 這個值＝眼睛偏移比例；最大偏移像素
constexpr float    PET_GAZE_FULL_G    = 0.6f;
constexpr int      PET_GAZE_MAX_X_PX  = 7;   // 直式的最大位移（px）
constexpr int      PET_GAZE_MAX_Y_PX  = 5;
// A4（11-20 實機：橫式看方向幅度不明顯）：橫式臉縮成 0.8 倍、位移也跟著縮；橫式左右空間較大 → 左右放大到直式的 1.6 倍（約 11px），
// 上下維持直式的像素量；嘴巴跟著眼睛移一半，整張臉看起來是轉過去而不只是眼珠動
constexpr float    PET_GAZE_LAND_X_GAIN = 1.6f;
// A5-1（11-20 Miles：直轉橫時臉突然變小、不協調）：橫式臉的比例 0.8 → 1.0（跟直式一樣大）。
// 橫式高 135：臉中心 y≈54、眼睛連最外圈光暈約 21～87、地平線 116，放得下。
constexpr float    FACE_LAND_SCALE    = 1.0f;
constexpr float    PET_GAZE_MOUTH_FOLLOW = 0.5f;
constexpr float    PET_GAZE_SIGN_X    = -1.0f;  // 實機驗收若方向相反就改正負號
constexpr float    PET_GAZE_SIGN_Y    = 1.0f;

// ── A4 拿法判斷（直式／橫式 A 左／橫式 A 右）── 11-20 v23-a4m 實測（每 100ms 一窗的平均加速度）
// 「平面」＝重力落在螢幕平面上的大小 √(x²+y²)；「角」＝atan2(x, y)：直拿 0°、A 鍵在右 +90°、A 鍵在左 −90°、倒拿 ±180°。
// 實測：直拿 平面 0.95～1.05、角 −9～+2°｜A 右 平面 0.93～1.01、角 +84～+87°｜A 左 平面 1.00～1.01、角 −86～−88°
//       斜靠桌上 平面 0.33～0.56｜倒拿 角 −171～−176°（維持原版面）
// 規則：平面 ≥ 0.70 才判斷（斜靠最高 0.56）；|角| ≤ 30° 直式、60°～120° 橫式，30°～60° 是死區（維持原版面，45° 附近不會來回翻）；
// 新方向連續 5 窗（0.5 秒）才切；偏離 1g ≥ 0.3 的窗（用力搖）不算。兩份 log 重播：每次轉向只切 1 次、斜靠與倒拿不翻。
constexpr float    ORIENT_PLANE_MIN      = 0.70f;
constexpr float    ORIENT_PORTRAIT_DEG   = 30.0f;
constexpr float    ORIENT_LAND_MIN_DEG   = 60.0f;
constexpr float    ORIENT_LAND_MAX_DEG   = 120.0f;
constexpr uint8_t  ORIENT_DWELL_WINDOWS  = 5;
constexpr float    ORIENT_SHAKE_DEV_G    = 0.30f;
// 畫布轉向（M5Canvas::setRotation）：A 鍵在右＝裝置逆時針轉 90° → 畫面要順時針轉 90°（LGFX rotation 1）。
// 推論值，第 2 輪實機以 o1／o2 確認；若畫面上下顛倒就把兩個值對調。
constexpr uint8_t  ORIENT_ROT_A_RIGHT    = 1;
constexpr uint8_t  ORIENT_ROT_A_LEFT     = 3;

// ── A4 驚嚇彈珠台（Miles 11-20）：搖的當下眼睛、嘴巴像彈珠在螢幕裡撞來撞去，停手後彈簧歸位、再發抖 ──
// 推力來源＝每筆 IMU 的「線性加速度」（讀數扣掉低通後的重力），換算成畫面左右／上下（g）。
// 門檻參考 A3：拿起來每窗偏離 ≤0.29、輕搖 ≤0.54、用力搖 0.5～7 → 線性加速度 ≥0.7g 才開始甩。下列手感參數待實機微調。
constexpr float    PIN_START_G   = 1.20f;   // 開始甩（11-20 實機：0.7 太容易觸發）
constexpr float    PIN_QUIET_G   = 0.35f;   // 低於這個算「沒在搖」
constexpr uint32_t PIN_QUIET_MS  = 400;     // 沒在搖多久 → 彈簧歸位
constexpr float    PIN_GAIN      = 5000.0f; // 1g 的推力（設計單位／秒²）（11-20 實機：900 太弱太慢、撞不到牆）
constexpr float    PIN_SIGN      = 1.0f;    // 11-20 實機：−1 方向相反 → 改 +1
constexpr float    PIN_DRAG      = 0.6f;    // 空氣阻力（每秒速度衰減比例）
constexpr float    PIN_REST      = 0.85f;   // 撞牆反彈保留的速度比例
constexpr float    PIN_RETURN_K  = 140.0f;  // 歸位彈簧（阻尼不足 → 衝過頭再回來）
constexpr float    PIN_RETURN_C  = 9.0f;
constexpr float    PIN_SQUASH    = 0.62f;   // 撞牆瞬間被撞方向壓成幾倍
constexpr float    PIN_GRAVITY_LPF = 0.08f; // 每筆 IMU 的重力低通權重（約 0.25 秒）
constexpr float    PIN_VMAX      = 2200.0f; // 速度上限（設計單位／秒）

// ── A8 甩碎（Miles 11-21）：彈珠台觸發時，眼睛、嘴巴、符號炸成小六角，像骰子／彈珠在臉的框裡滾、互撞；
//    停手（彈珠台結束）後彈簧拉回原位、拼回臉。重力方向用 IMU 即時算（imu_pet 每筆送 setGravity）。
constexpr float    FR_A          = 5.0f;    // 碎片六角邊長（px）；模擬：3.4 太碎像沙，5 像骰子
constexpr int      FR_MAX        = 110;     // 碎片上限（兩眼＋嘴＋符號實際約 40～60 顆）
constexpr float    FR_GRAVITY    = 900.0f;  // 1g 視重力 → px/s²
constexpr float    FR_PUSH       = 2600.0f; // 1g 線性加速度的推力 → px/s²
constexpr float    FR_REST       = 0.55f;   // 撞牆反彈保留的速度比例
constexpr float    FR_DRAG       = 0.6f;    // 空氣阻力（每秒速度衰減比例）
constexpr float    FR_RETURN_K   = 220.0f;  // 拼回彈簧（阻尼不足 → 衝過頭一點點）
constexpr float    FR_RETURN_C   = 16.0f;
constexpr uint32_t FR_RETURN_MAX_MS = 900;  // 拼回最多多久（到了直接換回正常五官）
constexpr uint32_t FR_HOLD_MS    = 2600;    // 停手（彈珠台結束）後碎片先散在畫面上多久才拼回（11-21 Miles：0.4 秒太快→2.5 秒→4 秒；
                                            // 11-22 Miles：「3 秒沒晃才變回表情」→ 停手判定 PIN_QUIET_MS 0.4 秒＋2.6 秒＝3 秒）
constexpr float    FR_SHAKE_GRAV = 0.5f;    // 甩著的時候重力打幾折（11-22 Miles：一直甩要一直有反應——碎片別都沉在底部）
constexpr float    FR_RATTLE     = 1800.0f; // 甩著的時候每 1g 給碎片的亂數踢力（px/s²），像骰子在盒子裡被搖得一直跳
// 視重力左右方向：推導值（靜置時加速度計讀到「往上」1g；右邊低 → 讀數右分量為負 → 碎片往右）。
// 實機往右傾若碎片往左跑，改 −1。
constexpr float    FR_GRAV_SIGN_X = 1.0f;

// ── A4b 充電液體（Miles 11-21）：充電中／充飽時背景六角格裝液體，量跟電量、跟傾斜即時流動 ──
constexpr int      LQ_MAX        = 192;     // 亮格上限（直式容器約 120 格、橫式約 115 格）
constexpr float    LQ_FILL_MIN   = 0.08f;   // 0% 時至少裝幾成（看得出有液體）
constexpr float    LQ_FILL_FULL  = 0.92f;   // 100%（充飽）裝到容器的幾成（11-21 Miles：滿到只剩一點空間才有充滿的感覺；原 0.6）
constexpr uint32_t LQ_STEP_MS    = 60;      // 每 60ms 走一步（只有走步那幀改畫面，部分更新才省）
constexpr uint32_t LQ_STEP_SHAKE_MS = 30;   // 甩著的時候 30ms 一步（11-22 Miles：一直甩液體要一直晃）
constexpr uint32_t LQ_WAVE_MS    = 120;     // 充電亮波每 120ms 更新一次
constexpr uint32_t LQ_FADE_MS    = 600;     // 拔線後淡出
constexpr uint32_t LQ_SETTLE_MS  = 1500;    // 重力方向穩定這麼久就不再側滑（液面靜止、部分更新不用一直重送；11-21 29-a4e 實機）

// ── A3 休息臉顏色跟電量（Miles 11-20 選方案一）──
// 待機／想睡／睡著的眼睛顏色：≥60% 螢光綠（11-20 改，原藍）、40–59 薄荷綠、20–39 黃、10–19 紅、<10 紅灰閃爍（語音中不閃、充電中不閃）
// 實測電量讀數充電中會在 17～26% 間跳（v22-a3m）→ 每 5 秒讀一次、指數平滑、升級要多 3% 才升
constexpr uint32_t BAT_READ_MS        = 5000;
constexpr float    BAT_SMOOTH         = 0.2f;   // 每次讀數的權重（約 25 秒的平滑）
constexpr int      BAT_HYST_PCT       = 3;
constexpr uint32_t BAT_CHARGE_HOLD_MS = 30000;  // 讀數「充電中／未充電」會來回跳 → 30 秒內見過充電就當充電中
constexpr uint32_t BAT_BLINK_MS       = 500;    // 低電量閃爍半週期（黃紅／紅灰）

// ── A4 量測：充電紀錄（v23-a4m；A4b 定「充飽」門檻用）──
// M5Unified 0.3.0 對 StickS3 的 isCharging()／getChargeState() 只讀 M5PM1 GPIO0 的 CHG_STAT 腳（低＝充電中），
// 只分得出「充電中／未充電」、沒有「充飽」；getBatteryCurrent() 固定 0（無電流感測）；VBUS 與電池電壓讀得到。
// → 每秒讀一次 CHG_STAT（算這 10 秒裡充電中幾秒、切換幾次），每 10 秒存一筆 VBUS／電池電壓／電量／電源來源。
constexpr uint32_t BAT_CHG_POLL_MS    = 1000;   // CHG_STAT 讀取間隔
constexpr uint8_t  BAT_LOG_EVERY_POLLS = 10;    // 幾次讀取存一筆（＝10 秒）
constexpr int      BAT_LOG_SAMPLES    = 2160;   // 環形紀錄筆數（10 秒一筆 ＝ 6 小時，PSRAM 約 26KB）
// ── A4b 充電表情（11-21）：插線＝充電中（看 USB 輸入，不看會來回跳的 CHG_STAT）；「充飽」看電壓（docs/lab/chg-1120-1651.txt）──
constexpr uint8_t  BAT_PLUG_DEBOUNCE  = 2;      // 連續幾次（每秒一次）讀到同樣的「有／沒有 USB」才算數
constexpr int      BAT_FULL_WIN       = 30;     // 電池電壓歷史：10 秒一筆 × 30 ＝ 5 分鐘
constexpr int      BAT_FULL_MV        = 4100;   // 5 分鐘平均 ≥ 這個…
constexpr int      BAT_FULL_RISE_MV   = 15;     // …且 5 分鐘內上升 < 這個（最後 1 分鐘平均 − 最早 1 分鐘平均）→ 充飽
constexpr int      BAT_FULL_EXIT_MV   = 4050;   // 充飽後 5 分鐘平均 < 這個 → 回到充電中

// ── A5-1 資訊頁（Desktop 短按 A 進出；版面見 docs/StickS3 介面規格.html 第 3 節，Miles 11-20 選方案一＋合成波）──
// 數值每秒更新一次（時間跳秒時重畫）；歷史資料不論在哪一頁都持續收，切過去圖表就是滿的。
constexpr int      INFO_HIST_SEC      = 60;     // CHIP 波形：最近 60 秒、每秒一點（該秒內主迴圈最長一圈的 ms）
constexpr int      INFO_HEAP_BARS     = 30;     // MEM 長條：最近 30 根
constexpr uint32_t INFO_HEAP_EVERY_MS = 2000;   // 每 2 秒一根（＝60 秒）
constexpr float    INFO_LOOP_FULL_MS  = 40.0f;  // 波形頂端＝40ms
constexpr float    INFO_LOOP_LIMIT_MS = 25.0f;  // 虛線＝每幀上限 25ms（鐵則八第 11 點）
constexpr int      INFO_NEWS_PAGE_SEC = 8;      // A5-2：快訊每 8 秒換一頁（一頁 2 則；資訊頁本來就每秒重畫，不另外加重畫）
constexpr float    INFO_TILT_FULL_G   = 0.6f;   // IMU 氣泡：傾斜這麼多 g 就到圓邊（與看方向同一個尺度）
// 對時：Wi-Fi 連上後呼叫一次 configTzTime（SNTP 在背景跑，不等待）。台北 UTC+8、無夏令時間。
constexpr const char* NTP_TZ      = "CST-8";
constexpr const char* NTP_SERVER1 = "time.stdtime.gov.tw";   // 國家時間與頻率標準實驗室
constexpr const char* NTP_SERVER2 = "pool.ntp.org";
// Wi-Fi 訊號格（dBm）：≥ −55 四格、≥ −65 三格、≥ −75 兩格、其餘一格；沒連上＝打叉
constexpr int      WIFI_BARS_4 = -55, WIFI_BARS_3 = -65, WIFI_BARS_2 = -75;

// 換整頁（切頁、轉向）時舊畫面溶解成新畫面的時間（交叉淡化，遮住由上而下刷新）；序列埠 t 開關
// 11-20 第一版「背光關掉再亮回 140ms」Miles 實機：遮住了刷新，但黑一下看起來更慢 → 改交叉淡化
constexpr uint32_t UI_XFADE_MS = 120;
// B 切模式的過場（EVA 風，11-20 定案）：新模式資源準備好之後，進度條再跑這麼久才換到新模式的畫面
constexpr uint32_t UI_MODESWITCH_MS = 450;

// ── UI ──
constexpr uint32_t UI_REFRESH_MS = 500;      // 狀態沒變時的定期重畫間隔
constexpr uint32_t LOOP_YIELD_MS = 1;
// ── P1 省電（2026-11-20，Miles：充飽拔線 21 分鐘掉到 67%；以體感＋序列埠 h 電壓紀錄驗證）──
// 規則見 power_save.h；序列埠 z 狀態、z0／z1 關／開、l<n> 正常背光
constexpr uint8_t  PWR_BL_AWAKE        = 110;   // 正常背光（0～255）
constexpr uint8_t  PWR_BL_DROWSY       = 45;    // 想睡
constexpr uint8_t  PWR_BL_ASLEEP       = 12;    // 睡著（微光）
constexpr uint32_t PWR_BL_DIM_STEP_MS  = 6;     // 變暗每降 1 階等多久（110→12 約 0.6 秒）；變亮立刻
constexpr uint32_t PWR_FRAME_CALM_MS   = 33;    // 待機臉約 30fps
constexpr uint32_t PWR_FRAME_DROWSY_MS = 100;   // 想睡 10fps
constexpr uint32_t PWR_FRAME_ASLEEP_MS = 200;   // 睡著 5fps
constexpr uint32_t PWR_INFO_DROWSY_MS  = 10000; // 資訊頁：靜置 10 秒變暗（11-20 Miles；表情頁仍跟想睡臉 6 秒）
constexpr uint32_t PWR_INFO_ASLEEP_MS  = 30000; // 資訊頁：靜置 30 秒微光＋降頻
constexpr uint32_t PWR_YIELD_MAX_MS    = 10;    // 兩張畫面之間每圈最多睡多久（IMU／按鍵／待機收音照常處理）
constexpr bool     PWR_CPU_SCALE       = true;  // 睡著降 CPU 頻率（出問題先改 false 對照）
constexpr uint32_t PWR_CPU_AWAKE_MHZ   = 240;
constexpr uint32_t PWR_CPU_ASLEEP_MHZ  = 80;    // Wi-Fi 最低要 80MHz        // loop 結尾讓出 CPU 的時間（非阻塞）
// B3b-4：中控斷線超過這麼久才顯示斷線提示（重連常在 1～2 秒內完成，避免圖示一閃一閃）
constexpr uint32_t OFFLINE_SHOW_MS = 2000;
// 睡著／關螢幕後主動斷 Wi-Fi，叫醒時重連（重連含 TLS 約 3～5 秒）→ 叫醒後這段時間內不顯示斷線提示
constexpr uint32_t OFFLINE_WAKE_GRACE_MS = 10000;

// ── P2 電源鍵與省電（2026-11-22，規格 docs/KICKOFF_P2.md）──
// 電源鍵由 M5PM1 管（內部 I2C 0x6E）；暫存器依官方 m5stack/M5PM1 src/M5PM1.h
constexpr uint8_t  PM1_ADDR            = 0x6E;
constexpr uint32_t PM1_I2C_HZ          = 100000;
constexpr uint8_t  PM1_REG_WDT_CNT     = 0x0A;  // 看門狗倒數（秒）；0＝關
constexpr uint8_t  PM1_REG_WDT_KEY     = 0x0B;  // 寫 0xA5＝餵狗
constexpr uint8_t  PM1_WDT_FEED_KEY    = 0xA5;
constexpr uint8_t  PM1_REG_BTN_STATUS  = 0x48;  // bit7 BTN_FLAG（讀了就清）、bit0 BTN_STATE（即時）
constexpr uint8_t  PM1_REG_BTN_CFG1    = 0x49;  // bit7 DL_LOCK、bit0 SINGLE_RST_DIS
constexpr uint8_t  PM1_REG_BTN_CFG2    = 0x4A;  // bit0 DOUBLE_OFF_DIS
constexpr uint8_t  PM1_BTN_PRESSED_BIT = 0x01;  // BTN_STATE＝1 當作按下（P2-1 用序列埠 P 實測確認極性）
constexpr uint8_t  PWR_WDT_SEC         = 60;    // 看門狗逾時（USB 燒錄約 20～40 秒，要實測燒錄中不會被重置）
constexpr uint32_t PWR_WDT_FEED_MS     = 1000;  // 主迴圈每秒餵一次
constexpr uint32_t PWR_KEY_POLL_MS     = 20;    // 輪詢電源鍵
constexpr uint32_t PWR_KEY_ARM_MS      = 150;   // 開機後要先看到「放開」這麼久才開始判斷（開機時手可能還按著／極性錯時不會亂動）
constexpr uint32_t PWR_KEY_CLICK_MS    = 700;   // 按住不到這麼久放開＝單按
constexpr uint32_t PWR_KEY_MULTI_GAP_MS = 500;  // 兩次單按間隔 < 這個＝連按
constexpr uint8_t  PWR_KEY_FORCE_OFF_CLICKS = 5;   // 連按五下＝強制關機
constexpr uint32_t PWR_KEY_OVERLAY_MS  = 800;   // 按住超過這麼久開始顯示長按畫面
constexpr uint32_t PWR_KEY_OFF_MS      = 3000;  // 3～5 秒之間放開＝關機
constexpr uint32_t PWR_KEY_ROOT_MS     = 5000;  // 按滿 5 秒放開＝進 root
constexpr uint32_t PWR_WAKE_A_MS       = 1000;  // 關螢幕或睡著變暗時按住 A 多久才醒（11-22 Miles：0.6→1.5→統一 1 秒；短按不理）
constexpr uint32_t PWR_AUTO_SCREEN_OFF_MS = 10000;  // 沒插線時進入睡眠（表情睡著／資訊頁靜置 30 秒）滿 10 秒 → 自動關螢幕（11-22 Miles：30→10 秒）
constexpr uint32_t PWR_RADIO_OFF_MS    = 180000;   // Desktop 睡著或關螢幕滿 3 分鐘 → 斷 Wi-Fi（插線時只看關螢幕）
constexpr float    BAT_CUTOFF_PCT      = 3.0f;  // 實際電量 ≤ 這個＝強制關機；畫面顯示 (實際−3)÷97×100
constexpr uint32_t BAT_CUTOFF_HOLD_MS  = 30000; // 連續這麼久才關（說話、播放時電壓會瞬間掉）

}  // namespace cfg
