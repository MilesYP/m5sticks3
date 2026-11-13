# 硬體驗收測試清單（M5StickS3 + RF Pack S3）

目的：板子到貨後，先確認 StickS3 與 RF Pack S3 兩塊硬體沒有出廠故障（DOA, Dead On Arrival），確認無誤再開始正式開發骨架。
原則：全程被動測試，不對外發射 RF。

## 進度總覽
| 階段 | 狀態 | 日期 |
| :-- | :-- | :-- |
| 1 原廠韌體開機 | ✅ 通過 | 2026-11-12 |
| 2 核心自檢（`selftest/`） | ✅ 通過 | 2026-11-12 |
| 3 RF Pack + Bruce 基準 | ❌ nRF24 通過；**CC1101 接收故障**（RSSI 恆定 -50～-55 dBm，拔天線、充飽電、重插皆不變）→ **申請換貨中** | 2026-11-13 |
| 4 自有工具鏈讀寫 CC1101（`acceptance-test/` v5） | ✅ 通過 | 2026-11-12 |

**結論：StickS3 與 RF Pack S3 兩塊板子確認健康，可開始搭韌體骨架（`docs/KICKOFF_PROMPT.md` M0）。**

---

## 前置：Mac 環境
- VS Code + **PlatformIO IDE** 擴充；**USB-C 資料線**
- ESP32-S3 原生 USB，macOS 不需驅動；序列埠 `/dev/cu.usbmodemXXXX`（Web Serial 顯示為 `USB JTAG/serial debug unit`）
- 專案位置：`~/Developer/m5sticks3/`（已移出 iCloud）；GitHub `MilesYP/m5sticks3`

## 階段 1：M5 單機 ✅
- 螢幕正常點亮、兩顆按鍵有反應

## 階段 2：M5 核心自檢（`selftest/`）✅
| 項目 | 結果 |
| :-- | :-- |
| PSRAM | 8.0MB OK（qio_opi 生效；編譯訊息的 "No PSRAM" 只是 devkitc 預設描述） |
| 內部 I2C | `18 68 6E`（ES8311 / BMI270 / M5PM1） |
| Grove I2C | `(none)`（未插配件） |
| IMU / 電池 / 喇叭 / 麥克風 / 按鍵 | 全部正常（電池 3870mV / 71%） |

**按鍵對應（實機確認）**：正面藍色大鍵 = **BtnA = G11**；側鍵 = **BtnB = G12**。（急停與切模式的按鍵配置見 `CLAUDE.md` 第 2 節待決議）
**電源鍵**（另一側）：按一下＝開機／重置；快速按兩下＝關機；長按至綠燈閃＝下載模式。

燒錄踩坑：
- 自動偵測可能誤抓 `/dev/cu.Bluetooth-Incoming-Port`。
- `Device not configured` → **長按電源鍵約 2 秒至綠燈閃 → 放開 → Upload**；燒完短按電源鍵啟動。
- 開機報告只在 setup 印一次，Serial Monitor 晚開看不到（原生 USB CDC 特性）→ 測試程式一律加「按 A 重測／重印」。

## 階段 3：RF Pack + Bruce 基準 ✅
- **Bruce 1.16.1**（https://flash.pingequa.com/devices/sticks3-bruce，勾選 Erase entire flash）
- 天線：433MHz、2.4G 各接對應 SMA 座

| 項目 | 結果 |
| :-- | :-- |
| CC1101（RF → Config → RF Module → CC1101 Legacy） | ✅ 無 "Not Found" |
| CC1101 Spectrum 433.92MHz | ✅ 按遙控器時出現尖峰、不按時無線（對照實驗確認） |
| nRF24 2.4G Spectrum | ✅ 頻譜有活動 |
| Scan/Copy | 測試用遙控器（大門，疑似滾動碼）未解出碼，屬遙控器特性 |

- 附註：該遙控器在 433.92 收訊時有時無，判斷為遙控器本身（電池／頻偏／滾動碼）。
- **2026-11-13 汽車鑰匙複驗**（福特 Kuga，FSK）：Bruce「Spectrum」只顯示 OOK 脈衝，對 FSK 無反應屬正常；「RSSI Spectrum」433.92MHz 有小尖峰，但**不按時也有同樣的小尖峰**（背景雜訊），無法對應按鍵 → **測試不成立，CC1101 收訊仍未確認**。待辦：315／868.35 固定頻率重測、大門遙控器對照、自有韌體 RSSI 數值＋峰值保持定量測試（退換期約 2026-11-26 前）。
- **2026-11-13 追加診斷**：RSSI Spectrum 各頻率（433.92／315／868）皆恆定約 -55 dBm 水平直線（正常背景約 -95～-105 dBm）；**拔天線不變、遠離電腦改電池供電也不變** → 讀值與天線輸入無關，疑似 CC1101 接收路徑故障。待 `acceptance-test` v6（MARCSTATE／PLL／RSSI）以自有韌體確認，排除 Bruce 軟體因素後申請換貨。
- **2026-11-13 上午**：充飽電、重插 RF Pack 後重測，RSSI 仍恆定約 -50 dBm → 排除電量與接觸因素，判定 CC1101 接收故障，向 PINGEQUA 申請換貨。新板到貨需重跑 v5（SPI）＋ v6（RSSI，有／無天線對照）。
- Bruce 操作備忘：A＝選擇、B＝下一個、B 雙擊＝上一個、B 長按＝返回；RSSI Spectrum 內按 A 叫出頻率／範圍選單；勾 Erase flash 重刷後要重新把 RF Module 設回 CC1101。

## 階段 4：自有工具鏈讀寫 CC1101 ✅（`acceptance-test/` v5）
| 項目 | 結果 |
| :-- | :-- |
| SmartRC `Init()` | PASS |
| 身分 | PARTNUM=0x00、VERSION=0x14（**TI 原廠晶片，非相容品**） |
| 寫入→讀回 | 16/16 |
| 穩定度 | 400 次讀取 0 錯誤 |
| nRF24 RF_CH 寫入→讀回→還原 | PASS |

確認的腳位（Bruce `boards/m5stack-sticks3`，已實機驗證）：

| 晶片 | SCK | MISO | MOSI | CS | GDO0/CE |
| :-- | :-- | :-- | :-- | :-- | :-- |
| CC1101 | G5 | G4 | G6 | G2 | G3 |
| nRF24 | G5 | G4 | G6 | G8 | G1 |
| SD（保留，先拉高） | G5 | G4 | G6 | G7 | — |

### 正式韌體必須遵守的工程結論（已併入 `CLAUDE.md` 鐵則五）
1. **RF Pack 不需要 EXT 5V**：EXT 5V 開或關結果完全相同；RF Pack 吃常開的 3.3V。
2. **SPI 匯流排分配**：M5GFX 的 StickS3 螢幕佔用 **SPI3_HOST**；RF 一律用 **SPI2（Arduino `FSPI`）**，不可用 HSPI（= SPI3）。
3. **CC1101 一定要用 SmartRC-CC1101-Driver-Lib 存取**（bmorcelli 分支，鎖 commit `7bba5217`）：
   - 手寫 SPI（`beginTransaction → CS 低 → 傳輸 → CS 高 → endTransaction`，1MHz）在此板會「每隔一次交易出錯」，讀到的 PARTNUM、預設值全是錯的，曾誤判為相容晶片；手寫時序甚至可讓晶片卡死。
   - 函式庫時序：開始＝CS 高 → endTransaction → CS 低 → beginTransaction(2MHz) → 等 MISO 低；結束＝endTransaction → CS 高。
   - 用法：`setSPIinstance(&spi2)` → `setBeginEndLogic(false)` → `setSpiPin(5,4,6,2)` → `setGDO0(3)` → `Init()`（**整個程式只呼叫一次**）。
4. **CC1101 電源不隨 ESP32 重開機而斷電**：晶片卡住時要「拔 USB → 按兩下電源鍵關機 → 等 10 秒」才會真正重置。nRF24 同理（暫存器會保留上一支韌體寫過的值）。
5. 開發工具：雲端環境無法連 PlatformIO 套件伺服器，編譯以 Mac 本機為準。

---

## 刷機策略說明
先用廠商保證可用的 Bruce（已知正常）確認「硬體沒壞」，再換我們的工具鏈確認「我們也能驅動它」，把「板子壞了」與「我們寫錯」兩種問題切開。本次階段四的經驗正好印證此策略：手寫時序的錯誤一度看起來像硬體瑕疵，靠 Bruce 的基準與函式庫對照才確認硬體正常。
