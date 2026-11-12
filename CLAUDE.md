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
| `docs/` | PRD、養成計畫、啟動 prompt、硬體驗收紀錄 | — |
| `selftest/` | StickS3 本體硬體自檢（PlatformIO 專案） | ✅ 已通過 |
| `acceptance-test/` | RF Pack CC1101/nRF24 SPI 驗收 v5（PlatformIO 專案） | ✅ 已通過 |
| `firmware/` | **正式韌體**（PlatformIO 專案，第 4 節模組放在 `firmware/src/`） | 尚未建立 |

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

### 鐵則二：互斥狀態機（Mutually Exclusive State Machine）
8MB PSRAM 與中斷資源有限，**同一時間只有一個模式的重資源在運作**，切換模式時必須關閉前一個模式的資源再開新的。這是整個韌體的骨架。

| 模式 | 啟用 | 必須關閉/休眠 |
| :-- | :-- | :-- |
| 桌面語音 AI | Wi-Fi, ES8311, CardKB(I2C) | RF Pack(SPI), BLE, USB HID |
| 跨裝置 HID 外掛 | USB OTG HID, Wi-Fi | ES8311, RF Pack, BLE |
| 掌上 RF 工具 | RF Pack(SPI), Scroll(I2C) | Wi-Fi, BLE, ES8311, USB HID |
| 空中體感滑鼠 | BLE HID, BMI270, Scroll(I2C) | Wi-Fi, ES8311, RF Pack |

### 鐵則三：實體急停（Physical Kill Switch）
任何模式下、尤其模式 2（HID 外掛），**拍一下正面實體按鈕（BtnA / G11）= 立即停止送出所有 HID 封包並斷線**。這段邏輯必須是最高優先、不可被其他工作阻塞（放在獨立高優先 task 或中斷旗標）。

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

## 2. 四大模式（細節見 PRD 第 2 節）
1. **桌面 AI 語音伴侶**：M5GFX 向量繪製 Grok 風極簡幾何表情（Idle 呼吸眨眼 / Listening 漣漪 / Thinking 旋轉 / Speaking 連動 ES8311 音量）＋ 底部跑馬燈文字。語音優先（麥克風→Wi-Fi→後端 STT/LLM/TTS→喇叭），CardKB 為靜音輸入備援。
2. **跨裝置 HID 外掛**：參考 `esp32-iphone-agent`。S3 當免越獄 USB 複合鍵鼠，iOS（配 AssistiveTouch）或 PC/macOS 自適應；視覺大模型截圖定座標→回傳 HID 指令。含實體急停。
3. **掌上射頻工具**：CC1101 做固定碼（PT2262/SC2260/EV1527）學習、存 NVS、一鍵複製發射；RSSI 掃描以柱狀/瀑布圖顯示，近訊號源時喇叭發蓋革式嗶聲。Scroll Unit 切頻率/選單。台灣遙控器常見 315MHz 與 433.92MHz，兩者都要支援。
4. **空中體感滑鼠**：BMI270 姿態解算（Pitch/Yaw→游標），BLE HID 當藍牙滑鼠。正面鍵＝左鍵，Scroll 滾動＝捲動，Scroll 按壓＝右鍵。

### ⚠ 待決議：正面鍵 A 的職責衝突
目前規格讓正面鍵 A 同時負責：(1) 骨架階段的「循環切換模式」、(2) 鐵則三的「急停」、(3) 模式 4 的「滑鼠左鍵」。三者互相衝突（例如模式 2 中按 A 想切模式卻觸發急停，或模式 4 點左鍵觸發急停）。
**開始寫骨架前必須先和 Miles 確認按鍵配置**（候選方向：側鍵 B 負責切模式／選單；A 在模式 2 專職急停；模式 4 的急停改為長按或其他手勢）。決議後更新本節與鐵則三。

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
  main.cpp            // 進入點：init → mode_manager loop
  mode_manager.*      // 互斥狀態機；模式切換時呼叫各 HAL 的 enter()/exit()
  hal_power.*         // Wi-Fi/BLE/ES8311/RF/USB 的開關與低功耗休眠（鐵則二核心）
  input_hal.*         // 開機 I2C 掃描，辨識 CardKB(0x5F)/Scroll(0x40)，統一輸入事件
  killswitch.*        // 正面按鈕急停，最高優先（鐵則三）
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
0. ✅ **硬體驗收**（2026-11-12）：`selftest/`、`acceptance-test/` 全數通過。
1. **骨架**（下一步）：`firmware/` PlatformIO 專案、`mode_manager` 狀態機、`input_hal` I2C 偵測、`killswitch`。能開機、能切換「空模式」。**先解決第 2 節的按鍵待決議。**
2. **模式 1 離線版**：Grok Idle 表情＋眨眼，不連網。驗證顯示與狀態機。
3. 模式 1 疊音訊與 Wi-Fi 後端 → 模式 3（RF，有實體成果最有成就感）→ 模式 4（BLE 滑鼠）→ 模式 2（最複雜，留最後）。
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

## 9. 和 Miles 協作的方式
- 動工前先給簡短計畫與要改的檔案，取得同意再寫（Miles 說「開始實作」才動手；說「先討論」就停在需求與計畫）。
- 不確定的腳位、板子設定、函式庫版本，**先問或先查，不要臆測**。
- 解釋專有名詞；提供可直接編譯/燒錄的完整片段與步驟。
- 每個里程碑結束給一句「怎麼驗收」，並提醒 commit + push。
- 若在雲端環境作業：PlatformIO 套件伺服器可能連不上，編譯以 Miles 的 Mac 為準；寫入 Mac 的檔案後要用 checksum（如 `md5`）確認內容正確。
