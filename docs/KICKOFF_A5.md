# 接手啟動 prompt：M1-A / A5-1 資訊頁（本機資料）

> 用法：開新的 Claude Code（或 Cowork）session，把下面整段貼進去。
> 產生於 2026-11-20（M1-A / A4 完成之後）。

---

我在接手 M5StickS3 桌寵專案（機器名 M5S3Pack，助理叫 Friday；不要叫「小S」）（~/Developer/m5sticks3，公開 repo MilesYP/m5sticks3）。這一片是 **M1-A / A5-1 資訊頁（本機資料）**：Desktop 模式**短按 A（<0.6 秒）**切換到資訊頁、再短按回表情；**按住 A 仍是說話**。資訊頁是密集儀表板，直式與兩種橫式都要排得好（沿用 A4 的拿法判斷）。
我是 PM、不親手寫 code；燒錄用 VS Code 的 PlatformIO 按鈕。回覆用繁體中文、術語要解釋。技術選擇請直接定，真的無法判斷才問我；動工前給一次計畫與要改的檔案，我說「開始實作」才寫。Cowork／橋接環境不能 git push（commit 前先請我允許刪檔），commit 後給我一行 `cd ~/Developer/m5sticks3 && git push`。橋接環境無法完整編譯：韌體用模擬標頭做語法檢查（**模擬標頭要有 Arduino 的 `RX`／`TX` 常數**，A4 曾因撞名編譯失敗）、純邏輯用 g++ 測；畫圖程式可用 AddressSanitizer 跑「每種畫面 × 3 種拿法」的模擬（A4 做法）。

【開工第一件事：先問我】
- 要不要先用 LVGL（或其他工具）排資訊頁的版面截圖給你？我若說要，等我給截圖再定版面；說不用，就照介面規格第 3 節，並先給我 2 個版面示意（直式＋橫式）選。

【先讀】
- CLAUDE.md：鐵則二（互斥狀態機：Desktop 模式下 RF Pack 的 SPI 是關的）、鐵則三（急停）、鐵則六第 10 點（誤觸防護 0.6 秒）、鐵則八（第 8 點拿法判斷、第 9 點畫布轉向、第 11 點畫圖規則）
- docs/PROGRESS.md 最上面 2 筆（最新是 M1-A / A4）
- docs/StickS3 執行計劃.md 的 A5-1、A5-2、A4b 列；docs/StickS3 介面規格.html 第 3 節（資訊頁直式、橫式兩版，欄位相同只是重排）
- firmware/src：`ui_expression.*`（畫布 `M5Canvas::setRotation`、`Orient`、對話區）、`imu_pet.*`（`s_orient`、`[orient]`、序列埠 `o`）、`voice_turn.cpp`（`onPress` 一按就開始錄；`onRelease` 裡 `stopRecording()` 回 0＝誤觸，直接 `toIdle()`）、`audio_io.cpp`（`VOICE_MIN_PRESS_MS` 誤觸判斷）、`battery_ui.*`、`net_hub.*`（Wi-Fi、RSSI、中控連線）、`input_hal.*`（Grove `deviceName()`）、`main.cpp`（`renderActive`：Desktop 畫表情、其他模式畫 `ui_status`）、`config.h`

【目前狀態】
- 表情頁（A4）：合成波表情、直式／橫式 A 左／橫式 A 右（rotation 0／3／1）、對話區在 A 鍵側。每幀約 21～25ms（`j` 的「迴圈≤」，上限 25ms）。
- A 鍵：Desktop 按下就呼叫 `voice_turn::onPress()` 開始錄；放開時若不到 0.6 秒，`audio_io` 判為誤觸、不上傳，`voice_turn` 回 Idle。**短按切資訊頁要接在這個「誤觸」路徑上**（不要改成延遲開錄，否則說話開頭會被吃掉）。
- 序列埠已有：`j`（IMU 量測，含拿法）、`o0/o1/o2`（假裝拿法）、`y0～y8`（預覽表情）、`c<%>`（假裝電量）、`h`（充電紀錄）、`#`（記號）、`e`（Grove 重掃）。

【這片要做（規格來自介面規格第 3 節）】
- 資訊頁欄位（本機能讀的）：時間（Wi-Fi 連上後 NTP 對時，時區台北；沒對到時顯示 `--:--`）、日期與開機時間（UP）、電量 %＋電壓（`battery_ui` 平滑值）、IMU（目前傾斜 X／Y 或拿法）、CPU 頻率、晶片溫度、負載（若取不到就先不放並說明）、記憶體（heap、PSRAM 剩餘）、Wi-Fi 訊號（RSSI）、中控連線（CLOUD ON／OFF）、語音狀態（VOICE IDLE／…）、配件（Grove：CardKB／Scroll／無；RF Pack：Desktop 模式下 SPI 依鐵則二是關的——**不要為了顯示去開 RF**，顯示「未啟用」或上次已知狀態，做法你定並說明）、韌體版本（`AUDIO_BUILD_TAG`）。
- 雲端欄位（天氣、快訊、已存訊號數）是 A5-2，這片先留位置或顯示「—」。
- 直式與兩種橫式都要排：跟 A4 一樣用畫布轉向；資訊頁直式／橫式欄位相同只是重排。可以沿用 `ui_expression` 的畫布，或另開模組（例如 `ui_info.*`）——選一種並說明理由（記憶體：再開一張 135×240×2 的 PSRAM 畫布約 64KB）。
- 資訊頁更新頻率不用每幀：數值 1 秒更新一次即可；時間跳秒要準。
- 在資訊頁時：搖、拍、靜置的表情反應怎麼處理（暫停或背景照跑、回表情頁時接續）由你定並說明；**按住 A 說話時要自動回到表情頁**（對話區在表情頁）。
- 序列埠：切換時印 `[ui] 表情頁 → 資訊頁`；可加一個指令直接切頁方便驗收（字母你挑，避開已用的 w/s/p/g/f/u/m/x/r/b/i/k/e/j/v/c/h/o/y/#/?）。
- 版本標記 `AUDIO_BUILD_TAG` 改 `24-a5`。

【限制】
- 不要動 A3／A4 的互動門檻、拿法判斷、語音流程；急停最高優先、主迴圈不可阻塞（NTP 用非阻塞的 `configTime`，不要等待）。
- 畫面每幀上限 25ms（`j` 的「迴圈≤」）；資訊頁若用很多文字，注意 `efontTW_12` 的繪製成本。
- 完成後更新 `cloud/persona/friday.md`（「現在會做的事」加資訊頁、從「規劃中」移除），並提醒我跑 `cloud/persona.sh`。

【驗收】
1. Desktop 短按 A（<0.6 秒）進出資訊頁；按住 A 照常說話（在資訊頁按住也能說，並回到表情頁）。
2. 數值正確：時間（連網後對時）、電量（可用 `c50` 假裝比對）、Wi-Fi RSSI、記憶體、溫度、Grove 配件（插拔 CardKB／Scroll 後 `e` 重掃）。
3. 直拿、兩種橫拿都正著看、欄位重排不擠不切字；轉到 45° 不閃。
4. 迴圈 ≤25ms；B 長按 3 秒急停照常立即生效；B 短按切模式照常。

【收尾】
通過後：CLAUDE.md 補資訊頁規則；PROGRESS.md 最上面加一筆、改「目前狀態」；執行計劃 md/html 把 A5-1 標 ✅、下一片改 A5-2；寫 `docs/KICKOFF_A5-2.md`（雲端資料：中控加資料端點、定時送天氣快訊）；更新 friday.md；git commit。

【已知待辦（不在這片）】
- A4b 充電表情：Miles 選 B 閃電眼；先做充電實測（`h0` → 充滿再 30 分鐘 → `h`），燒錄或重開會清掉紀錄。
- 體驗細調（整體完工後）：彈珠台、拍打、看方向、表情節奏與配色。
