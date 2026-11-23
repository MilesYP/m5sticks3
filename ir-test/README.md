# StickS3 IR1 收碼驗證

獨立 PlatformIO 專案。用本體頂端的紅外線接收器驗證 G42、RMT 硬體與 NEC 解碼。螢幕和序列埠顯示標準 NEC 或延伸 NEC 的位址、指令與原始碼值。

## 建置

📋 ：cd ~/Developer/m5sticks3/ir-test && pio run

板卡與函式庫版本沿用 `selftest/`。畫面連結正式韌體的 `ui_kit` 與 `ui_fx`，收碼使用 Arduino 2.0.17 隨附的 ESP-IDF 4.4 RMT 驅動。

## 切換共用測試板

目前若執行正式韌體，先在序列埠送以下指令，看到 `[pwrkey] → root` 且有「看門狗關」後再關閉序列埠監看。可用長按電源鍵 5 秒後放開代替，看到紅底「ROOT 模式」。

📋 ：q0

確認可覆寫板上的正式韌體後燒錄，燒完開啟序列埠。若燒錄後畫面仍停在下載模式，短按電源鍵重置。

📋 ：cd ~/Developer/m5sticks3/ir-test && pio run -t upload -t monitor

## 驗收

1. 開機應顯示「紅外線」與「等待訊號」。按正面 A 或在序列埠送 `?` 可重印狀態，看到 `[ir1] READY RX=G42 RMT=4 EXT_5V=ON` 代表供電與驅動已初始化。
2. 用自家電視或電風扇的紅外線遙控器對準 StickS3 頂端，保持大於 30 公分，短按兩顆不同的鍵。遙控器使用 NEC 時，螢幕顯示 `NEC` 或 `NEC EXT`，序列埠印出 `[ir1] NEC ADDR=… CMD=… RAW=…` 或延伸版。再按相同鍵，核對碼值一致。
3. 長按支援 NEC 重複碼的遙控器按鍵，再按 A 重印。可從 `count=`、`repeat=`、`unknown=` 分別看完整碼、重複碼和未辨識訊號。收到未知訊號表示 RMT 有捕捉到脈衝，需再核對遙控器協定。
4. 長按 B 滿 3 秒停止接收，螢幕顯示「已停止接收」，序列埠印出 `[ir1] STOPPED`。短按電源鍵重新開始。

IR1 的驗收以實際遙控器送出的穩定碼值為準。G46 發射驗證排在 IR3。

## 接收診斷

按 A 或送 `?` 時，`[ir1] DIAG` 同時回報：

- `ext_mv`：M5PM1 的 5V 輸出電壓讀值，單位為毫伏。
- `amp_latch`：喇叭功放控制的輸出設定，`0` 為低電位，`?` 為讀取失敗。
- `gpio42` 與 `edges`：G42 當下電位及開機後偵測到的總跳變次數。兩次取樣的差值可用來對照按鍵或遮光前後。`probe=ESP_OK` 代表跳變計數已掛上。

同次查詢的 `[ir1] PYG3` 以唯讀方式印出 M5PM1（`0x6E`）暫存器原值。依 M5PM1 資料表，`FUNC0`（`0x16`）的 bits 7:6 為功能欄位，`MODE`（`0x10`）、`DRV`（`0x13`）、`OUT`（`0x11`）則各看 bit 3。`amp_off_config=1` 表示讀回一般 GPIO、推挽輸出、低電位，符合官方關閉功放設定。這是暫存器讀值，功放腳位電位需以儀器驗證。

跳變計數只用來判斷腳位有無活動，完整訊號與 NEC 解碼仍由 RMT 路徑判斷。官方電路圖標示接收器為 38 kHz 的 VSOP38338。手機相機可確認遙控器有發光，載波頻率需要另外量測。 [M5Stack 電路圖](https://docs.m5stack.com/en/core/StickS3#schematics) · [Vishay 接收器規格](https://www.vishay.com/en/ir-receiver-modules/vsop/)

在接收中送小寫 `b` 可做背光對照。板子在原亮度、背光關閉和恢復原亮度各量 10 秒，每段印出 `[ir1] BL step=…`、實測毫秒數、跳變及收碼增量。只切換背光，LCD 與 RMT 持續運作。側鍵 B 按滿 3 秒會停止接收和本輪量測，並恢復原亮度。

### USB → 電池 → USB 供電對照

1. USB 接在電腦、EXT_5V 開啟時送小寫 `p`。保持位置、照明、背光與配件固定，期間先不按遙控器。記下 `[ir1] PWR boot=…`。等 `completed=1` 才拔線。
2. 拔掉 USB，等至少 20 秒，再插回同一個 USB 埠。板子用 M5PM1 的 5VIN 狀態區分供電階段，每次切換等 1 秒穩定，再取樣至少 10 秒。兩種情況的紅外線接收器都由板載 5V 升壓供電。
3. 插回後送小寫 `r` 讀結果，等 `status=DONE completed=3`。每段包含時間、跳變、完整碼／重複碼／未知碼增量，以及板載 5V ADC 的平均、最低、最高值和採樣數。跳變率為 `edges × 1000 ÷ ms`。任何階段超過 180 秒、接收停止或背光改變會使該輪失效。
4. 三段結果存於 RAM，重啟會丟失。若 `boot` 改變、回報 `NONE` 或 `INVALID_…`，整輪重做。

## 回到正式韌體

關閉序列埠監看，燒回正式韌體。開機會自動恢復正式韌體電源鍵與看門狗設定。

📋 ：cd ~/Developer/m5sticks3/firmware && pio run -t upload -t monitor

## 硬體依據

- G42 接收，開 `EXT_5V`。初始化用 M5Unified 的 `M5.Speaker.end()` 關閉喇叭功放。[M5Stack 腳位與供電](https://docs.m5stack.com/en/core/StickS3) · [M5Stack 紅外線接收範例](https://docs.m5stack.com/en/arduino/m5sticks3/ir_nec)
- ESP32-S3 選 RMT 接收通道 4，環形緩衝區交給主迴圈以零等待讀取。[ESP-IDF 4.4.7 RMT 文件](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/peripherals/rmt.html)
