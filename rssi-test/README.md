# CC1101 接收驗收 v6（RSSI 測試）

用自有韌體確認 RF Pack S3 上 CC1101 的**接收路徑**是否正常，並檢驗 PINGEQUA 的說法：Bruce 上「RSSI 恆定約 -50 dBm」是 `AGCCTRL2=0xC7` 造成的。
**全程只接收，不發射。** v5（`acceptance-test/`，SPI 讀寫）保持原樣。

狀態：✅ 2026-11-17 實測（v6.1）：接收正常，SCAN 在 315.00 MHz 收到 -16 dBm、333.00 MHz 收到 -18 dBm；433.92 背景 -111 dBm（設定 1）／-96 dBm（設定 2）。天線有／無對照尚未做。

## 畫面
| 位置 | 內容 |
| :-- | :-- |
| 第 2 行 | 目前的 AGC 設定名稱 |
| 第 3 行 | AGCCTRL2/1/0 的值、EXT 5V 開關 |
| 第 4 行 | `MARC`（應為 `0D`＝接收中）、`FSCAL1`（`3F`＝鎖相失敗）；綠＝正常、紅＝異常 |
| 第 5 行 | `VER`（應為 `14`）、`RXdrop`（掉出接收狀態的次數，應為 0） |
| 大字 | 即時 RSSI（dBm） |
| 5s | 最近 5 秒 min／avg／max |
| 圖 | 最近 13.5 秒歷史，虛線為 -50 與 -90 dBm |
| 下方 | 擷取倒數，或最近一次 10 秒擷取結果 |

## 按鍵
- **A 短按**：開始 10 秒擷取，結束後畫面與序列埠印出 min／avg／max。
- **B 短按**：切換 AGC 設定（1 → 2 → 3 → 1）。
- **B 按住 1 秒再放**：切換 EXT 5V（選做）。

AGC 設定：`1 CHIP DEFAULT`＝03 40 91（晶片出廠值）；`2 LIB 0xC7`＝C7 00 B2（SmartRC `Init()` 與 Bruce 一般模式）；`3 BRUCE OOK C7`＝C7 01 40（Bruce 固定頻率 OOK）。

## 怎麼跑
1. RF Pack 插好、433 天線鎖好。**拔 USB → 按兩下電源鍵關機 → 等 10 秒**再接回（CC1101 不隨 ESP32 重開機斷電）。
2. VS Code「File → Open Folder」→ `~/Developer/m5sticks3/rssi-test`。
3. ✓ Build → 長按電源鍵到綠燈閃 → 放開 → → Upload → 短按電源鍵啟動。
4. 開 Serial Monitor（每秒印一行狀態，每次擷取結束印結果表）。
5. 確認 `MARC 0D`、`FSCAL1` 不是 `3F`、`VER 14`、`RXdrop 0`。
6. 設定 1 與設定 2 各做三個情境，每次按 A 後照做、等 10 秒：

| 情境 | 做法 |
| :-- | :-- |
| (a) 有天線、安靜 | 不按任何遙控器 |
| (b) 有天線、按遙控器 | 距離約 1 公尺，按住遙控器直到擷取結束（433.92 MHz 的遙控器或車鑰匙皆可） |
| (c) 拔天線、按遙控器 | 同樣 1 公尺、同樣按法 |

## 判定
| 項目 | 正常 | 異常 |
| :-- | :-- | :-- |
| MARC | `0D` | 其他 |
| FSCAL1 | 不是 `3F` | `3F` |
| 設定 1 (a) 的 avg | 約 -85 dBm 以下 | 停在 -50 上下 |
| 設定 1 (b) 的 max 相對 (a) 的 avg | 高 20 dB 以上 | 幾乎不變 |
| 設定 1 (c) 的 max 相對 (b) 的 max | 低 10 dB 以上 | 相同 |
| 設定 2 (a) 相對設定 1 (a) | 基線升高、變平（即官方的解釋成立） | — |

門檻為經驗值，非規格；落在邊界時看整體數字判斷。把序列埠最後一張結果表貼回來即可。

## v6.1 補充（2026-11-17）
首次實測（v6）：設定 1 背景 -111 dBm、設定 2 背景 -96 dBm；遙控器貼近 10 公分時最大值只到 -104 dBm。背景值合理，但對遙控器幾乎沒反應，無法分辨是遙控器不在 433.92 MHz 還是接收前端有問題，因此加入掃描模式。

- **A 按住 1 秒再放**：切換 MONITOR／SCAN。A 短按改為「放開時」才開始擷取。
- **SCAN 模式**：頻寬 812 kHz、每 0.75 MHz 一點，掃 300–348／387–464／779–928 MHz，每點保留最大值。A 短按清除、B 短按切頻段（ALL → 300-348 → 387-464 → 779-928）。每掃完一輪序列埠印一行 `[scan #n]` 與前三高的頻率。
- 第 5 行改為 `VER`／`drop`（確認後仍不在接收的次數）／`bad`（單次讀值不一致的次數）分開上色。
- 每 10 秒印一次 `[regs]`，含手動校正後的 FSCAL3/2/1。

---

## For PINGEQUA / Bruce maintainers — how to reproduce (English)

Receive-only test firmware for the CC1101 on an RF Pack S3 + M5StickS3. It never transmits.

**Build**: PlatformIO, `espressif32@6.12.0` (Arduino 2.0.17 / ESP-IDF 4.4). Everything is pinned in `platformio.ini`, including `bmorcelli/SmartRC-CC1101-Driver-Lib` at commit `7bba5217`. Open this folder, Build, Upload. Serial monitor at 115200.

Pins (from Bruce `boards/m5stack-sticks3`, verified on hardware): shared SPI2 `SCK 5 / MISO 4 / MOSI 6`, CC1101 `CS 2 / GDO0 3`, nRF24 `CS 8 / CE 1` (held idle), SD `CS 7` (held high). `Init()` is called exactly once.

**Screen, line by line**: AGC profile name; AGCCTRL2/1/0 values; `MARC` (MARCSTATE, `0D` = RX) and `FSCAL1`; `VER` / `d` (confirmed RX drops) / `b` (MARCSTATE single-read mismatches) / `HANG` (SPI operations over 500 ms); live RSSI; 5-second min/avg/max; a 13.5-second history plot; the last 10-second capture.

**Buttons**
- `A` short: start a 10-second RSSI capture; min/avg/max printed to serial at the end.
- `A` hold 1 s: switch between MONITOR (fixed 433.92 MHz, 256 kHz) and SCAN (812 kHz, 0.75 MHz steps over 300-348 / 387-464 / 779-928 MHz, max-hold, top 3 peaks printed per sweep).
- `B` short in MONITOR: cycle AGC profile — `03 40 91` (chip reset default), `C7 00 B2` (what SmartRC `Init()` writes, and Bruce's general mode), `C7 01 40` (Bruce's fixed-frequency OOK preset).
- `B` short in SCAN: cycle band. `A` short in SCAN: clear max-hold.
- `B` hold 1 s in MONITOR: run the SPI reliability test (about 15 s, auto-aborts).

**The SPI reliability test** reads `VERSION` (0x31, read-only, must be `0x14`) and `SYNC1` (0x04, reset default `0xD3`) in both IDLE and RX: 24 raw values printed verbatim, then an error count per inter-read gap (0 / 50 / 200 / 500 / 1000 / 2000 us, 50 reads each), then a "read until two consecutive agree" pass, then a verified-write test. It aborts a row after 3 timeouts and the whole test after 10 timeouts or 60 s, so it cannot hang for long.

**To reproduce the lockup**: run the test once (completes), then press and hold `B` again immediately, or press it twice within about two seconds. With no gap between runs the chip stops responding to SPI entirely — `checkMISO()` times out on every transaction and only a board reset clears it. With a 2-second gap both runs complete.

Our measurements and the engineering conclusions are in `../docs/cc1101-spi-reliability.md` (Traditional Chinese; the numbers and register values are readable as-is).
