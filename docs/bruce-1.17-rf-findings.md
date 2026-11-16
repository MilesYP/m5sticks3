# Bruce 1.16.1／1.17 的 CC1101 問題對照與自有 RF 韌體避坑清單

- 日期：2026-11-16
- 來源：PINGEQUA 客服（Colin L.）2026-11-16 回信 ＋ 自行比對原始碼
- 比對對象：`BruceDevices/firmware` 標籤 `1.16.1`（ba519c9）與 `1.17`（2b6ba4d，2026-11-12，同 `main`）；驅動 `bmorcelli/SmartRC-CC1101-Driver-Lib` `7bba521`（2025-08-18，Bruce 未鎖版本，1.17 仍用同一份）
- 可信度：以下皆為**讀原始碼的推論**，未在 1.17 實機逐項重現。

## 1. 官方結論
RF Pack S3 硬體正常、非故障品（信中未提換貨，建議改試其他 Bruce 版本）。我們回報的症狀歸因於 Bruce 韌體：

| 症狀 | 官方解釋 |
| :-- | :-- |
| RSSI Spectrum 基線偏高且平坦（約 -50～-55 dBm） | Bruce 1.16.x 重寫 Sub-GHz 堆疊，套用保守的 OOK AGC 設定 `AGCCTRL2=0xC7` |
| 偶發「CC1101 not found」 | ESP-IDF 5.x 下驅動的 `checkMISO()` 被編譯掉，SRES 重置後未等晶振穩定就讀 VERSION，偶爾讀到 0x00 |
| SquareWave Spec 必定「CC1101 not found」 | `rf_SquareWave()` 連續初始化兩次，第二次 SRES 落在 PLL 校正期間 |

## 2. 原始碼對照

| 項目 | 1.16.1 | 1.17 | 判定 |
| :-- | :-- | :-- | :-- |
| `checkMISO()`（驅動 `ELECHOUSE_CC1101_SRC_DRV.cpp:186`） | `#if ESP_IDF_VERSION < 5.0.0` 才有等待迴圈；IDF 5 以上直接 `return true` | 同一份驅動 | **未修** |
| 偵測流程 `initRfModule()` → `Init()` → `getCC1101()`（`rf_utils.cpp:237`） | 重置後立即讀 VERSION，0 即報 not found | 相同，僅多了 M5 Cap CC1101 的電源腳處理 | **未修** |
| `rf_SquareWave()`（`rf_spectrum.cpp`） | `initRfModule("rx")` → `rx.begin()`；`begin()` → `setup_rf_rx()` 內再 `initRfModule("rx")` | 第 158、161 行相同；`begin()` 新增 `radioReady` 參數可跳過第二次初始化，但這裡仍呼叫不帶參數的 `rx.begin()` | **未修** |
| `AGCCTRL2=0xC7`（`rf_utils.cpp:165`） | 有 | 有 | 未變 |
| StickS3 的 CC1101 腳位 | SCK5／MISO4／MOSI6／CS2／GDO0 3 | 相同 | 未變 |

補充：
- 官方信寫 1.16.1 是「沒帶 `radioReady` 旗標」，但該旗標 1.16.1 不存在，是 1.17 才新增（目前只有 `rf_scan.cpp:1220` 用 `rx.begin(true, …)`）。不影響結論。
- 1.17 的 RF 主要變動：新增 FSK 接收設定（`rf_fsk_receive_profile.h`）、協定引擎、ProtoPirate，Scan／Spectrum／Waterfall 畫面重寫。
- 1.17 建置平台為 pioarduino 55.03.39（Arduino 3.3.x，IDF 5.x），所以 `checkMISO()` 在 Bruce 上確實是空的。

## 3. 自有 RF 韌體（模式 3／`rf_transceiver`）避坑清單

1. **`Init()` 整個程式只呼叫一次**（已是鐵則五）。切頻率、切 RX／TX 只改暫存器與下 strobe，不重跑 `Init()`、不重下 SRES。這正是 SquareWave 的錯法。
2. **重置後要等晶片就緒再讀暫存器**。我們目前用 `espressif32@6.12.0`（Arduino 2.0.17，IDF 4.4），`checkMISO()` 的等待迴圈**有編譯進去**，所以沒有這個問題。**若日後升到 Arduino 3.x／pioarduino（IDF 5），該等待會消失**，屆時要自己在 SRES 後等 MISO 轉低或固定延遲，並重跑 v5 驗收。
3. **偵測晶片不要只讀一次 VERSION 就判死**：讀到 0x00 時延遲後重試數次；VERSION 應為 0x14。
4. **AGC 要自己明確寫入，不要沿用 `0xC7`**。注意 SmartRC 的 `Init()`（`RegConfigSettings()`）本身就會寫 `AGCCTRL2/1/0 = C7 00 B2`，與 Bruce 相同；只呼叫 `Init()` 不等於「預設 AGC」。晶片重置後的出廠值是 `03 40 91`。RSSI 掃描與蓋革嗶聲需要 RSSI 隨訊號強弱變化，採用哪一組以 `rssi-test/`（v6）的實測結果為準，並記錄各設定下的背景讀值。
5. **切頻率前先進 IDLE**（SIDLE strobe），改完再進 RX，讓 PLL 重新校正後再讀 RSSI。
6. **判讀 Bruce 現象時先想到韌體**：Bruce 一份程式支援數十種板子，版本間常有相容性退步；Bruce 上的異常不等於硬體故障，以自有韌體的量測為準。

## 4. 自有韌體實測結果（2026-11-17，`rssi-test/` v6.1）
- **CC1101 接收鏈有反應**：掃頻在 315.00 MHz 收到 -16 dBm（Kuga 鑰匙）、333.00 MHz 收到 -18 dBm（鐵捲門遙控器），貼近約 10 公分。
- **但晶片會間歇性不回應 SPI**（2026-11-17）：自有韌體（IDF 4.4，`checkMISO()` 有效）下出現 MISO 持續高電位超過 1 秒的逾時，連續數次後恢復又再發生；Bruce 的重置時序 bug 無法解釋這個現象。原因未明，硬體是否正常**未定**。
- 433.92 MHz／256 kHz 背景：AGC `03 40 91` 為 -111 dBm（接近熱雜訊底）；`C7 00 B2` 為 -96.0 dBm 且不動。`0xC7` 使基線升高變平屬實，但單靠它沒有重現 Bruce 上的 -50～-55。
- 掃 ALL（三段）時底噪顯示約 -55 dBm，只掃 300-348 時約 -101 dBm，兩者穩定時間同為 3 ms → 不是「RSSI 未穩定就讀」。原因未明（可能與跨頻段後驅動 `Calibrate()` 留下的 FSCAL2／TEST0 有關）；Bruce 的 -50～-55 平直線是否同源也未知。
- 先前測不到是因為手邊遙控器在 315／333 MHz，不在 433.92。
- 未確認：天線有／無對照（SMA 與天線路徑）；433.92 MHz 的已知訊號源；`[regs]` 中 FREQ2 讀回值不穩、PKTCTRL0 讀回 3F 的原因。
