# M5StickS3 極客口袋終端 — 專案指引 (CLAUDE.md)

> 這份檔案是本專案的常駐上下文，Claude Code 每次進入此資料夾都會讀。
> 完整產品規格見 `M5StickS3+RF Pack S3.md`（PRD）。本檔是「怎麼做」的工程約定，PRD 是「做什麼」。

## 0. 這個專案是什麼

把一支 **M5StickS3（ESP32-S3）** 做成一台可切換四種角色的口袋終端：桌面 AI 語音伴侶、跨裝置 USB 鍵鼠 AI 外掛、掌上 433MHz 射頻工具、空中體感滑鼠。硬體只有 StickS3 本體 ＋ RF Pack S3 ＋ Scroll Unit ＋ CardKB。

**開發者背景**：Miles，PM／前 CAE 工程師，懂架構與系統設計，但不親手寫 production code。你（Claude Code）是他的結對工程師：**他負責判斷「為什麼這樣設計」，你負責產出可編譯的程式、查證腳位、解釋術語、逐步帶他驗證。** 回覆用繁體中文，術語要解釋。

## 1. 硬體拓撲與鐵則

### 核心
- **M5StickS3**：ESP32-S3（8MB Flash / 8MB PSRAM）、1.14" 135×240 LCD、BMI270 六軸 IMU、ES8311 音訊 codec ＋ MEMS 麥克風 ＋ 揚聲器、原生 USB-C OTG、正面實體大按鈕。
- **RF Pack S3**：CC1101（433MHz）＋ nRF24L01+（2.4GHz）；走**背面 Hat2 排針、SPI** 介面。
- **CardKB**：全鍵盤，**I2C `0x5F`**。
- **Scroll Unit**：滾輪編碼器＋按鍵，**I2C `0x40`**。

### 鐵則一：單一 Grove 孔，場景專用
本體只有一個 Grove 孔、無分接線。**CardKB 和 Scroll Unit 永遠二選一，不會同時存在。**
→ 開機時先做 I2C 掃描，偵測到 `0x5F` 就啟用 CardKB 流程，偵測到 `0x40` 就啟用 Scroll 流程，都沒有就走純 RF/攜帶模式。**不要假設哪個一定在。**

### 鐵則二：互斥狀態機（Mutually Exclusive State Machine）
8MB PSRAM 與中斷資源有限，**同一時間只有一個模式的重資源在運作**，切換模式時必須關閉前一個模式的資源再開新的。這是整個韌體的骨架。

| 模式 | 啟用 | 必須關閉/休眠 |
| :-- | :-- | :-- |
| 桌面語音 AI | Wi-Fi, ES8311, CardKB(I2C) | RF Pack(SPI), BLE, USB HID |
| 跨裝置 HID 外掛 | USB OTG HID, Wi-Fi | ES8311, RF Pack, BLE |
| 掌上 RF 工具 | RF Pack(SPI), Scroll(I2C) | Wi-Fi, BLE, ES8311, USB HID |
| 空中體感滑鼠 | BLE HID, BMI270, Scroll(I2C) | Wi-Fi, ES8311, RF Pack |

### 鐵則三：實體急停（Physical Kill Switch）
任何模式下、尤其模式 2（HID 外掛），**拍一下正面實體按鈕 = 立即停止送出所有 HID 封包並斷線**。這段邏輯必須是最高優先、不可被其他工作阻塞（放在獨立高優先 task 或中斷旗標）。

### 鐵則四：腳位絕不臆測
RF Pack 的 SPI 腳位（SCK/MOSI/MISO/CS/GDO0）、紅外線腳位、Grove I2C 腳位，都**依 StickS3 與 RF Pack S3 的實際規格 / Bruce 的 `m5stack-sticks3` pin map 確認**，不確定就先停下來問 Miles 或查官方文件，**不要填一個看起來合理的數字**。錯的腳位會讓人 debug 很久。

## 2. 四大模式（細節見 PRD 第 2 節）
1. **桌面 AI 語音伴侶**：M5GFX 向量繪製 Grok 風極簡幾何表情（Idle 呼吸眨眼 / Listening 漣漪 / Thinking 旋轉 / Speaking 連動 ES8311 音量）＋ 底部跑馬燈文字。語音優先（麥克風→Wi-Fi→後端 STT/LLM/TTS→喇叭），CardKB 為靜音輸入備援。
2. **跨裝置 HID 外掛**：參考 `esp32-iphone-agent`。S3 當免越獄 USB 複合鍵鼠，iOS（配 AssistiveTouch）或 PC/macOS 自適應；視覺大模型截圖定座標→回傳 HID 指令。含實體急停。
3. **掌上射頻工具**：CC1101 做固定碼（PT2262/SC2260）學習、存 NVS、一鍵複製發射；RSSI 掃描以柱狀/瀑布圖顯示，近訊號源時喇叭發蓋革式嗶聲。Scroll Unit 切頻率/選單。
4. **空中體感滑鼠**：BMI270 姿態解算（Pitch/Yaw→游標），BLE HID 當藍牙滑鼠。正面鍵＝左鍵，Scroll 滾動＝捲動，Scroll 按壓＝右鍵。

## 3. 技術棧與函式庫
- **建置**：PlatformIO（`platform = espressif32`，Arduino framework）。board 以 ESP32-S3 為基底，確認 StickS3 的 PSRAM（OPI）與 flash 設定。
- **顯示 / IMU / 音訊**：**M5Unified ＋ M5GFX**（`M5.Display`、`M5.Imu`（BMI270）、`M5.Speaker`、`M5.Mic`）。
- **USB HID（模式 2）**：Arduino ESP32 核心的 **TinyUSB**（`USBHIDKeyboard` / `USBHIDMouse`，複合裝置）。注意：開了原生 USB HID 後，序列埠除錯要改走第二路 CDC 或 UART。
- **BLE HID（模式 4）**：**NimBLE-Arduino**（比 Bluedroid 省記憶體）實作 BLE Mouse。
- **射頻（模式 3）**：**ELECHOUSE_CC1101** 驅動 ＋ **RCSwitch**（OOK 固定碼解碼/編碼）。這也是 Bruce 的路線。
- **資料持久化**：`Preferences`（NVS）存學到的 RF 碼、Wi-Fi 設定、使用者偏好。

## 4. 建議模組切分
```
src/
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
先把骨架與一個垂直切片做穩，再逐一疊模式：
1. **骨架**：PlatformIO 專案、`mode_manager` 狀態機、`input_hal` I2C 偵測、`killswitch`。能開機、能用正面按鈕切換「空模式」。
2. **模式 1 離線版**：Grok Idle 表情＋眨眼，不連網。驗證顯示與狀態機。
3. 模式 1 疊音訊與 Wi-Fi 後端 → 模式 3（RF，有實體成果最有成就感）→ 模式 4（BLE 滑鼠）→ 模式 2（最複雜，留最後）。
每做一個切片就編譯、燒錄、實機驗證再往下。

## 7. 程式規範與約束
- **非阻塞**：主迴圈與各 task 不可用長 `delay()` 卡住；急停與輸入要隨時有反應。用 `millis()` 排程或 FreeRTOS task。
- **PSRAM 預算**：大緩衝（影像、音訊）放 PSRAM，並在 `exit()` 時釋放；切模式前確認前一模式資源已回收。
- **I2C 位址固定**：CardKB `0x5F`、Scroll `0x40`，寫成具名常數，別散落魔術數字。
- **先寫 2~3 個函式驗證再展開**：例如 RF 先確認能解出一組碼、顯示先確認能畫一張臉，再長成完整模組。
- **每個模組都要能單獨編譯測試**，不要等全部寫完才第一次 build。

## 8. 安全與法律
- RF 與 HID 只用於**自己的設備**：複製自家鐵捲門/插座、控制自己的電腦手機。不對他人設備發射或注入。
- 433MHz 被動接收合法；主動發射僅限自有、低功率。滾動碼（新式車庫/車鑰）不支援也不嘗試。
- 模式 2 的急停是安全關鍵，任何改動都要保持它最高優先、可立即生效。

## 9. 和 Miles 協作的方式
- 動工前先給簡短計畫與要改的檔案，取得同意再寫。
- 不確定的腳位、板子設定、函式庫版本，**先問或先查，不要臆測**。
- 解釋專有名詞；提供可直接編譯/燒錄的完整片段與步驟。
- 每個里程碑結束給一句「怎麼驗收」。
