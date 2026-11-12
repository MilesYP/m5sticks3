# m5sticks3 — M5StickS3 極客口袋終端

把一支 **M5StickS3（ESP32-S3）** 加上 **RF Pack S3**，做成可切換四種角色的口袋終端：

1. **桌面 AI 語音伴侶**：幾何表情動畫 + 語音對話（麥克風 → 後端 STT/LLM/TTS → 喇叭）
2. **跨裝置 USB HID 外掛**：S3 當 USB 鍵鼠，搭配視覺模型操作自己的電腦（含實體急停）
3. **掌上 433MHz 射頻工具**：固定碼學習／重播（僅限自有設備）、RSSI 頻譜
4. **空中體感滑鼠**：BMI270 姿態 → BLE 滑鼠

## 目錄結構

| 路徑 | 內容 |
| :-- | :-- |
| `CLAUDE.md` | 工程約定與鐵則（給 Claude Code 的常駐上下文） |
| `docs/` | PRD、養成計畫、啟動 prompt |
| `selftest/` | 階段二：StickS3 本體硬體自檢（PlatformIO 專案） |
| `acceptance-test/` | 階段四：RF Pack 的 CC1101 / nRF24 SPI 驗收（PlatformIO 專案） |
| `firmware/` | 正式韌體（尚未建立） |

每個子資料夾都是獨立的 PlatformIO 專案：VS Code「Open Folder」開該子資料夾，再 Build / Upload。

## 進度

- ✅ 2026-11-12 硬體驗收四階段全數通過（StickS3 本體、RF Pack S3 的 CC1101 與 nRF24）
- ⏭ 下一步：正式韌體骨架（互斥狀態機、輸入偵測、實體急停）

## 硬體與腳位重點

- RF Pack S3：CC1101（433MHz）+ nRF24L01+（2.4GHz），共用 SPI：SCK=G5 / MISO=G4 / MOSI=G6；CC1101 CS=G2、GDO0=G3；nRF24 CS=G8、CE=G1
- 螢幕佔用 SPI3，RF 一律走 SPI2（FSPI）
- CC1101 透過 [SmartRC-CC1101-Driver-Lib](https://github.com/bmorcelli/SmartRC-CC1101-Driver-Lib) 存取（手寫 SPI 時序在此板不穩定）
- 腳位參考 [Bruce](https://github.com/pr3y/Bruce) 的 `boards/m5stack-sticks3`

## 安全與法律

射頻與 HID 功能只用於**自己的設備**。不支援也不嘗試滾動碼，不提供任何干擾（jamming）功能。主動發射僅限自有設備、低功率，請遵守當地無線電法規。
