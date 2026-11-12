# M5StickS3 極客口袋終端與實體外掛架構設計規格書 (PRD / Implementation Spec)

本文件整理 M5StickS3 搭配現有模組（RF Pack S3、Scroll Unit、CardKB）之系統架構、硬體介面限制、四大特化模式與具體實作指引，可直接作為 Claude Code / Codex / Cursor 的提示詞與架構藍圖。

---

## 1. 硬體清單與物理拓撲 (Hardware Topology)

### 1.1 現有硬體資源
* **核心大腦**：M5StickS3 (ESP32-S3-PICO-1-N8R8, 8MB Flash, 8MB PSRAM, 1.14" 135x240 LCD, BMI270 6軸 IMU, ES8311 音訊解碼 + MEMS 麥克風 + 揚聲器, 原生 USB-C OTG)
* **射頻擴充**：RF Pack S3 (433MHz / 2.4GHz 雙頻收發，走頂部專屬排針 / SPI)
* **輸入周邊**：
  * **CardKB**：全鍵盤 (I2C 位址 `0x5F`)
  * **Scroll Unit**：編碼器滾輪與實體點擊鍵 (I2C 位址 `0x40`)

### 1.2 單一 Grove 孔位策略 (Single Grove Port Policy)
由於本體僅具備 1 個 Grove 介面且無分接線，系統採取「**單插拔、場景專用**」策略：
* **桌上 AI 伴侶模式**：底部固定插 **CardKB**（作為靜音文字問答補充）。
* **體感滑鼠 / RF 選單模式**：底部固定插 **Scroll Unit**（作為實體滾輪操作手把）。
* **隨身攜帶 / 鐵捲門鑰匙**：底部 **空接**，僅裝頂部 RF Pack S3，維持口袋極致便攜性。

---

## 2. 系統架構與四大核心模式 (Core Operating Modes)

---

### 模式 1：桌面 AI 語音伴侶 (Desktop Grok Companion)

* **核心定位**：桌上常駐的對話小助理，具備情感反饋，以視覺表情為主、文字為輔。
* **螢幕 UI 佈局 (135 × 240)**：
  * **頂部 75% 區域（動態表情）**：
    * 風格：Grok 極簡幾何幾何線條（黑底高對比，支援白色/青藍切換）。
    * 待機 (Idle)：雙橫線/平行圓角矩形，呼吸式浮動，隨機眨眼 (Blink)。
    * 聆聽 (Listening)：眼睛縮小，下方出現聲音收音漣漪波形。
    * 思考 (Thinking)：幾何線條旋轉 / 方形漸變動態。
    * 說話 (Speaking)：振幅與 ES8311 PCM 音量即時連動，呈現波動嘴型。
  * **底部 25% 區域（跑馬燈文字）**：
    * 顯示 1~2 行微型像素字 (Pixel Font)。
    * 僅顯示當前提問與回覆的文字摘要，播放完畢後自動淡出，保留乾淨表情。
* **輸入方式**：
  * **語音優先**：內建 MEMS 麥克風收音，經 Wi-Fi 送至後端 Whisper / STT 串流，LLM 處理後由 TTS 回傳音訊經揚聲器播放。
  * **CardKB 靜音輸入**：不方便出聲時，以 CardKB 鍵入字元，直接發送文字至 LLM。

---

### 模式 2：跨裝置實體 AI 外掛 (Universal AI HID Controller)

* **核心原理**：基於 `esp32-iphone-agent` 思維，將 S3 作為免越獄、免驅動的 **USB 複合鍵鼠 (Composite HID)**，熱插拔於手機或電腦。
* **目標裝置自適應**：
  * **iOS 模式**：
    * 需求：iPhone 開啟「輔助觸控 (AssistiveTouch)」。
    * 運作：S3 模擬滑鼠座標移動、觸控滑動、文字輸入。視覺大模型透過 AirPlay 或專用 App 截圖定位座標後，發送 HID 動作指令給 S3。
  * **PC / macOS 模式**：
    * 需求：免額外設定，即插即認。
    * 運作：電腦端背景服務抓取螢幕畫面送給 VLM，回傳座標與指令。S3 可注入全域快捷鍵（`Cmd+Space`、`Ctrl+C`、文字 Macro 展開）與高精度滑鼠控制。
* **安全機制 (Physical Kill Switch)**：
  * 當 AI 操作發生幻覺或誤操作時，**拍下 S3 正面實體按鈕**，板子立即中斷發送所有 HID 封包並斷開連線。

---

### 模式 3：隨身掌上射頻工具 (RF Pocket Tool)

* **核心硬體**：RF Pack S3 (433MHz / 2.4GHz)。
* **功能定義**：
  * **鐵捲門 / 遙控開關學習與複製**：
    * 支援固定碼 (PT2262 / SC2260 等) 接收解碼。
    * 將波形長度、Protocol 與 Raw Code 寫入 Flash / NVS。
    * 提供一鍵發射開門 / 關門功能。
  * **RF 訊號強度雷達 (RSSI Scanner)**：
    * 讀取當前頻段 RSSI 強度，螢幕以柱狀圖或瀑布圖即時顯示。
    * 靠近發射源時，S3 揚聲器發出蓋革計數器式的間歇嗶聲。
* **操控方式**：插上 Scroll Unit 進行頻率切換與模式選擇。

---

### 模式 4：空中體感滑鼠 (Air Mouse)

* **核心硬體**：內建 BMI270 (6軸 IMU) + BLE HID。
* **運作機制**：
  * S3 啟動 BLE 廣播，電腦或平板直接配對為藍牙滑鼠。
  * 姿態解算（俯仰角 Pitch 與偏航角 Yaw）直接映射為螢幕游標移動。
  * **操作映射**：
    * S3 正面大按鍵：滑鼠左鍵。
    * Scroll Unit 滾輪：網頁 / 簡報垂直滾動。
    * Scroll Unit 按壓：滑鼠右鍵。

---

## 3. 資源排程與穩定性設計 (Resource & RTOS Management)

為避免 8MB PSRAM 耗盡或中斷衝突，系統必須遵守**互斥狀態機 (Mutually Exclusive State Machine)**：

| 模式 | 啟用資源 | 關閉 / 休眠資源 |
| :--- | :--- | :--- |
| **桌面語音 AI** | Wi-Fi, ES8311 Codec, CardKB (I2C) | RF Pack (SPI), BLE, USB HID |
| **跨裝置 AI 外掛** | USB OTG (HID), Wi-Fi (收指令) | ES8311, RF Pack, BLE |
| **掌上 RF 工具** | RF Pack (SPI), Scroll Unit (I2C) | Wi-Fi, BLE, ES8311, USB HID |
| **空中體感滑鼠** | BLE HID, BMI270 (IMU), Scroll Unit | Wi-Fi, ES8311, RF Pack |

---

## 4. 給 Codex / Claude Code 的實作提示詞指引

在實作時，請將專案拆分為以下模組：
1. `hal_power.cpp`：管理 Wi-Fi / BLE / 音訊晶片在切換模式時的低功耗休眠。
2. `ui_grok.cpp`：利用 M5GFX 向量繪製極簡幾何表情，監聽音訊震幅動態變形。
3. `hid_injector.cpp`：基於 TinyUSB 封裝 iOS / macOS / Windows 跨平台鍵鼠指令與實體中斷鍵邏輯。
4. `rf_transceiver.cpp`：封裝 RCSwitch / CC1101 協定，負責 433MHz 學習、儲存與發射。
5. `air_mouse.cpp`：讀取 BMI270 原始角速度，經過低通濾波後送出 BLE Mouse 封包。
"""
