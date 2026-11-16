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
- **階段**：**M0 實機驗收通過（2026-11-16）** → 進行 M1-A 離線表情（kickoff：`docs/KICKOFF_M1A.md`）；UI 與雲端架構見 `docs/StickS3 介面規格.html`
- **進行中**：M1-B / B3b（即時語音）；B3b-1 本機回放已出聲、剛修連續錄音＋音量待驗，接著 B3b-2 串音訊給中控、B3c 接 GPT-Live
- **未決**：無（按鍵配置已決議，見 `CLAUDE.md` 第 2 節）
- **⚠ 硬體狀態（給所有 Session）**：RF Pack S3 的 **CC1101 接收故障，已決定向 PINGEQUA 申請換貨**（信已備妥，退換期約 2026-11-26 前）。nRF24、StickS3 本體正常。**開發照常（含 M3 程式，不必避開 RF）；新 RF Pack 到貨並通過驗收（v5＋v6）後，才燒錄含 RF 功能的韌體做收發實測。** M0、M1、M4 只用 StickS3 本體，隨時可燒錄。
- **待辦**：
  - 換貨：寄信 → 等賣家回覆／RMA → 寄回 → 收新板（進度由 claude.ai Session 追蹤，結果寫回本檔）
  - 新板到貨驗收：重跑 `acceptance-test` v5（SPI）＋ 新增 v6 RSSI 測試（433.92 MHz 進 RX，讀 MARCSTATE／FSCAL1／RSSI dBm＋峰值保持；有天線、拔天線、按遙控器三種情境，正常背景應約 -95～-105 dBm 且拔天線會變）
  - 插 CardKB／Scroll 確認 Grove 偵測

---

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
