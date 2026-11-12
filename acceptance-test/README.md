# 階段四：RF 探針 v5 (Stage 4 RF Probe)

用**我們自己的工具鏈、腳位，以及正式韌體要用的 SmartRC 函式庫**，確認 ESP32-S3 能正確讀寫 RF Pack S3 上的 CC1101 與 nRF24L01+。
**全程不發射**：CC1101 只讀寫設定暫存器；nRF24 的 CE 保持 LOW。

狀態：✅ 2026-11-12 通過（PARTNUM 0x00 / VERSION 0x14、R/W 16/16、400 次讀取 0 錯誤、nRF24 PASS）

## 怎麼跑
1. RF Pack 插好、天線鎖好。若之前跑過其他 RF 程式，先**拔 USB → 按兩下電源鍵關機 → 等 10 秒**再接回（CC1101 不隨 ESP32 重開機斷電）。
2. VS Code「File → Open Folder」→ `~/Developer/m5sticks3/acceptance-test`。
3. ✓ Build → 長按電源鍵到綠燈閃 → 放開 → → Upload → 短按電源鍵啟動。
4. 開 Serial Monitor 後按**正面藍鍵 A** 重測並重印報告。

## 通過標準
| 項目 | 通過 |
| :-- | :-- |
| INIT | SmartRC `Init()` 成功 |
| ID | `PN00 VER14`（或 VER04） |
| R/W | 16/16 |
| STRESS | 0 err / 400 |
| nRF24 | 寫入值 = 讀回值，還原成功 |

## 版本歷程（為什麼是 v5）
- v1~v4 用手寫 SPI 讀寫 CC1101，時序在這塊板子上會「每隔一次交易出錯」，甚至讓晶片卡住，結果無法判斷硬體好壞。
- v5 改用 Bruce 同款的 SmartRC-CC1101-Driver-Lib（bmorcelli 分支，鎖 commit `7bba5217`），一次全數通過。
- 詳細工程結論見專案文件 `claude/hardware-acceptance-test.md`。
