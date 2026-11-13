# 專案進度紀錄（PROGRESS）

> **用途**：這是各個 Session（Claude Code CLI、claude.ai／Cowork…）之間共享進度的唯一地方。各 Session 的記憶不互通，但都讀得到這份檔案。
> **規則**：
> - 新紀錄加在「紀錄」區的**最上方**（最新的在最前面）。
> - 開始工作前先讀最上面 2～3 筆；結束時新增一筆，再 `git commit` + `git push`。
> - 工程規則與設計決議寫在 `CLAUDE.md`；詳細測試數據寫在對應文件（如 `docs/hardware-acceptance-test.md`）。這裡只記摘要與連結。
>
> **每筆紀錄格式**：
> ```
> ### YYYY-MM-DD｜里程碑名稱｜Session 類型（Claude Code CLI / claude.ai / Cowork）
> - 做了什麼：
> - 驗收結果：
> - 遇到的坑／新規則：（新規則要同步寫進 CLAUDE.md）
> - 下一步：
> - 待辦／未決：
> ```

## 目前狀態（每次更新時順手改這一段）
- **階段**：M0 韌體骨架程式完成、編譯通過 → **待實機驗收**
- **進行中**：M0 實機驗收（燒錄 `firmware/`，照下方 2026-11-13 紀錄的驗收步驟）
- **未決**：無（按鍵配置已決議，見 `CLAUDE.md` 第 2 節）
- **⚠ 硬體狀態（給所有 Session）**：RF Pack S3 的 **CC1101 接收故障，已決定向 PINGEQUA 申請換貨**（信已備妥，退換期約 2026-11-26 前）。nRF24、StickS3 本體正常。**新 RF Pack 到貨並通過驗收前，不做依賴 CC1101 收發的開發（M3）**；M0、M1（表情／音訊／Wi-Fi）、M4（BLE 滑鼠）不受影響，照常進行。
- **待辦**：
  - 換貨：寄信 → 等賣家回覆／RMA → 寄回 → 收新板（進度由 claude.ai Session 追蹤，結果寫回本檔）
  - 新板到貨驗收：重跑 `acceptance-test` v5（SPI）＋ 新增 v6 RSSI 測試（433.92 MHz 進 RX，讀 MARCSTATE／FSCAL1／RSSI dBm＋峰值保持；有天線、拔天線、按遙控器三種情境，正常背景應約 -95～-105 dBm 且拔天線會變）
  - 插 CardKB／Scroll 確認 Grove 偵測

---

## 紀錄

### 2026-11-13｜CC1101 收訊複驗（汽車鑰匙）｜claude.ai
- 做了什麼：刷回 Bruce 1.16.1（勾 Erase flash 後 RF Module 會重置成 `M5 RF433T/R`，要改回 `CC1101`）；用福特 Kuga 鑰匙（滾動碼、FSK 調變）只做接收測試。
- 驗收結果：Bruce「Spectrum」無反應——查原始碼（`rf_spectrum.cpp`）確認它只畫 OOK 開關脈衝，FSK 鑰匙本來就看不到。改用「RSSI Spectrum」固定 433.92MHz：有小尖峰，**但不按鑰匙時也會出現同樣大小的小尖峰**（背景雜訊），無法和按鍵對應 → **鑰匙測試不成立**。範圍掃描（300-348／387-464／779-928／All）也無明顯反應（掃頻輪詢可能錯過短脈衝）。
- 判定：**CC1101 收訊功能仍未確認**。目前唯一正面證據是 11-12 公司大門遙控器（OOK，433.92）在 Spectrum 上時有時無、不按時無線。SPI 讀寫（v5）已確認正常，問題範圍縮小在 RF 前端（天線、匹配、靈敏度）或測試訊號源（鑰匙頻率不是 433.92、電池弱）。
- 學到（Bruce 操作）：A＝選擇、B＝下一個、B 雙擊＝上一個、B 長按＝返回；頻率／範圍選單要在 RSSI Spectrum 畫面內按 A 叫出（Config → RF Frequency 只是輸入預設頻率的鍵盤）。
- **追加診斷（同晚）**：RSSI Spectrum 在 433.92／315／868 每個頻率都是「開頭一下 -20（啟動假訊號）→ 之後恆定約 -55 dBm 的水平直線」。正常背景應約 -95～-105 dBm。**拔掉天線讀值不變；拔 USB、遠離電腦與電子產品讀值仍不變** → 讀值與天線輸入無關，排除外部干擾。
- **再追加**：切換 Bruce 頁面（例如進 RF → Scan/Copy）時會間歇跳出紅底「CC1101 not found」，有時閃一下就進去、有時卡很久。當時電池僅約 12%。賣家疑難排解寫明：模組無回應／卡住 → 確認側邊電源開關、重插確保排針沒歪、**電池保持 30% 以上**。→ 在宣判硬體故障前，先排除**供電不足與排針接觸**（充電 >50%、重插並鎖螺絲、完整斷電重開）再重測。
- 補充症狀：RF → **SquareWave Spec 每次必定**跳出「CC1101 not found」並退回主選單（Bruce 在 `initRfModule()` 呼叫 `getCC1101()` 讀 VERSION 得到 0 時顯示此錯誤，代表當下 SPI 讀不到晶片回應）。
- **11-13 上午複測**：電池充飽後，RSSI Spectrum 433.92 仍恆定約 -50 dBm → **排除電量不足**，維持換貨判斷。
- **11-13 上午**：已重插 RF Pack 並重測，仍恆定約 -50 dBm → 排除接觸不良。**結論：CC1101 接收故障，維持換貨。**
- **決定**：為趕在退換期內，先以「CC1101 接收故障」向 PINGEQUA（support@pingequa.com）**申請換貨**（英文信已備，附 RSSI 有／無天線照片與影片）。若充電＋重插後問題消失，再回信取消。
- 判定：**CC1101 接收路徑疑似故障**（RF 前端、匹配電路或 PLL 未鎖定），但尚未排除「Bruce 在 StickS3 上的 RSSI 讀取有問題」這個軟體可能。
- 下一步：用自有韌體（SmartRC，已驗證 SPI 正常）做 `acceptance-test` v6：設 433.92MHz 進 RX → 讀 MARCSTATE（應為 0x0D=RX）、FSCAL1（0x3F 代表 PLL 未鎖定）、RSSI dBm 數值＋峰值保持，有天線／無天線／按鑰匙三種情境對照。若仍「恆定且與天線無關」→ 附數據向 PINGEQUA 申請換貨（退換期約 2026-11-26 前）。

### 2026-11-13｜M0 韌體骨架｜Claude Code CLI
- 做了什麼：按鍵配置決議（方案 1：A＝做事／模式 2 急停，B＝切模式／1～3 秒重印／3 秒急停備援，電源鍵不用），寫回 `CLAUDE.md` 第 2 節與鐵則三。新建 `firmware/`：`config.h`、`input_hal`（Grove 掃描＋按鍵事件）、`killswitch`（G11 中斷＋最高優先 task 關輸出閘門，兩段式）、`mode_manager`（四模式＋Safe，先 exit 再 enter）、`ui_status`（狀態畫面＋序列埠報告）、`main.cpp`。
- 驗收結果：Mac 上 `pio run` 逐步編譯皆 SUCCESS（RAM 7.3%、Flash 16.6%）。**尚未燒錄實測。**
- 驗收步驟：(1) 開機顯示 Desktop＋輸入裝置；(2) B 短按循環四模式，序列埠印 exit→enter；(3) 插 CardKB／Scroll 開機能辨識；(4) HidAgent 按 A → 紅底 KILLED，B 短按恢復；AirMouse 按 A 不急停、B 按住 3 秒急停；(5) A／B 交錯連打 20 次不當機。
- 遇到的坑／新規則：M5Unified 預設不 `begin()` `Ex_I2C`，selftest 的 Grove `(none)` 結果可信度待確認 → 韌體明確 `begin()`（已寫入 `CLAUDE.md` 鐵則一）。
- 下一步：實機驗收 M0 → M1-A 模式 1 離線表情。

### 2026-11-12｜CLAUDE.md 更新與跨 Session 進度機制｜claude.ai
- 做了什麼：`CLAUDE.md` 修正文件路徑、新增按鍵表、已驗證腳位表、鐵則五（RF Pack 驅動規則）、實測版本；`docs/KICKOFF_PROMPT.md` 改寫為 M0 骨架 prompt；新增本檔，並在 `CLAUDE.md` 第 9 節規定各 Session 結束時要更新本檔。
- 下一步：在 Mac 的 VS Code 終端機啟動 Claude Code CLI，以 plan mode 貼上 M0 prompt。
- 待辦／未決：正面鍵 A 職責衝突待決議。

### 2026-11-12｜Git／GitHub 建立｜claude.ai
- 做了什麼：專案移至 `~/Developer/m5sticks3`（移出 iCloud）；建立根目錄 `.gitignore`、`README.md`；推上公開 repo `MilesYP/m5sticks3`。
- 遇到的坑：`.pio/` 動輒數百 MB 必須忽略；賣家網頁離線存檔（PINGEQUA）有著作權，不上傳。

### 2026-11-12｜硬體驗收四階段｜claude.ai
- 做了什麼：`selftest/`（StickS3 本體自檢）、Bruce 1.16.1 基準（RF Pack）、`acceptance-test/` v1→v5（自有工具鏈讀寫 CC1101／nRF24）。
- 驗收結果：全數通過。CC1101 為 TI 原廠（PARTNUM 0x00／VERSION 0x14），寫入→讀回 16/16、400 次讀取 0 錯誤；nRF24 PASS。詳見 `docs/hardware-acceptance-test.md`。
- 遇到的坑／新規則（已寫入 `CLAUDE.md` 鐵則五）：手寫 CC1101 SPI 時序會每隔一次交易出錯 → 一律用 SmartRC 函式庫；RF 走 SPI2（螢幕佔 SPI3）；RF Pack 不需 EXT 5V；CC1101 不隨 ESP32 重開機斷電。
- 待辦：汽車鑰匙複驗 CC1101 收訊穩定度。
