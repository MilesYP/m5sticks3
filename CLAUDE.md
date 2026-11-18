# M5StickS3 極客口袋終端 — 專案指引 (CLAUDE.md)

> 這份檔案是本專案的常駐上下文，Claude Code 每次進入此資料夾都會讀。
> 完整產品規格見 `docs/M5StickS3+RF Pack S3.md`（PRD）。本檔是「怎麼做」的工程約定，PRD 是「做什麼」。
> 硬體驗收紀錄與實測結論見 `docs/hardware-acceptance-test.md`。
> 程式碼版本控管：GitHub `MilesYP/m5sticks3`（公開），本機位置 `~/Developer/m5sticks3/`。

## 0. 這個專案是什麼

把一支 **M5StickS3（ESP32-S3）** 做成一台可切換四種角色的口袋終端：桌面 AI 語音伴侶、跨裝置 USB 鍵鼠 AI 外掛、掌上 433MHz 射頻工具、空中體感滑鼠。硬體只有 StickS3 本體 ＋ RF Pack S3 ＋ Scroll Unit ＋ CardKB。

**開發者背景**：Miles，PM／前 CAE 工程師，懂架構與系統設計，但不親手寫 production code。你（Claude Code）是他的結對工程師：**他負責判斷「為什麼這樣設計」，你負責產出可編譯的程式、查證腳位、解釋術語、逐步帶他驗證。** 回覆用繁體中文，術語要解釋。

### 專案目錄
| 路徑 | 內容 | 狀態 |
| :-- | :-- | :-- |
| `docs/` | PRD、執行計劃、**介面規格**、啟動 prompt、硬體驗收紀錄 | — |
| `selftest/` | StickS3 本體硬體自檢（PlatformIO 專案） | ✅ 已通過 |
| `acceptance-test/` | RF Pack CC1101/nRF24 SPI 驗收 v5（PlatformIO 專案） | ✅ 已通過 |
| `firmware/` | **正式韌體**（PlatformIO 專案，第 4 節模組放在 `firmware/src/`） | M0 驗收通過；M1-A 切片 1–2、M1-B B1～B3b-4 完成（進度見 `docs/PROGRESS.md`） |

每個子資料夾是獨立的 PlatformIO 專案：VS Code「Open Folder」要開到子資料夾那一層。

## 1. 硬體拓撲與鐵則

### 核心
- **M5StickS3**：ESP32-S3（8MB Flash / 8MB PSRAM）、1.14" 135×240 LCD、BMI270 六軸 IMU、ES8311 音訊 codec ＋ MEMS 麥克風 ＋ 揚聲器、M5PM1 電源晶片、原生 USB-C OTG。
- **RF Pack S3**：CC1101（433MHz）＋ nRF24L01+（2.4GHz）；走**背面 Hat2 排針、SPI** 介面。
- **CardKB**：全鍵盤，**I2C `0x5F`**。
- **Scroll Unit**：滾輪編碼器＋按鍵，**I2C `0x40`**。

### 按鍵（2026-11-12 實機確認）
| 按鍵 | 位置 | GPIO | M5Unified |
| :-- | :-- | :-- | :-- |
| A | 正面藍色大鍵 | G11 | `M5.BtnA` |
| B | 側鍵 | G12 | `M5.BtnB` |
| 電源鍵 | 另一側 | （M5PM1 管理） | 按一下＝開機／重置；快速按兩下＝關機；長按至綠燈閃＝下載模式 |

### 鐵則一：單一 Grove 孔，場景專用
本體只有一個 Grove 孔、無分接線。**CardKB 和 Scroll Unit 永遠二選一，不會同時存在。**
→ 開機時先做 I2C 掃描，偵測到 `0x5F` 就啟用 CardKB 流程，偵測到 `0x40` 就啟用 Scroll 流程，都沒有就走純 RF/攜帶模式。**不要假設哪個一定在。**
→ Grove 匯流排是 `M5.Ex_I2C`（SDA=G9 / SCL=G10）；內部匯流排 `M5.In_I2C`（G47/G48）上固定有 `0x18` ES8311、`0x68` BMI270、`0x6E` M5PM1。Grove 供電要 `M5.Power.setExtOutput(true)`。
→ **`M5.Ex_I2C` 要自己 `begin()`**：M5Unified 0.3.0 只有在 `cfg.external_imu/rtc`（預設皆 false）時才會呼叫 `Ex_I2C.begin()`（原始碼 `_begin_rtc_imu`）。`selftest/` 沒呼叫，未 begin 時掃描是否仍有效尚未確認 → 驗收時 Grove 的 `(none)` 不能當作配件偵測正常的證據，以 `firmware/` M0 插配件實測為準。

### 鐵則二：互斥狀態機（Mutually Exclusive State Machine）
8MB PSRAM 與中斷資源有限，**同一時間只有一個模式的重資源在運作**，切換模式時必須關閉前一個模式的資源再開新的。這是整個韌體的骨架。

| 模式 | 啟用 | 必須關閉/休眠 |
| :-- | :-- | :-- |
| 桌面語音 AI | Wi-Fi, ES8311, CardKB(I2C) | RF Pack(SPI), BLE, USB HID |
| 跨裝置 HID 外掛 | USB OTG HID, Wi-Fi | ES8311, RF Pack, BLE |
| 掌上 RF 工具 | RF Pack(SPI), Scroll(I2C) | Wi-Fi, BLE, ES8311, USB HID |
| 空中體感滑鼠 | BLE HID, BMI270, Scroll(I2C) | Wi-Fi, ES8311, RF Pack |

### 鐵則三：實體急停（Physical Kill Switch）
按鍵配置見第 2 節「按鍵配置決議」。
- **模式 2（HID 外掛）**：**拍一下正面實體按鈕（BtnA / G11）= 立即停止送出所有 HID 封包並斷線**。走 G11 下降緣硬體中斷，不經主迴圈。
- **所有模式**：**側鍵 B 按住滿 3 秒 = 急停備援**（不等放開）。
- **兩段式實作**（`firmware/src/killswitch.*`）：第一段在 ISR＋最高優先權 task 關閉「輸出閘門」（`killswitch::outputAllowed()`），**日後 HID/RF 送出任何封包前都必須先查這個閘門**；第二段由主迴圈呼叫 `mode_manager::emergencyStop()` → 目前模式 `exit()` → `Safe`。
- 急停後停在 `Safe`（紅底 KILLED），**B 短按才回到急停前的模式並重新 `enter()`**，不會自動續跑。
- 急停 task 與 ISR 內不可呼叫 `Serial`（USB CDC 沒接電腦時可能卡住）或任何可能阻塞的函式。

### 鐵則四：腳位絕不臆測
腳位一律依實機驗證過的資料，不確定就先停下來問 Miles 或查官方文件，**不要填一個看起來合理的數字**。以下為已驗證腳位（來源：Bruce `boards/m5stack-sticks3`，2026-11-12 以 `acceptance-test` v5 實機驗證）：

| 用途 | SCK | MISO | MOSI | CS | 其他 |
| :-- | :-- | :-- | :-- | :-- | :-- |
| CC1101 | G5 | G4 | G6 | G2 | GDO0=G3 |
| nRF24L01+ | G5 | G4 | G6 | G8 | CE=G1 |
| SD（保留，初始化時拉高 CS） | G5 | G4 | G6 | G7 | — |
| 螢幕（M5GFX 自動管理） | G40 | — | G39 | G41 | DC=G45、RST=G21、背光=G38 |

紅外線腳位尚未驗證（Bruce 設定為 TX=G46、RX=G42，使用前要實機確認）。

### 鐵則五：RF Pack 驅動規則（2026-11-12 硬體驗收得出，違反會 debug 很久）
1. **RF 走 SPI2（Arduino `FSPI`）**：M5GFX 的 StickS3 螢幕佔用 **SPI3_HOST**，RF 不可用 HSPI（= SPI3），否則螢幕或 RF 會壞。三顆 SPI 裝置共用匯流排，存取任一顆前其他 CS 必須是高電位。
2. **CC1101 一律用 SmartRC-CC1101-Driver-Lib 存取，不要手寫 SPI 時序**。手寫的 `beginTransaction → CS 低 → 傳輸 → CS 高 → endTransaction` 在此板會「每隔一次交易出錯」，曾讀出錯誤的 PARTNUM 而誤判為相容晶片，甚至讓晶片卡死。
   用法：`setSPIinstance(&spi2)` → `setBeginEndLogic(false)` → `setSpiPin(5,4,6,2)` → `setGDO0(3)` → `Init()`（**整個程式只能呼叫一次**）。
3. **RF Pack 吃常開的 3.3V，不需要 `setExtOutput(true)`**（EXT 5V 開關實測不影響 RF）。
4. **CC1101 與 nRF24 的電源不會隨 ESP32 重開機而斷電**：暫存器會殘留上一支韌體（例如 Bruce）的設定；晶片卡住時要「拔 USB → 按兩下電源鍵關機 → 等 10 秒」才會真正重置。程式初始化時不要假設暫存器是出廠預設值。
5. nRF24 用一般 `beginTransaction → CS 低 …` 寫法實測正常（寫入→讀回通過）。
6. **Bruce 的 CC1101 坑不要重蹈（2026-11-16，詳見 `docs/bruce-1.17-rf-findings.md`）**：(a) `Init()`／SRES 只做一次，切頻率或 RX／TX 只改暫存器與下 strobe；(b) 驅動的 `checkMISO()` 等待只在 IDF < 5 有效，目前 `espressif32@6.12.0`（IDF 4.4）沒問題，升 Arduino 3.x 前要自己補「SRES 後等 MISO 低」並重跑 v5；(c) 偵測晶片讀到 VERSION 0x00 要延遲重試，不可一次判死；(d) 不照抄 Bruce 的 `AGCCTRL2=0xC7`，RSSI 功能先用預設 AGC；(e) 切頻率前先 SIDLE。Bruce 上的異常以自有韌體量測為準。

7. **CC1101 的 SPI 讀寫必須防錯（2026-11-17 `rssi-test/` v6.4 實測 6 輪，詳見 `docs/cc1101-spi-reliability.md`）**：這塊板子的單筆 SPI 交易**穩定約有 0.5% 回傳錯值**（12 次量測，每次 200 筆幾乎都恰好錯 1 筆，IDLE 與 RX 相同）。錯值多為 `00`／`03`／`07`／`08`／`0F`／`1F`／`20`／`34`／`F8` 等，形態符合 CC1101 的狀態位元組（含 CHIP_RDY＝未就緒、RXFIFO_OVERFLOW 等狀態），即資料位元組被吃掉或錯位。切換晶片狀態後的第一筆交易特別容易錯。因此：
   - **「連讀到兩次相同」不夠**。實測有兩次出現「兩筆一致但值是錯的」（1/200 與 **14/200**），代表錯誤會成叢發生、同一個錯值會連續出現，不是獨立隨機。
   - **讀暫存器要用多數決（至少讀 3 筆取多數，無多數就擴大到 9 筆）**，而且**關鍵時機要做語意檢查**：切換狀態或改完設定後，先確認 `VERSION` 讀得到 `0x14`，確認通過才信任其他讀值。
   - **寫暫存器一律寫完驗證、不符就重寫**（實測 1～2 次會成功，IDLE 與 RX 皆可）。
   - **絕對不要用函式庫的 read-modify-write 類函式改位元欄位**（`setModulation`／`setPktFormat`／`setRxBW`／`setCCMode` 內部都會先 `Split_*()` 讀回再寫回）：讀錯就會把垃圾寫進晶片。要改這些就自己算出完整位元組再整顆寫入並驗證。
   - **不要做 burst 式連續讀取**。每 2 ms 讀一次 RSSI 可連續數分鐘無事；但高強度連續交易（約 900 筆的壓力測試）結束後，接下來的讀取會卡住約 2 秒（＝ `checkMISO()` 逾時 1 秒 × 2 次）才自行恢復；**兩輪零間隔連跑則會完全鎖死、不自行恢復，要重置板子**。高強度存取之間至少留 2 秒。

### 鐵則六：音訊（ES8311）規則（2026-11-18 B3b-1 實測得出）
1. **麥克風與喇叭不能同時開**：兩者共用 MCLK/BCLK/WS（G18/G17/G15），M5Unified 分別用 `I2S_NUM_1`（麥克風）與 `I2S_NUM_0`（喇叭）。走對講機式：待機停在麥克風，只有播放時切喇叭，播完切回。
2. **麥克風增益要在每次 `M5.Mic.begin()` 之後覆寫**：M5Unified 0.3.0 對 StickS3 的 ES8311 設定是「模擬 PGA 最小（`0x14`=`0x10`）＋數位增益最大（`0x17`=`0xFF`，約 +32dB）」，訊號在晶片內就夾波，調 M5Unified 的 `magnification` 無效。改成 `0x14`=`0x15`（模擬 +15dB，B3b-2 起；原為 `0x17`=+21dB，B3.6 實聽背景嘶聲較多）、`0x17`=`0xBF`（數位 0dB）；`begin()` 會重寫，所以每次都要補寫。`magnification` 用 8。
3. **喇叭的板子預設是 `sample_rate`=22050、`magnification`=1**：取樣率要設成和音訊相同（16000），避免非整數倍重取樣的雜訊；乘數設 4。`config()` 要在 `Speaker.end()` 之後、`begin()` 之前改才生效（`begin()` 已啟動時會直接 return）。
4. **不要自己通斷喇叭功率放大器**：`Speaker.begin()`／`end()` 會透過 M5PM1（`0x6E` 暫存器 `0x11` bit3）開關放大器，通斷本身就有爆音；M5Unified 的防爆音機制只在內建 DAC 時生效，ES8311 不走那段。自己再加一輪通斷實測**更響**。只用 DAC 音量（ES8311 `0x32`，`0xBF`=0dB／`0x00`=靜音）在切換前後靜音。
5. **A 鍵緊貼麥克風**：按下的機械聲在開始錄音後約 40～50ms 撞滿幅，播出來就是「噠」。錄音頭尾各切 60ms（`TRIM_START_MS`／`TRIM_END_MS`）。放開的聲音實測沒撞滿幅。
6. **錄音要「隊列未滿就預排下一塊」**（`M5.Mic.isRecording() < 2` 就 `record()`），塊與塊才連續；等前一塊錄完才排會有空檔。`ps_malloc` 不清零，緩衝要先 `memset`，長度以實際按住時間計算。
7. **ES8311 上電後約 1 秒內擷取可能全為零**（M5Unified 註解）→ 開機就 `Mic.begin()` 暖機，待機保持麥克風開著。
8. **音訊問題先量測再改**：印滿幅樣本數與位置、暫存器讀回值。11-17 連兩次照原始碼推理就改，兩次都沒中。版本標記只印序列埠時，開機那刻監看未連上會丟 → 印在每次錄音的那一行。
9. **Wi-Fi 省電模式（modem sleep）開著時錄音有滋滋聲 → 錄放音期間關省電**（2026-11-18 B3.6 定案）。對照：省電開（T1、T3 與重複兩輪共 4 次）都有、省電關（T2 與重複兩輪共 3 次）都沒有；降低發射功率（`p8`，8.5dBm）**無效**，斷流皆 0。量化指標是不說話錄音的「高頻底噪」（一階差分 2ms 格 RMS 的中位數）：省電開約 −50.4 dBFS、省電關約 −53.5 dBFS，可重複。平均音量（RMS）只差 1～3dB、分不出來；脈衝偵測為 0 → 雜音是**持續的**，不是每 102ms 一下的 beacon 脈衝。機制**推測**為低負載時電源轉換電路改間歇工作、漣波落入可聽頻段（未量電源波形，未證實）。做法（Miles 選方案 B）：`net_hub::voiceActive(true)` 在按下 A 時關省電，播完／離開 Desktop 時 `voiceActive(false)` 開回；關掉後立即生效（錄音開頭也乾淨）。`diag_cmd` 的 `s0`／`s1` 手動覆寫、`sa` 恢復自動。B3c 之後「聽→想→說」整段都要維持關省電。另：不說話錄音有約 160ms 的規律週期（Wi-Fi 關時也有，聽不出來，來源未查）。

### 鐵則七：連網（Wi-Fi／中控 WebSocket）規則（2026-11-18 B3b-2 實測得出）
1. **傳輸速度被往返時間（RTT）綁住**：iPhone 熱點下 ping→pong 約 290～400ms（含中控處理）。ESP32 Arduino 的 TCP 收發緩衝小，每送出約 8～12KB 就要等確認 → 實測上傳約 36KB/s（`sendBIN` 3200 bytes 平均 88ms、偶發 3 秒重傳卡住）、下載約 25KB/s（每次 `loop()` 只收 1 幀）。16kHz PCM16 是 32KB/s、OpenAI 輸出 24kHz PCM16 是 48KB/s → **原始 PCM 跟不上即時**，B3c 前必須壓縮（B3b-3）。
2. **`sendBIN` 是阻塞的**，會卡住呼叫它的迴圈；在主迴圈送音訊時錄音緩衝（2 塊＝200ms）撐不住 → 邊錄邊傳前要把網路移到獨立 task 或加大緩衝。
3. **Wi-Fi 沒連上就不要跑 `WebSocketsClient::loop()`**：它每 5 秒重連，走到 DNS 查詢時可能卡住主迴圈好幾秒（B3b-2 實測按 8 秒只錄到 2 秒）。`net_hub` 已處理。
4. **逾時用「多久沒進展」，不要用固定總時長**：6 秒錄音在熱點上下載就要約 7.5 秒。
5. **每則 WebSocket 訊息有固定成本**（B3b-3 實測）：ADPCM 每則 804 bytes 時下載只剩約 12KB/s；合併成每則約 8KB 後回到約 23～26KB/s（頻寬上限）。下載一律合併成 ≤8040 bytes 一則。
6. **下載格式**：B3b-3 定 μ-law（16KB/s），**B3b-4 改預設 ADPCM 8kHz**（約 4KB/s，序列埠 `f3`；`f2` 切回 μ-law）。原因：11-19 熱點 RTT 升到 630～760ms 時下載只剩約 8.8KB/s，μ-law 跟不上即時。中控先低通（31 階、截止 3.6kHz）再降到 8kHz，裝置用 8 階半帶濾波器升回 16kHz（線性內插只有約 25dB，半帶約 42dB）；實測真人語音 SNR 約 20～22 dB（含 4kHz 以上的頻寬損失）。以下為 B3b-3 原紀錄——下載格式 μ-law（B3b-3，Miles 實聽選定）：16kHz、每取樣 8 bit、16KB/s，SNR 約 36.6 dB，與原音聽不出差；ADPCM（8KB/s，SNR 約 25 dB）聽得出較糊，留作弱網備案（序列埠 `f1`）。5～6 秒錄音的等待回傳：PCM 6.3～6.6 秒、μ-law 約 3.6 秒、ADPCM 約 2.0 秒。裝置↔中控格式由 `start` 的 `up`／`down` 協商，與中控↔OpenAI 的格式無關（中控轉碼）。
7. **熱點連線會被重置**：`ssl_client (-80) Connection lost` 閒置時約每 40～80 秒一次、上傳中也發生過；前後 RTT 飆到 1～3 秒。會自動重連，原因未查。B3b-4 已加 `[link]` 斷線紀錄（連線維持多久、距上次 pong、收送量、RSSI、省電、Wi-Fi 層原因碼）與 `i<秒>` ping 間隔，中控 `wrangler tail` 印 `[ws] close code=…`；11-19 那晚連 185 秒都沒斷，資料待收。**重連（TLS 交握）會卡主迴圈約 2.3 秒**，錄音中發生會少錄 2 秒（B3b-5 網路背景化處理）。
8. **量測先行**：每輪印 `[perf]`（主迴圈、`ws.loop`、送幀耗時、收幀數），ping 回來印 RTT；改傳輸前先看這些數字。
9. **中控 Durable Object 閒置約 10 秒會休眠**（B3b-4 實測）：休眠式 WebSocket 下 DO 被移出記憶體，**連線不斷，但 `this.*` 記憶體全部清空**。按住 A 10 秒期間裝置沒送任何訊息 → 中控忘了這輪格式、照 pcm16 回傳 → 裝置當 ADPCM 解 → 大爆音。規則：**跨訊息要記住的東西一律存 `ws.serializeAttachment()`**（或 storage），不能只放記憶體。B3c 中控要開 OpenAI 連線時同樣要注意（有外連中的 DO 不會休眠，但開之前的設定要能找回）。
10. **邊收邊播（B3b-4）**：中控 stop 後一則一則送（預設 `pace=fast` 全速，`r1` 改照真實速度＝壓力測試；只照真實速度送時緩衝永遠只有預緩衝那 0.3 秒，任何卡頓都會斷音）。**WebSocket 函式庫要等一整則訊息收完才放手，`ws.loop()` 實測最久卡 2.5 秒** → 喇叭不能靠主迴圈餵，改由 M5Unified 播放 task 的 `setBufferReleaseCallback` 自己從環形緩衝拿下一塊。M5Unified 0.3.0 `Speaker.playRaw` 規則（查原始碼）：每聲道只有 2 格隊列；**不指定聲道會挑空聲道 → 兩塊同時混音，一律指定聲道 0**；隊列滿時會原地等 → 先查 `isPlaying(0) < 2`；回呼裡可以 `playRaw`、不可 `begin()/end()`，回呼只能在 `begin()` 前或 `end()` 後設定。實測熱點（RTT 300～500ms）中控送完後 0.6～1.4 秒開始播、0 斷音，緩衝最低 1.6～2.5 秒。

## 2. 四大模式（細節見 PRD 第 2 節）
1. **桌面 AI 語音伴侶**：M5GFX 向量繪製 Grok 風極簡幾何表情（Idle 呼吸眨眼 / Listening 漣漪 / Thinking 旋轉 / Speaking 連動 ES8311 音量）＋ 底部跑馬燈文字。語音優先（麥克風→Wi-Fi→後端 STT/LLM/TTS→喇叭），CardKB 為靜音輸入備援。
2. **跨裝置 HID 外掛**：參考 `esp32-iphone-agent`。S3 當免越獄 USB 複合鍵鼠，iOS（配 AssistiveTouch）或 PC/macOS 自適應；視覺大模型截圖定座標→回傳 HID 指令。含實體急停。
3. **掌上射頻工具**：CC1101 做固定碼（PT2262/SC2260/EV1527）學習、存 NVS、一鍵複製發射；RSSI 掃描以柱狀/瀑布圖顯示，近訊號源時喇叭發蓋革式嗶聲。Scroll Unit 切頻率/選單。台灣遙控器常見 315MHz 與 433.92MHz，兩者都要支援。
4. **空中體感滑鼠**：BMI270 姿態解算（Pitch/Yaw→游標），BLE HID 當藍牙滑鼠。正面鍵＝左鍵，Scroll 滾動＝捲動，Scroll 按壓＝右鍵。

### 按鍵配置決議（2026-11-13，Miles 選定「方案 1」）
原問題：正面鍵 A 同時被分配到切換模式、急停、模式 4 左鍵，互相衝突。決議為「**A＝做事／停，B＝系統鍵**」：

| 操作 | 功能 |
| :-- | :-- |
| B 短按（< 1 秒） | 切到下一模式（Desktop → HidAgent → RfTool → AirMouse 循環）；在 `Safe` 中＝解除急停、回原模式 |
| B 按住 1～3 秒後放開 | 序列埠重印狀態報告 |
| B 按住滿 3 秒 | 全域急停備援（立即觸發，放開不再產生事件） |
| A（模式 2） | 按下即急停（硬體中斷） |
| A（模式 4） | 滑鼠左鍵（模式 4 急停走 B 長按 3 秒） |
| A（模式 1／3） | 模式內主動作（各里程碑再定義） |
| 電源鍵 | **韌體不使用**：單擊＝硬體重置、雙擊＝關機，且 M5Unified 需經 I2C 輪詢 M5PM1 才讀得到，不夠即時 |

被否決的方案：A 全域專職急停（模式 4 失去正面左鍵）、A 長按 1 秒急停（慌亂時撐不滿 1 秒，模式 2 不可靠）。

## 3. 技術棧與函式庫（版本已在驗收專案中實測）
- **建置**：PlatformIO，`platform = espressif32@6.12.0`、`board = esp32-s3-devkitc-1`、Arduino framework。PSRAM／Flash 設定（照抄 `selftest/platformio.ini`，已驗證 8MB PSRAM 正常）：
  ```ini
  board_build.arduino.memory_type = qio_opi
  board_upload.flash_size         = 8MB
  board_build.partitions          = default_8MB.csv
  build_flags = -DBOARD_HAS_PSRAM -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1
  ```
  編譯訊息會顯示 "No PSRAM"（只是 devkitc 預設描述），實際以執行時 `ESP.getPsramSize()` 為準。
- **顯示 / IMU / 音訊 / 電源**：**M5Unified ＋ M5GFX**，用 git tag 鎖 `#0.3.0`（`M5.Display`、`M5.Imu`、`M5.Speaker`、`M5.Mic`、`M5.Power`）。ES8311 同一時間只能喇叭或麥克風其一。
- **USB HID（模式 2）**：Arduino ESP32 核心的 **TinyUSB**（`USBHIDKeyboard` / `USBHIDMouse`，複合裝置）。注意：開了原生 USB HID 後，序列埠除錯要改走第二路 CDC 或 UART。
- **BLE HID（模式 4）**：**NimBLE-Arduino**（比 Bluedroid 省記憶體）實作 BLE Mouse。
- **射頻（模式 3）**：**SmartRC-CC1101-Driver-Lib**（ELECHOUSE_CC1101 的維護分支，Bruce 使用的 bmorcelli 版，鎖 `#7bba5217dc0632f718a3278dc27e401016741a95`）＋ **RCSwitch**（OOK 固定碼解碼/編碼）。用法見鐵則五。
- **資料持久化**：`Preferences`（NVS）存學到的 RF 碼、Wi-Fi 設定、使用者偏好。

## 4. 建議模組切分（放在 `firmware/src/`）
```
firmware/src/
  config.h            // 具名常數：腳位、I2C 位址、按鍵門檻（M0 已建）
  main.cpp            // 進入點：init → mode_manager loop（M0 已建）
  mode_manager.*      // 互斥狀態機；模式切換時呼叫各 HAL 的 enter()/exit()（M0 已建）
  ui_status.*         // M0 狀態畫面＋序列埠報告（M0 已建）
  hal_power.*         // Wi-Fi/BLE/ES8311/RF/USB 的開關與低功耗休眠（鐵則二核心）
  input_hal.*         // 開機 I2C 掃描，辨識 CardKB(0x5F)/Scroll(0x40)，統一輸入事件
  killswitch.*        // 正面按鈕急停，最高優先（鐵則三）
  ui_expression.*     // 模式1：幾何表情（M1-A 已建）
  net_hub.*           // Wi-Fi＋中控 WebSocket（token、RTT、省電切換、二進位收送、格式協商、[link] 斷線紀錄）（M1-B 已建）
  audio_io.*          // ES8311 錄放音、PGA、雜訊量測、邊收邊播環形緩衝（播放 task 自餵）（M1-B 已建）
  voice_turn.*        // 一輪語音：錄→上傳→邊收邊解壓邊播；表情本機決定（M1-B 已建）
  adpcm.*             // IMA ADPCM／μ-law 解碼、8kHz→16kHz 升取樣（M1-B B3b-3／B3b-4 已建）
  diag_cmd.*          // 序列埠診斷指令 w/s/p/g/f/r/b/i/k/?（M1-B B3.6 已建）
  ui_grok.*           // 模式1：M5GFX 幾何表情 + 音量連動 + 跑馬燈
  hid_injector.*      // 模式2：TinyUSB 跨平台鍵鼠 + 急停整合
  rf_transceiver.*    // 模式3：CC1101 學習/存/發 + RSSI 掃描
  air_mouse.*         // 模式4：BMI270 → 低通濾波 → BLE Mouse
```

## 5. 參考專案 esp32-iphone-agent（只對應模式 2）
Repo: https://github.com/onehumanbeing/esp32-iphone-agent
- **核心思路可借**：S3 當「手」（USB HID），大模型當「腦」（截圖→VLM→座標→動作），免越獄免驅動。
- **要簡化**：它的「眼睛」用 ESP32 跑 AirPlay 接收器（UxPlay 移植，GPL、很重）。**模式 2 先做 PC/macOS 版**——由電腦自己截圖送 VLM、回傳 HID 指令給 S3，比 iOS 的 AirPlay 路線單純得多。iOS/AirPlay 當後期挑戰。
- **授權注意**：該 repo 的 `airplay/` 是 GPL-3.0，含它的韌體要連帶 GPL。若借用該段程式，sketch 要標註。

## 6. 建置順序（不要一次做四個模式）
0. **硬體驗收**：`selftest/`（StickS3 本體）通過；RF Pack 的 nRF24 通過；CC1101 **接收正常**（2026-11-17 `rssi-test/` 掃頻確認，11-13 的「接收故障」判定撤回），但 SPI 單筆讀取約 0.5% 會錯，讀寫一律依鐵則五第 7 點防錯。換貨未定（已請 PINGEQUA 用樣品對照）。各里程碑都可照常開發與燒錄。
1. **M0 骨架**（2026-11-13 完成、編譯通過，待實機驗收）：狀態機、Grove 偵測、急停、狀態畫面。
2. **M1-A 表情**：RoboEyes 風圓角眼＋情緒互動（搖晃／拍打／聲音），直橫雙版面。畫面見 `docs/StickS3 介面規格.html`。
3. **開發順序（2026-11-15 定案）**：M1-A → M1-B（語音＋Cloudflare 中控，常駐、上下分區不蓋表情） → M1.5（紅外線萬能遙控器，S3 本體） → M4（空中滑鼠） → M2（HID，語音驅動） → M3（無線電工具箱：**S3 軌** Wi-Fi/BLE 可現在做、**RF 軌** 433/nRF 等新板）。警用無線電（P25 加密）永久排除；FM 收音機、航空監聽列待辦。
每做一個切片就編譯、燒錄、實機驗證、`git commit` + `git push` 再往下。

## 7. 程式規範與約束
- **非阻塞**：主迴圈與各 task 不可用長 `delay()` 卡住；急停與輸入要隨時有反應。用 `millis()` 排程或 FreeRTOS task。
- **PSRAM 預算**：大緩衝（影像、音訊）放 PSRAM，並在 `exit()` 時釋放；切模式前確認前一模式資源已回收。
- **I2C 位址、腳位寫成具名常數**（CardKB `0x5F`、Scroll `0x40`、上表腳位），別散落魔術數字。
- **先寫 2~3 個函式驗證再展開**：例如 RF 先確認能解出一組碼、顯示先確認能畫一張臉，再長成完整模組。
- **每個模組都要能單獨編譯測試**，不要等全部寫完才第一次 build。
- **序列埠**：原生 USB CDC 在電腦打開序列埠前印的訊息會遺失 → 測試／除錯畫面要能「按鍵重印報告」，或以螢幕顯示為主。
- **燒錄**：連不上時「長按電源鍵約 2 秒至綠燈閃 → 放開 → Upload」，燒完短按電源鍵啟動。

## 8. 安全與法律
- RF 與 HID 只用於**自己的設備**：複製自家鐵捲門/插座、控制自己的電腦手機。不對他人設備發射或注入。
- 433MHz 被動接收合法；主動發射僅限自有、低功率。滾動碼（新式車庫/車鑰）不支援也不嘗試。不實作任何干擾（jamming）功能。
- 模式 2 的急停是安全關鍵，任何改動都要保持它最高優先、可立即生效。
- 本 repo 公開：不得提交密碼、API key、Wi-Fi 帳密；這類設定放 NVS 或未追蹤的本機檔（加入 `.gitignore`）。
- **中控連線 token（B3.5）**：裝置連 `/ws?t=<HUB_TOKEN>`。token 只存在 `wrangler secret`（中控）與 `firmware/src/secrets.h`（裝置），**絕不印出、不進 repo、不叫 Miles 貼給你**；要確認 secrets.h 只做格式檢查（長度、字元種類），不讀值。產生／更換用 `cloud/README.md` 的一行指令（token 不經剪貼簿在步驟間傳遞——曾因 Miles 複製下一步指令而蓋掉剪貼簿裡的 token）。
- **`wrangler tail` 的輸出含完整網址（含 token）**：Miles 貼 tail 輸出給你或別人前要遮掉 `?t=` 後面；你若看到未遮的 token，提醒他重跑指令換一組。

## 9. 和 Miles 協作的方式
- 動工前先給簡短計畫與要改的檔案，取得同意再寫（Miles 說「開始實作」才動手；說「先討論」就停在需求與計畫）。
- 不確定的腳位、板子設定、函式庫版本，**先問或先查，不要臆測**。
- 解釋專有名詞；提供可直接編譯/燒錄的完整片段與步驟。
- 每個里程碑結束給一句「怎麼驗收」。
- **進度與記憶以 repo 為唯一真相來源**（Miles 會在 Claude Code CLI、claude.ai/Cowork 等不同 Session 之間切換，各 Session 的記憶不互通，只有 repo 大家都讀得到）。每個里程碑或工作階段結束時：
  1. 在 `docs/PROGRESS.md` 最上方新增一筆紀錄（格式見該檔說明）：日期、Session 類型、做了什麼、驗收結果、遇到的坑、下一步。
  2. 新的設計決議或工程規則寫回本檔（`CLAUDE.md`）對應章節。
  3. `git commit` + `git push`。
- 開始新工作前，先讀 `docs/PROGRESS.md` 最上面幾筆，掌握其他 Session 做到哪裡。
- 若在雲端環境作業：PlatformIO 套件伺服器可能連不上，編譯以 Miles 的 Mac 為準；寫入 Mac 的檔案後要用 checksum（如 `md5`）確認內容正確。
