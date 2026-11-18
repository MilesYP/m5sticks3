# 專案進度紀錄（PROGRESS）

> **用途**：這是各個 Session（Claude Code CLI、claude.ai／Cowork…）之間共享進度的唯一地方。各 Session 的記憶不互通，但都讀得到這份檔案。
> **規則**：
> - 新紀錄加在「紀錄」區的**最上方**（最新的在最前面）。
> - 開始工作前先讀最上面 2～3 筆；結束時新增一筆，再 `git commit` + `git push`。
> - 工程規則與設計決議寫在 `CLAUDE.md`；詳細測試數據寫在對應文件（如 `docs/hardware-acceptance-test.md`）。這裡只記摘要與連結。
>
> **每筆紀錄格式**：
> ```
> ### YYYY-MM-DD｜里程碑名稱｜Session 類型（Claude Code CLI / claude.ai / Cowork）
> - 做了什麼：
> - 驗收結果：
> - 遇到的坑／新規則：（新規則要同步寫進 CLAUDE.md）
> - 下一步：
> - 待辦／未決：
> ```

## 目前狀態（每次更新時順手改這一段）
- **階段**：**M1-B 語音對話進行中**（M0、M1-A 切片 1–2、B1～B3b-3 完成）；全部里程碑的切片清單見 `docs/StickS3 執行計劃.md`「切片拆分」
- **進行中**：下一片 **B3b-4 邊收邊播＋斷線處理**（含斷線時畫面提示、表情定格，Miles 11-19 提出）→ B3b-5 上傳邊錄邊傳＋網路背景化 → B3c 接 GPT-Live（端點 `wss://api.openai.com/v1/live/sessions`、PCM 16kHz 不需重取樣）
- **未決**：無（按鍵配置已決議，見 `CLAUDE.md` 第 2 節）
- **硬體狀態（給所有 Session）**：RF Pack S3 **可用但 SPI 不乾淨**（2026-11-17 `rssi-test/` v6.4 共 6 輪實測定案）。接收正常（315.00 MHz −16 dBm、333.00 MHz −18 dBm）。CC1101 **單筆 SPI 讀取穩定約 0.5% 錯**，且錯誤會成叢出現，「連讀兩次相同」被騙過 2 次（最嚴重 14/200）→ 必須用**多數決＋`VERSION`=`0x14` 語意檢查**，寫入一律驗證重寫。高強度連續存取後會卡約 2 秒自行恢復；兩輪零間隔連跑會永久鎖死需重置。規則見 `CLAUDE.md` 鐵則五第 7 點與 `docs/cc1101-spi-reliability.md`。0.5% 錯誤率與 2 秒鎖死都不算正常，但單片無法判定是本片瑕疵或此型通病 → **換貨未定，已請 PINGEQUA 用樣品對照並保留期限**。nRF24、StickS3 本體正常。M3 可照常開發與燒錄（照上述規則寫）。
- **待辦**：
  - **寄信前先 commit＋push `rssi-test/` 與 `docs/`**，信中要附 GitHub 連結（`https://github.com/MilesYP/m5sticks3/tree/main/rssi-test`）給 PINGEQUA 自行復現；`rssi-test/README.md` 已有英文復現步驟
  - 選做：SCAN 模式下用 315 MHz 遙控器在 1 公尺做「有天線／拔天線」對照，確認 SMA 與天線路徑
  - （已完成第 2 步即重現）分離測試（完整斷電後：MONITOR 靜置 3 分鐘 → SCAN 300-348 靜置 2 分鐘 → 遙控器 1 公尺 → 貼近 → 不按遙控器輕壓 RF Pack），記錄 drop／bad 與 checkMISO 錯誤何時出現
  - 回信 PINGEQUA：依分離測試結果決定內容（先前「撤回換貨」草稿作廢）
  - `rssi-test` 待修／M3 動工前要釐清：(1) SCAN 掃 ALL 時底噪約 -55 dBm，只掃 300-348 時約 -101 dBm（同樣 3 ms 穩定時間）→ 不是「RSSI 未穩定」，可能與跨頻段後驅動 `Calibrate()` 留下的 FSCAL2／TEST0 設定有關，未驗證；(2) `[regs]` 的 FREQ2 每次讀回值不同（1E／06／87，應為 10），MARCSTATE 開機頭幾秒偶有讀錯 → 狀態／暫存器讀取的可靠性要查；(3) PKTCTRL0 讀回 3F（預期 32）
  - 插 CardKB／Scroll 確認 Grove 偵測

---

### 2026-11-19｜M1-B / B3b-3 下載壓縮定案（μ-law）＋查證 GPT-Live｜Claude Code（Cowork 橋接）
- 做了什麼：查 OpenAI 官方文件——GPT-Live 端點是 `wss://api.openai.com/v1/live/sessions`（先前寫的 `/v1/realtime` 有誤，已改執行計劃與 `docs/M1B-架構.md`），支援 PCM 16kHz（與裝置相同、不需重取樣），輸入輸出同一格式。切法改為 B3b-3 下載壓縮／B3b-4 邊收邊播／B3b-5 邊錄邊傳（Miles 同意）。中控新增 `cloud/src/adpcm.js`（IMA ADPCM、μ-law 編碼與訊息合併），`start` 帶 `up`／`down` 格式、中控回 `format` 確認；裝置新增 `firmware/src/adpcm.*` 解碼，驗收印 SNR。序列埠 `f0/f1/f2` 切 PCM／ADPCM／μ-law。
- 設計決議（Miles 選定）：上傳原始 PCM（OpenAI 聽得最清楚）、下載壓縮；加格式欄位當保險；**預設 μ-law**，ADPCM 為弱網備案。
- 驗收結果：**通過**。三種格式各錄 2 次（5～6 秒）：PCM 等待回傳 6.3～6.6 秒（CRC 一致）、μ-law 約 3.6 秒（SNR 36.6 dB）、ADPCM 約 2.0 秒（SNR 25 dB）。實聽：μ-law 最清楚、ADPCM 較糊、PCM 與 μ-law 聽不出差。
- 遇到的坑（規則寫入 `CLAUDE.md` 鐵則七第 5～7 點）：(1) 每則 WebSocket 訊息約 67ms 固定成本——804 bytes 的 ADPCM 幀下載只剩 12KB/s，合併成約 8KB 一則後回到 23～26KB/s。(2) 第一次比較時 `f0` 沒生效、PCM 那輪又斷線改本機回放，造成「f0 較快較清楚」的誤判 → 指令要確認有回 `下載格式 → …` 再錄。(3) 開機後第一個序列埠指令常失效 → 解析時略過看不見的字元，並在不認得時印 16 進位以便追查。(4) 熱點 `ssl -80 Connection lost` 變頻繁（閒置約每 40～80 秒、上傳中也有），RTT 偶飆 1～3 秒。
- 下一步：B3b-4 邊收邊播＋斷線處理（含 Miles 要的斷線畫面提示：斷線標誌、表情定格，恢復後繼續或回起始）→ B3b-5 → B3c。
- 待辦／未決：連線重置原因未查；開機第一個指令失效是否已修需燒錄確認。

### 2026-11-18｜M1-B / B3b-2 音訊來回中控（整段上傳）｜Claude Code（Cowork 橋接）
- 做了什麼：中控 `voice_session.js` 收二進位幀暫存（每輪上限 256KB），`stop` 時原樣回傳＋`{"t":"audio_end","n":...}`；B3a 的 stop→OpenAI 文字撤下（`askOpenAI()` 留給 B3c）。裝置新增 `voice_turn.*`（錄音→3200 bytes 幀上傳，每圈 2 幀→等 audio_end→播放），CRC32 比對上傳／收回；中控未連線、上傳失敗、中途斷線退回本機回放；表情本機決定、中控 state 只印出。`audio_io` 拆成 `stopRecording()`／`playBuffer()`＋PSRAM 收音緩衝；PGA 預設改 `0x15`。量測：每輪印 `[perf]`、ping 回來印 RTT。修正：Wi-Fi 沒連上不跑 WebSocket（避免 DNS 卡主迴圈）、等待逾時改「5 秒沒進展」。
- 設計決議（Miles 選定，全照建議）：表情本機決定；中控未連線退回本機回放；背景嘶聲只把 PGA 改 `0x15`（B3c 後使用者聽的是 AI 語音，不值得做雜訊閘）；`/test` 的 stop 不再呼叫 OpenAI。
- 驗收結果：**通過**。十餘輪 CRC 全部一致，最長 6 秒；無滋滋聲；`g15` 實聽優於 `g17`（高頻底噪約 −50～−58 vs −45～−49 dBFS）；`w0` 退回本機回放正常、不再重連；6 秒錄音不再誤判逾時。
- 遇到的坑（規則寫入 `CLAUDE.md` 鐵則七）：(1) **熱點 RTT 約 290～400ms**，加上 ESP32 TCP 緩衝小 → 上傳約 36KB/s（`sendBIN` 3200 bytes 平均 88ms，偶發 3.3 秒重傳）、下載約 25KB/s；6 秒錄音來回 12～13 秒。原始 PCM 跟不上即時，B3c 前必須處理 → 新增切片 B3b-3。(2) Wi-Fi 關閉時 WebSocket 每 5 秒重連、DNS 查詢卡住主迴圈數秒，錄音只錄到 2 秒（已修）。(3) 固定 8 秒逾時對 6 秒錄音不夠（已改）。(4) 舊的「斷流」計數在放開 A 時來不及計 → 改用「按住時間 vs 實際錄到」判斷。
- 下一步：B3b-3 傳輸提速（先比較方案）→ B3c。
- 待辦／未決：錄音上限 6 秒（`CAP_SAMPLES`），邊錄邊傳後再改；中控 WS 偶發 `ssl_client (-80)` 被重置；省電關閉期間的耗電未量；不說話錄音約 160ms 週期來源未查。

### 2026-11-18｜M1-B / B3.6 Wi-Fi 滋滋聲定案｜Claude Code（Cowork 橋接）
- 做了什麼：韌體加序列埠診斷指令 `firmware/src/diag_cmd.*`（`w0/w1` Wi-Fi、`s0/s1` 省電手動、`sa` 省電自動、`p<dBm>` 發射功率、`g<hex>` PGA、`k` ping、`?` 設定），`net_hub` 加對應函式與讀回實際設定的標籤；`audio_io` 每次錄音印量測行（整段 RMS、高頻底噪、脈衝、週期峰、斷流，附 Wi-Fi 設定與 PGA），版本標記印在錄音行。`platformio.ini` 加 `monitor_echo`／`send_on_enter`／`monitor_eol=LF`，Serial Monitor 才能打指令。修正：按下 A 關 Wi-Fi 省電、播完／離開 Desktop 開回（`net_hub::voiceActive()`，Miles 選方案 B）。
- 驗收結果：**通過**。Wi-Fi 連著（WS connected）講話，6 次都沒有滋滋聲，錄音開頭也乾淨；不說話錄音高頻底噪 −53.2 dBFS，與手動關省電相同。
- 量測經過：(1) 省電開 4 次都有滋滋、關 3 次都沒有；降發射功率到 8.5dBm 無效；斷流皆 0。(2) v9 的平均音量指標只差 1～3dB 分不出來，10ms 週期指標永遠落在搜尋下限（寫錯）；v10 改「高頻底噪」（差分 2ms 格 RMS 中位數）才分出來：省電開約 −50.4、關約 −53.5 dBFS，可重複。脈衝偵測為 0 → 雜音是持續的，**不是**原推測的每 102ms beacon 脈衝；機制推測為低負載時電源漣波進入可聽頻段（未證實）。規則寫入 `CLAUDE.md` 鐵則六第 9 點。
- 遇到的坑：上一輪實作途中連線中斷、回覆變英文，重送後我把自己中斷前寫的檔誤判成「另一個 session 在改」→ 以後先比對內容再判斷。
- 新發現：(1) 背景嘶聲（偏高頻）不是 Wi-Fi，是麥克風自身底噪經 PGA +21dB 與正規化放大；`g15`（PGA 降兩階）明顯改善 → 併入 B3b-2。(2) 不說話錄音有約 160ms 規律週期（Wi-Fi 關時也有，聽不出來），來源未查。(3) 中控 WS 偶發 `ssl_client (-80)` 被對方重置後自動重連，B3b-2 傳音訊時留意。
- 下一步：B3b-2（方案 A＋背景嘶聲）→ B3c。
- 待辦／未決：省電關閉期間的耗電增加未量；B3c 之後「聽→想→說」整段要維持關省電。

### 2026-11-18｜M1-B / B3.5 連線 token 驗證｜Claude Code（Cowork 橋接）
- 做了什麼：中控 `/ws` 要求 `?t=<HUB_TOKEN>`，SHA-256＋`crypto.subtle.timingSafeEqual` 固定時間比對，不符回 401、未設定回 503，皆不進 DO（`cloud/src/worker.js`）；/test 頁加 token 輸入框、「記住在這個瀏覽器」（localStorage）、清除鈕。裝置 `net_hub.cpp` 連線網址加 token、斷線印原因、`secrets.h` 缺 `HUB_TOKEN` 會 `#error`。`cloud/README.md` 加產生／更換 token 的一行指令；`docs/M1B-架構.md`、`CLAUDE.md` 第 8 節補規則。
- 設計決議（Miles 選定）：**token 放 query（方案 A）**，裝置與 /test 頁同一套、好維護；代價是 `wrangler tail` 會印出含 token 的網址 → 貼 tail 輸出前要遮掉。header＋subprotocol（方案 C）不採用。裝置 `beginSSL` 不驗中控憑證（函式庫 `setInsecure()`），只連家用 Wi-Fi 與自己的熱點，接受此風險。
- 驗收結果：**通過**。/test 頁不填、填錯被拒，填對可連、記住正常；裝置用正確 token 連上（`[hub] WS connected`、持續收到 pong）；Miles 刻意換錯 token 可見被拒原因。
- 遇到的坑：(1) 第一版 token 設定步驟要 Miles 在步驟間靠剪貼簿傳 token，他複製下一步指令時把剪貼簿蓋掉，中控與 secrets.h 都存到指令文字 → 改成一行指令（token 只存在 shell 變數，直接 pipe 給 `wrangler secret put` 並寫進 secrets.h），規則已寫入 `CLAUDE.md`。(2) iPhone 熱點連不上時要開「最大化相容性」（ESP32 只支援 2.4GHz）並停在熱點設定頁。
- 新發現：(1) **按 A 不送 start/stop 給中控是預期行為**——B3b-1 把 A 改成本機錄放音（`main.cpp` 註解：B3b-2 改回串給中控），表情由本機切換。KICKOFF_B35 的驗收項「按 A 照常切表情（經中控）」不適用。(2) **Wi-Fi 連上後錄音有電波滋滋聲**，未連上時沒有（數據見 `CLAUDE.md` 鐵則六第 9 點）→ 新增切片 B3.6，排在 B3b-2 之前。
- 下一步：B3.6 Wi-Fi 滋滋聲（先量測）→ B3b-2（方案 A）→ B3c。
- 待辦／未決：日後若要上不可信網路，改 `beginSslWithCA` 驗證 Cloudflare 根憑證。

### 2026-11-18｜M1-B / B3b-1 本機錄放音音質定案＋執行計劃改版｜Claude Code（Cowork 橋接）
- 做了什麼：`firmware/src/audio_io.*` 重寫——分塊預排錄音（無空檔）、緩衝清零、ES8311 麥克風增益改「模擬 +21dB／數位 0dB」、喇叭取樣率 22050→16000 與乘數 1→4、DAC 靜音時序、正規化、淡入淡出、頭尾各切 60ms 按鍵聲；序列埠每次錄音印版本標記（`AUDIO_BUILD_TAG`）與滿幅診斷。`main.cpp` 的 `feedRecording()` 改名 `loop()`。`docs/StickS3 執行計劃.{md,html}` 改版：所有里程碑拆切片（M1-A 4、M1-B 11、M1.5 3、M4 3、M2 4、M3 8），RF Pack 狀態改為「接收正常、SPI 需防錯」。
- 驗收結果：**通過**。噠聲消失、音量正常（正規化 ×3～×6）、不破音。
- 遇到的坑（規則已寫入 `CLAUDE.md` 鐵則六）：(1) 噠聲主因是 **A 鍵機械聲被錄進去**（滿幅固定在按下後 38～52ms），不是喇叭；(2) 自己額外通斷喇叭放大器反而更響，已撤回；(3) 夾波在 ES8311 內部（`0x17`=`0xFF`），調 M5Unified `magnification` 無效；(4) 版本標記只印在開機時會被丟掉 → 改印在每次錄音行。
- 下一步：**B3.5 連線 token 驗證**（新 session，接手 prompt `docs/KICKOFF_B35.md`）→ B3b-2（方案 A）→ B3c。
- 待辦／未決：錄音上限 6 秒（`CAP_SAMPLES`），B3b-2 改串流後消失，不另改；`despike()` 實測只修掉 1～4 個、留著無害，B3b-2 時評估移除；每次 `micOn()` 印的 ES8311 讀回行可在 B3b-2 收掉。

### 2026-11-17｜v6.4 六輪完整實測：錯誤率與叢發特性定案｜claude.ai
- 做了什麼：連續跑 6 輪 SPI 可靠性測試（每輪間隔數秒），期間穿插切換 AGC 設定與一次 10 秒擷取。
- 驗收結果：
  - **單筆讀取錯誤率穩定約 0.5%**：12 次量測（IDLE／RX 各 6 次）每次 200 筆幾乎都恰好錯 1 筆。
  - **「連讀到兩次相同」不可靠**：2 次出現「兩筆一致但值是錯的」，分別 1/200 與 **14/200** → 錯誤成叢出現、同一錯值會連續重複，上一輪「連讀確認 400 次 0 錯」的結論**作廢**。
  - 寫入驗證 1～2 次成功，IDLE 與 RX 皆可（先前「RX 寫不進去」的推測也作廢，那是 v6.3 測試設計錯誤造成的）。
  - **6 輪都正常跑完，無永久鎖死**；但 2 次在測試結束、恢復 RSSI 取樣時卡 2022 ms（＝逾時 1011 ms × 2）後自行恢復。永久鎖死只在「兩輪零間隔連跑」時發生。
  - 錯值新增 `08`／`20`／`34`／`C7`／`F8`，其中高位元為 1 者對應 CHIP_RDY＝未就緒或 RXFIFO_OVERFLOW，仍是狀態位元組。
  - 一次 hang 之後的 `[regs]` 大量錯誤，印證剛恢復時讀值特別不可信。
- 修正：`CLAUDE.md` 鐵則五第 7 點改為「多數決（至少 3 筆）＋ `VERSION` 語意檢查」，並補上錯誤叢發特性；`docs/cc1101-spi-reliability.md` 同步改寫並新增第 6 節「這是正常的嗎」。
- 下一步：寄出回信（草稿已附六輪數據）；M3 動工時依新規則實作 `rf_transceiver` 的讀寫層。
- 待辦／未決：換貨決定（退換期可能 11/20）。

### 2026-11-17｜v6.4 定案：讀取偶發錯誤＋高強度存取會鎖死｜claude.ai
- 做了什麼：修好 v6.4（先前 `device_commit_files` 回報成功但檔案沒落地，改用 device_bash 直接改檔），跑 IDLE／RX 對照；再加 v6.5（每列 50 筆、自動中止、SIDLE 恢復測試），避免測試卡死跑 20 分鐘。
- 驗收結果（重置後乾淨一輪）：IDLE 單筆讀取 0/200 錯、RX 1/200 錯；**兩種狀態的連讀確認都 0 失敗 0 錯值**；寫入 1～2 次成功。前 24 筆原始讀值裡偶有 `0F`／`07`／`00`／`34` 等錯值，且狀態切換後第一筆特別容易錯。
- 關鍵發現：**鎖死的觸發條件是「短時間大量連續交易」**。一輪測試（約 900 筆）正常；連按或長按 B 導致兩輪無間隔連跑 → 立刻完全停止回應且不恢復；間隔 2 秒再跑 → 正常。每 2 ms 讀 RSSI 連續 5 分鐘無事。
- 判定：接收與暫存器存取都可用，韌體防錯即可開發 M3。錯誤形態（錯值多為 CC1101 狀態位元組）偏向時序問題而非晶片損壞，但單片無法斷定是否瑕疵。
- 新文件 `docs/cc1101-spi-reliability.md`；`CLAUDE.md` 鐵則五新增第 7 點（連讀確認、寫入驗證、禁用 read-modify-write 函式、禁 burst、高強度存取間留間隔）。
- 下一步：回信 PINGEQUA 附此報告，請其在樣品上對照並保留換貨選項；M3 動工。
- 待辦／未決：換貨決定（退換期可能 11/20）。

### 2026-11-17｜v6.3 測試無效＋改寫為 v6.4（IDLE／RX 對照）｜claude.ai
- 做了什麼：跑 v6.3 的 SPI 時序測試（寫 A5 到 0x04，10 種間隔各讀 200 次）。
- 驗收結果：**所有間隔都 200/200 錯、奇偶各半、hang 0**。但讀回值一律含 `D3`，而 **D3 正是 SYNC1（0x04）的原廠預設值** → 是測試的寫入沒生效，不是讀錯；此測試設計無效，結論不成立。
- 新線索（值得記）：v6.2／v6.3 讀錯時出現的 `01`、`03`、`07`、`0F`、`1F` 都符合 **CC1101 狀態位元組**的格式（bit6-4＝狀態、bit3-0＝FIFO 位元組數；RX 時高位元為 001→1x，IDLE 為 000→0x）→ 推測每隔一筆交易「讀到狀態位元組而非資料位元組」，即資料晚一個位元組，屬軟體／時序問題而非晶片損壞。未驗證。
- 另一線索：驅動 `SpiStart()` 的順序是 `CS 高 → endTransaction → CS 低 → beginTransaction`，CS 先拉低才設定時脈；v5（IDLE）沒事、v6（RX）每隔一筆錯，兩版唯一的結構差異就是晶片狀態。
- 新增 v6.4（取代 v6.3 的測試）：不依賴寫入，改讀唯讀且值已知的 VERSION（應 0x14），在 **IDLE 與 RX 兩種狀態**下各做：前 24 筆原始讀值（看交替規律）、200 筆單筆讀取統計、200 筆「連讀到兩次相同」統計、寫入後驗證要重試幾次。
- 下一步：跑 v6.4 → 若 IDLE 正常、RX 出錯，則為狀態相關的時序問題（韌體可避開，硬體不必換）；若兩者都錯且連讀確認也救不回，才偏向硬體。

### 2026-11-17｜v6.2 結果：不是自發卡住，是讀取時序問題＋v6.3｜claude.ai
- 驗收結果（v6.2，靜置 5 分鐘以上＋按 B 三次）：**hang 0**（取消週期性的連續讀取後不再卡住）。設定 3（C7 01 40）背景約 -80 dBm。
- 新發現：`[regs]` 改成每筆讀取前停 300 µs 後，**每隔一筆就讀錯**（錯值多為 01／03；對的那幾筆值都正確，寫入的 AGC 也讀得回來）。與 v5 手寫 SPI 的「每隔一次交易出錯」是同一現象 → 用 SmartRC 函式庫也會發生，取決於兩次交易的間隔。v6.1 連續讀取時大多正確，但偶爾引發約 2 秒不回應。
- 判讀：規律性很強（嚴格交替），偏向匯流排／驅動時序問題而非隨機的晶片損壞；但只有一片板子，無法排除本片緩衝電路不良。接收與寫入設定實際有效（頻率、AGC 改了都有對應反應）。
- 新增 v6.3：MONITOR 下 B 按住 1 秒執行 SPI 時序測試（暫存器 0x04 寫入 A5，間隔 0～5000 µs 各讀 200 次，單筆讀取與 burst 讀取分開統計）。
- 下一步：跑 v6.3 取得「哪些間隔安全」的表 → 決定韌體規則，並附給 PINGEQUA 請其在樣品上對照。

### 2026-11-17｜分離測試第 2 步即重現＋v6.2｜claude.ai
- 做了什麼：完整斷電後、電量 100%、機器放桌上不碰、不按遙控器，MONITOR 靜置 3 分鐘。
- 驗收結果：**靜置就會發生**。`checkMISO()` 逾時出現在 61.1／62.2 秒、91.0／92.0 秒、121.0／122.0 秒（每次連續 2 筆，約 2 秒沒回應後自行恢復）；rxdrop 0。三次都緊接在每 10 秒一次的 `[regs]`（20 多筆連續讀取）之後，但 18 次 `[regs]` 只有 3 次觸發。逾時時驅動回傳 0x00，所以統計裡出現 -74 dBm 的假值。`[regs]` 的 FREQ2 每次讀回都不同（1E／06／87／E3／20／07／7C／03／1F，應為 10），FREQ1／0 正確。
- 排除：電量、接觸晃動、近距離發射、Bruce 的重置時序 bug（我們是 IDF 4.4）。
- 未排除：晶片或 RF Pack 匯流排緩衝的硬體問題；驅動在「連續密集交易」下的時序問題（v5 的手寫 SPI 也有類似敏感性）。
- 新增 `rssi-test` v6.2：SPI 操作超過 500 ms 記為 hang 並顯示次數；取消週期性 `[regs]`；`[regs]` 內每筆間隔 300 µs、FREQ2 連讀三次。
- 下一步：v6.2 靜置 5 分鐘看 hang 是否仍出現（仍出現＝自發性；不出現＝密集讀取觸發），並回信 PINGEQUA 保留換貨案。

### 2026-11-17｜v6.1 追加：晶片間歇性不回應｜claude.ai
- 做了什麼：Miles 確認 333 MHz＝鐵捲門遙控器、315 MHz＝Kuga 鑰匙。SCAN 切到 300-348 頻段續測。
- 驗收結果：安靜時底噪約 -101 dBm（20 輪穩定）。按鐵捲門遙控器後，整段讀值升高（平均 -87 → -77 dBm）、峰值落在 300.00／336.00／320.25 等不相干頻率（另一次顯示 387）；隨後序列埠連續出現 `checkMISO(): CC1101: Fail checking MISO pin`（每次 1 秒逾時、連續 5 筆），恢復掃幾輪後又再發生，**放開遙控器後仍持續間歇出現**。
- 遇到的坑／新規則：先前「SCAN 底噪 -55 是 RSSI 未穩定」的推測不成立（單一頻段同設定為 -101），已更正文件，回信草稿中相關段落作廢。「硬體正常」的結論改為「接收鏈有反應，但 SPI 回應不穩，待查」。
- 下一步：分離測試（見待辦），判斷是自發、受近距離發射影響，還是機械接觸。
- 待辦／未決：換貨決定、回信內容。

### 2026-11-17｜CC1101 接收驗收 v6／v6.1 實測｜claude.ai
- 做了什麼：Miles 在 Mac 編譯燒錄 `rssi-test/`。v6 首測後加上 v6.1（MARCSTATE 連讀確認、每 10 秒印 `[regs]`、SCAN 掃頻模式）。
- 驗收結果：
  - MONITOR 433.92 MHz／256 kHz：設定 1（AGC 03 40 91）背景平均 -111 dBm；設定 2（C7 00 B2）背景 -96.0 dBm 且完全不動 → `0xC7` 確實讓基線升高變平（方向符合 PINGEQUA 說法，但沒到 Bruce 上的 -50）。
  - 遙控器貼近 10 公分在 433.92 只到 -104 dBm → 遙控器不在 433.92。
  - SCAN（812 kHz、0.75 MHz 一點）：第一支遙控器 **315.00 MHz -16 dBm**（另有 340.50 MHz -36 dBm），第二支 **333.00 MHz -18 dBm**（336.00 MHz -46.5 dBm）；433 一帶無峰值。鐵捲門鑰匙與車鑰匙各是哪一個頻率未確認。
  - VER 14、MARC 0D、v6.1 下 rxdrop 0。
  - **結論：CC1101 接收鏈與頻率合成正常，硬體非故障。**
- 遇到的坑／新規則：手邊的遙控器是 315／333 MHz，不是 433.92；驗收 RF 要先掃頻找訊號源頻率。SCAN 的底噪顯示約 -55 dBm（MONITOR 是 -111），疑為每點換頻進 RX 後 3 ms 就取最大值、抓到 RSSI 起始暫態——Bruce 的 RSSI Spectrum 恆定 -50～-55 可能是同一現象（未驗證）。`FSCAL1` 讀回 00 但頻率正確，不代表異常。
- 下一步：選做天線有／無對照 → 回信 PINGEQUA → M3 動工前釐清待辦中的三個讀值疑點。
- 待辦／未決：見「目前狀態」待辦。

### 2026-11-17｜CC1101 接收驗收 v6 撰寫｜claude.ai
- 做了什麼：新增 `rssi-test/`（獨立 PlatformIO 專案，設定同 `acceptance-test/`）。433.92 MHz、OOK、非同步模式進 RX，顯示 MARCSTATE／FSCAL1／即時 RSSI／5 秒統計／歷史圖；B 切換三組 AGC（晶片出廠值 03 40 91、SmartRC／Bruce 的 C7 00 B2、Bruce 固定頻率 OOK 的 C7 01 40），A 做 10 秒擷取並印結果表，B 長按切 EXT 5V。全程只接收。
- 驗收結果：**尚未編譯、尚未實測**。雲端無法連 PlatformIO 套件庫，只用假標頭＋真實驅動標頭做過語法檢查。
- 遇到的坑／新規則：SmartRC `Init()` 本身就寫入 `AGCCTRL2=0xC7`（與 Bruce 相同），「預設 AGC」必須明確寫回 `03 40 91`；已更正 `docs/bruce-1.17-rf-findings.md` 第 3 節第 4 點。
- 下一步：Miles 在 Mac 上 Build／Upload，照 `rssi-test/README.md` 做 6 組擷取並貼回結果表 → 判定硬體 → 再回信 PINGEQUA。
- 待辦／未決：退換期可能 11/20 截止。

### 2026-11-16｜PINGEQUA 回覆＋Bruce 1.16.1／1.17 原始碼對照｜claude.ai
- 做了什麼：PINGEQUA 回覆判定 RF Pack 硬體正常，症狀來自 Bruce 韌體（`AGCCTRL2=0xC7` 使 RSSI 基線偏高、IDF 5 下 `checkMISO()` 被編譯掉造成偶發 not found、`rf_SquareWave()` 連續初始化兩次）。下載 Bruce `1.16.1` 與 `1.17` 及驅動原始碼比對。
- 驗收結果：官方三點在原始碼皆屬實；**兩個 bug 在 1.17 都還在**（1.17 新增 `radioReady` 參數但 SquareWave 沒用上）。僅為讀碼推論，未實機重現。詳見 `docs/bruce-1.17-rf-findings.md`。
- 遇到的坑／新規則：自有 RF 韌體避坑清單寫入上述文件第 3 節，並同步 `CLAUDE.md` 鐵則五第 6 點。重點：我們用 `espressif32@6.12.0`（IDF 4.4），`checkMISO()` 有效；升 Arduino 3.x 前要補等待並重跑 v5。
- 下一步：回信 PINGEQUA 回報 1.17 未修；寫 v6 在現有板子上獨立驗證 CC1101 接收。
- 待辦／未決：CC1101 接收是否正常仍無自有數據；退換期可能 11/20 截止。

## 紀錄

### 2026-11-16｜M1-B 進度：中控上線、B3a 真語音文字通、B3b-1 回放出聲｜Cowork
- **B1 中控骨架**：`cloud/` Cloudflare Worker + Durable Object，已部署：**https://sticks3-hub.yenpang.workers.dev**（`/ws` WebSocket、`/test` 測試頁）。WebSocket 協定、存對話、ping/pong 全通。
- **B2a/B2b**：裝置連 Wi-Fi + 中控 WS；長按 A → start/stop；中控 state → 切 Listening/Thinking/Speaking 表情。實機通過。Wi-Fi/中控位址在 `firmware/src/secrets.h`（git 忽略）。
- **B3a**：中控收到 stop 真的呼叫 OpenAI（`gpt-4o-mini`，文字）回覆，`/test` 頁驗證真回覆。**OpenAI 走 pay-as-you-go，已儲值 US$10**（Plus 方案沒有免費 API 額度，只有 Pro 才有）。key 以 `wrangler secret put OPENAI_API_KEY` 存在 Worker（不在 repo）。
- **B3b-1**：`audio_io` 本機回放（按住 A 錄音→放開播放）。已出聲，但原本分塊錄音有空檔→小聲/破/偏快；**已改連續錄音＋喇叭音量 220，待實機再驗**（commit d075d81）。
- **表情**：M1-A 切片 1、2 完成，9 種表情（含 Listening/Thinking/Speaking）定案、固定配色。切片 3（IMU 互動）、切片 4（直橫版面）尚未做。
- **架構文件**：`docs/M1B-架構.md`（切片 B1~B6）、`docs/StickS3 介面規格.html`。
- **待辦**：B3b-1 音質再驗 → B3b-2 音訊串中控 → B3c 接 GPT-Live 即時語音 → B4 對話列文字 → B5 新聞快報。另 M1-A 切片 3/4、M3 等新 RF 版。
- **未 push**：本機有多個 commit 未 push（Miles 自行 `git push`）。

### 2026-11-16｜M1-A 切片 2：九種表情定案｜Cowork（VS Code 驗收）
- 做了什麼：`ui_expression` 擴充為 9 種表情，固定配色、平滑過渡：Idle(青) / Listening(綠·漣漪) / Thinking(藍·上看游移＋三點) / Speaking(白·嘴巴隨音量) / Happy(黃·笑眼) / Angry(紅·八字眉) / Scared(紫·發抖冒汗) / Sleepy(灰藍·半閉) / Asleep(暗灰藍·閉眼飄Z)。眼睛放大（32×46）、呼吸改上下平移（±9px、約 2 秒）。Desktop 按 A 循環切換（測試用，切片 3 接 IMU 後移除）。`setSpeakLevel()` 預留給 M1-B 接喇叭音量。
- 驗收結果：**通過**（實機逐一確認 9 種表情、配色、過渡）。
- 下一步：Listening/Speaking 的真實觸發要等 M1-B（麥克風/喇叭）；先做 M1-B 架構。切片 3、4（IMU 互動、直橫版面）稍後補。
- 並行：M1-B 架構規格見 `docs/M1B-架構.md`；介面規格 HTML 的九表情與對話狀態更新，交由另一 session 處理。

### 2026-11-16｜M1-A 切片 1：離線 Idle 表情｜Cowork（VS Code 燒錄驗收）
- 做了什麼：新增 `firmware/src/ui_expression.{h,cpp}`（PSRAM sprite 畫兩個青色圓角眼睛、呼吸起伏＋隨機眨眼）；`main.cpp` 加 `renderActive()`，Desktop 模式畫表情、其餘模式維持 `ui_status`。
- 驗收結果：**通過**（Build 無紅字、Upload、實機）— 開機即 Idle 表情；B 切模式正常；HidAgent 按 A 急停、B 恢復皆正常。
- 調整：呼吸幅度 ±3→±7、週期放慢（Miles 反映原幅度太小看不出）。
- 下一步：切片 2 — 六種情緒（開心/生氣/驚嚇/想睡/睡著）與眼型，固定配色，平滑過渡。

### 2026-11-16｜M0 實機驗收通過｜（VS Code 燒錄）
- 做了什麼：在 Mac 用 VS Code（PlatformIO）燒錄 `firmware/` M0 骨架、實機驗收。
- 驗收結果：**通過** — 開機顯示 Desktop；B 短按循環四模式、序列埠印 exit→enter；HidAgent 按 A 即急停（紅底 KILLED）、B 恢復；AirMouse 按 A 不停、B 長按 3 秒才停；A/B 交錯連打不當機。
- 待補：Grove 配件偵測（插 CardKB 0x5F／Scroll 0x40）因配件尚未到貨，待到貨補測；目前無配件顯示 None 正常。
- 工具：Miles 用 VS Code 操作（非終端機），後續說明以 VS Code 按鈕為準。
- 下一步：M1-A 切片 1（離線 Idle 呼吸＋眨眼），貼 `docs/KICKOFF_M1A.md` 進 Claude Code CLI 執行。

### 2026-11-15｜介面規格定稿、M1-A kickoff｜Cowork
- 做了什麼：`docs/StickS3 介面規格.html` 收尾（語音對話列：直式 ≤5 行、橫式對話欄寬＝直式對話列高、同比例；資訊頁補橫式；選單直式整頁 5 項／橫式一頁三個）。新增 `docs/KICKOFF_M1A.md`（M1-A 離線表情的 CLI 啟動 prompt，四個切片）。已 commit（未 push）。
- 分工提醒：韌體編譯/燒錄在 Mac 的 Claude Code CLI（有 PlatformIO 工具鏈）；Cowork/claude.ai 這端做規格與文件。
- 下一步（依序執行）：
  1. **M0 實機驗收**（你在 Mac：長按電源鍵約 2 秒到綠燈閃→放開→`pio run -t upload`；照 11-13 紀錄的 5 點清單測）。
  2. **M1-A**：VS Code 終端開 Claude Code CLI（plan mode），貼 `docs/KICKOFF_M1A.md`，一個切片一個切片做。

### 2026-11-15｜UI 與雲端架構定案、文件改名｜Cowork
- 做了什麼：
  - `docs/StickS3 養成計畫.{html,md}` → 改名 **執行計劃**（當規格/說明書用）。
  - 新增 `docs/StickS3 介面規格.html`（介面說明書）：表情（RoboEyes 風＋互動）、語音「上下分區」不蓋表情、資訊頁（密集儀表板）、一頁三個選單、功能模式、直橫雙版面、待辦清單。
  - 執行計劃路線圖改版；`CLAUDE.md` 第 6 節更新開發順序。
- 架構決議（本次對話）：
  - **雲端中控**：Cloudflare Workers＋Durable Objects（存對話、工具呼叫）；StickS3 常連中控（WebSocket 休眠省電），講話時才開 OpenAI 語音連線。
  - **語音**：GPT-Live-1（$0.05/分，Pro 送的 API 額度可付）；後端先全用 GPT（Responses 委派，少一段往返），之後再做可換後端（Claude/Grok）的機制。常駐背景，長按 A／喚醒詞叫出，對講機式一人一句。
  - **快報**：Cloudflare Workers AI 免費額度（10k neurons/日）定時抓新聞摘要＋TTS。
  - **位置**：GPS 取消（板子無 GPS，nRF24/CC1101 收不到手機基地台）。Wi-Fi 定位列未來選項。
- 里程碑定案：M1-A → M1-B → **M1.5 紅外線萬能遙控器（新增，S3 本體）** → M4 → M2 → **M3 無線電工具箱（S3 軌 Wi-Fi/BLE 可現在做；RF 軌 433/nRF 等新板）**。
- 排除/待辦：警用無線電（P25 數位加密，技術不可解＋截收違法）**永久排除**；FM 收音機（需 TEA5767、佔 Grove 孔）、航空/航管監聽（需 RTL-SDR）列**待辦**，規劃內功能完成後再評估。
- 下一步：M0 實機驗收（不需 RF Pack）→ M1-A 表情。

### 2026-11-13｜養成計畫文件更新＋RF 開發規則調整｜Cowork
- 做了什麼：`docs/StickS3 養成計畫.html` 新增「目前進度」（M0～M4 路線圖＋RF Pack 換貨支線＋養成關卡對照表）與「M0 圖解」（可操作的畫面／按鍵模擬器、狀態機圖、開機與主迴圈流程、M0 驗收清單）；第 6 關加上換貨註記。`.md` 同步更新（含 Obsidian 相容的 Mermaid 圖）。
- 新規則：RF Pack 換貨期間**開發不必避開 RF（M3 程式照寫）**，只有燒錄實測要等新板通過 v5＋v6 驗收。已同步改 `CLAUDE.md` 第 6 節與本檔「目前狀態」。
- 下一步：M0 實機驗收（不需裝 RF Pack）→ M1-A。

### 2026-11-13｜CC1101 收訊複驗（汽車鑰匙）｜claude.ai
- 做了什麼：刷回 Bruce 1.16.1（勾 Erase flash 後 RF Module 會重置成 `M5 RF433T/R`，要改回 `CC1101`）；用福特 Kuga 鑰匙（滾動碼、FSK 調變）只做接收測試。
- 驗收結果：Bruce「Spectrum」無反應——查原始碼（`rf_spectrum.cpp`）確認它只畫 OOK 開關脈衝，FSK 鑰匙本來就看不到。改用「RSSI Spectrum」固定 433.92MHz：有小尖峰，**但不按鑰匙時也會出現同樣大小的小尖峰**（背景雜訊），無法和按鍵對應 → **鑰匙測試不成立**。範圍掃描（300-348／387-464／779-928／All）也無明顯反應（掃頻輪詢可能錯過短脈衝）。
- 判定：**CC1101 收訊功能仍未確認**。目前唯一正面證據是 11-12 公司大門遙控器（OOK，433.92）在 Spectrum 上時有時無、不按時無線。SPI 讀寫（v5）已確認正常，問題範圍縮小在 RF 前端（天線、匹配、靈敏度）或測試訊號源（鑰匙頻率不是 433.92、電池弱）。
- 學到（Bruce 操作）：A＝選擇、B＝下一個、B 雙擊＝上一個、B 長按＝返回；頻率／範圍選單要在 RSSI Spectrum 畫面內按 A 叫出（Config → RF Frequency 只是輸入預設頻率的鍵盤）。
- **追加診斷（同晚）**：RSSI Spectrum 在 433.92／315／868 每個頻率都是「開頭一下 -20（啟動假訊號）→ 之後恆定約 -55 dBm 的水平直線」。正常背景應約 -95～-105 dBm。**拔掉天線讀值不變；拔 USB、遠離電腦與電子產品讀值仍不變** → 讀值與天線輸入無關，排除外部干擾。
- **再追加**：切換 Bruce 頁面（例如進 RF → Scan/Copy）時會間歇跳出紅底「CC1101 not found」，有時閃一下就進去、有時卡很久。當時電池僅約 12%。賣家疑難排解寫明：模組無回應／卡住 → 確認側邊電源開關、重插確保排針沒歪、**電池保持 30% 以上**。→ 在宣判硬體故障前，先排除**供電不足與排針接觸**（充電 >50%、重插並鎖螺絲、完整斷電重開）再重測。
- 補充症狀：RF → **SquareWave Spec 每次必定**跳出「CC1101 not found」並退回主選單（Bruce 在 `initRfModule()` 呼叫 `getCC1101()` 讀 VERSION 得到 0 時顯示此錯誤，代表當下 SPI 讀不到晶片回應）。
- **11-13 上午複測**：電池充飽後，RSSI Spectrum 433.92 仍恆定約 -50 dBm → **排除電量不足**，維持換貨判斷。
- **11-13 上午**：已重插 RF Pack 並重測，仍恆定約 -50 dBm → 排除接觸不良。**結論：CC1101 接收故障，維持換貨。**
- **決定**：為趕在退換期內，先以「CC1101 接收故障」向 PINGEQUA（support@pingequa.com）**申請換貨**（英文信已備，附 RSSI 有／無天線照片與影片）。若充電＋重插後問題消失，再回信取消。
- 判定：**CC1101 接收路徑疑似故障**（RF 前端、匹配電路或 PLL 未鎖定），但尚未排除「Bruce 在 StickS3 上的 RSSI 讀取有問題」這個軟體可能。
- 下一步：用自有韌體（SmartRC，已驗證 SPI 正常）做 `acceptance-test` v6：設 433.92MHz 進 RX → 讀 MARCSTATE（應為 0x0D=RX）、FSCAL1（0x3F 代表 PLL 未鎖定）、RSSI dBm 數值＋峰值保持，有天線／無天線／按鑰匙三種情境對照。若仍「恆定且與天線無關」→ 附數據向 PINGEQUA 申請換貨（退換期約 2026-11-26 前）。

### 2026-11-13｜M0 韌體骨架｜Claude Code CLI
- 做了什麼：按鍵配置決議（方案 1：A＝做事／模式 2 急停，B＝切模式／1～3 秒重印／3 秒急停備援，電源鍵不用），寫回 `CLAUDE.md` 第 2 節與鐵則三。新建 `firmware/`：`config.h`、`input_hal`（Grove 掃描＋按鍵事件）、`killswitch`（G11 中斷＋最高優先 task 關輸出閘門，兩段式）、`mode_manager`（四模式＋Safe，先 exit 再 enter）、`ui_status`（狀態畫面＋序列埠報告）、`main.cpp`。
- 驗收結果：Mac 上 `pio run` 逐步編譯皆 SUCCESS（RAM 7.3%、Flash 16.6%）。**尚未燒錄實測。**
- 驗收步驟：(1) 開機顯示 Desktop＋輸入裝置；(2) B 短按循環四模式，序列埠印 exit→enter；(3) 插 CardKB／Scroll 開機能辨識；(4) HidAgent 按 A → 紅底 KILLED，B 短按恢復；AirMouse 按 A 不急停、B 按住 3 秒急停；(5) A／B 交錯連打 20 次不當機。
- 遇到的坑／新規則：M5Unified 預設不 `begin()` `Ex_I2C`，selftest 的 Grove `(none)` 結果可信度待確認 → 韌體明確 `begin()`（已寫入 `CLAUDE.md` 鐵則一）。
- 下一步：實機驗收 M0 → M1-A 模式 1 離線表情。

### 2026-11-12｜CLAUDE.md 更新與跨 Session 進度機制｜claude.ai
- 做了什麼：`CLAUDE.md` 修正文件路徑、新增按鍵表、已驗證腳位表、鐵則五（RF Pack 驅動規則）、實測版本；`docs/KICKOFF_PROMPT.md` 改寫為 M0 骨架 prompt；新增本檔，並在 `CLAUDE.md` 第 9 節規定各 Session 結束時要更新本檔。
- 下一步：在 Mac 的 VS Code 終端機啟動 Claude Code CLI，以 plan mode 貼上 M0 prompt。
- 待辦／未決：正面鍵 A 職責衝突待決議。

### 2026-11-12｜Git／GitHub 建立｜claude.ai
- 做了什麼：專案移至 `~/Developer/m5sticks3`（移出 iCloud）；建立根目錄 `.gitignore`、`README.md`；推上公開 repo `MilesYP/m5sticks3`。
- 遇到的坑：`.pio/` 動輒數百 MB 必須忽略；賣家網頁離線存檔（PINGEQUA）有著作權，不上傳。

### 2026-11-12｜硬體驗收四階段｜claude.ai
- 做了什麼：`selftest/`（StickS3 本體自檢）、Bruce 1.16.1 基準（RF Pack）、`acceptance-test/` v1→v5（自有工具鏈讀寫 CC1101／nRF24）。
- 驗收結果：全數通過。CC1101 為 TI 原廠（PARTNUM 0x00／VERSION 0x14），寫入→讀回 16/16、400 次讀取 0 錯誤；nRF24 PASS。詳見 `docs/hardware-acceptance-test.md`。
- 遇到的坑／新規則（已寫入 `CLAUDE.md` 鐵則五）：手寫 CC1101 SPI 時序會每隔一次交易出錯 → 一律用 SmartRC 函式庫；RF 走 SPI2（螢幕佔 SPI3）；RF Pack 不需 EXT 5V；CC1101 不隨 ESP32 重開機斷電。
- 待辦：汽車鑰匙複驗 CC1101 收訊穩定度。
