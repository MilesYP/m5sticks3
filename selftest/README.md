# M5StickS3 硬體自檢 (Stage 2 Self-Test)

板子到貨後，用這支程式一次確認 StickS3 核心硬體沒有出廠故障(DOA)。
全程被動，不涉及 RF、不發射訊號。RF Pack 的驗收是後面的階段。

## 怎麼跑

1. **先把資料夾複製到本機（非 iCloud）路徑再開**，例如 `~/Projects/sticks3-selftest`。
   原因：iCloud 會一直同步編譯產生的上千個暫存檔(.pio/)，拖慢甚至干擾燒錄。
   ```bash
   cp -R "~/Library/Mobile Documents/com~apple~CloudDocs/M5StickS3+RF Pack S3/selftest" ~/Projects/sticks3-selftest
   ```
2. VS Code 開這個資料夾（確認已裝 PlatformIO IDE 擴充）。
3. 用 **USB-C 資料線**接上 StickS3。
4. 按 VS Code 最下面狀態列的 **→（Upload）**。第一次會下載 ESP32 工具鏈，要等幾分鐘。
5. 燒錄完，按狀態列的 **插頭圖示（Serial Monitor）** 看開機自檢報告。

> 若第一次燒錄卡在連線：按住側鍵→插 USB→放開，進入下載模式再按一次 Upload。

## 通過標準

| 項目 | 螢幕/序列埠應顯示 |
| :-- | :-- |
| PSRAM | `PSRAM 8.0MB OK`（約 8MB，綠字） |
| I2C 內部核心 | `I2C core OK`，`in :18 68 6E`（ES8311/BMI270/M5PM1 到齊） |
| I2C Grove | 插 CardKB 顯示 `5F`、插 Scroll 顯示 `40`；空接顯示 `(none)`，正常 |
| IMU | `IMU OK`，加速度值會隨你轉動板子變化 |
| 電池 | `Batt xxxx mV`，有合理數字 |
| 喇叭 | 開機嗶一聲；按 **A 鍵**再嗶一聲 |
| 麥克風 | `Mic peak` 有數字；按 **B 鍵**對它說話，數字應跳動 |
| 按鍵 | 按 A/B，螢幕對應的 `[ ]` 變 `[#]` |

全部符合 → StickS3 本體健康，可進入下一階段（插 RF Pack，刷 Bruce 當基準）。

## 備註
- 按鍵標示 A=G11、B=G12。哪顆是正面大鍵、哪顆是側鍵，按下去看螢幕反應即可確認。
- 函式庫版本用 git tag 鎖在 M5Unified / M5GFX 0.3.0，確保日後重編結果一致。
- 這支會 `delay` 阻塞（嗶聲、錄音），是測試碼可接受；正式韌體會改非阻塞。
