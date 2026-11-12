# 啟動 Prompt（每個里程碑開一個新 Session，貼對應的 prompt）

> 用法：在 Claude Code（或連到 `~/Developer/m5sticks3` 的 Session）開啟後，把「=== PROMPT 開始 ===」到「=== PROMPT 結束 ===」之間整段貼上。
> 硬體驗收已於 2026-11-12 全數通過（見 `docs/hardware-acceptance-test.md`），從骨架開始。

---

## M0：韌體骨架（目前要做的）

=== PROMPT 開始 ===

你是 M5StickS3 專案的結對工程師。專案在我 Mac 的 `~/Developer/m5sticks3/`（GitHub：MilesYP/m5sticks3，公開）。回覆用繁體中文，術語第一次出現要用一句白話解釋。

**開始前先讀這三份文件，照裡面的鐵則做：**
1. `CLAUDE.md`：工程約定、鐵則一～五、已驗證腳位、函式庫版本
2. `docs/M5StickS3+RF Pack S3.md`：PRD
3. `docs/hardware-acceptance-test.md`：硬體驗收紀錄與實測結論
4. `docs/PROGRESS.md`：跨 Session 進度紀錄（先看「目前狀態」與最上面幾筆）

**背景**：硬體已驗收完成。`selftest/` 與 `acceptance-test/` 是驗收用的獨立專案，**不要改它們**。正式韌體要建在新的 `firmware/` 子資料夾（獨立 PlatformIO 專案，設定照抄 `selftest/platformio.ini`）。

**本次任務：M0 骨架**，只做以下三件事，不碰 Wi-Fi、音訊、HID、RF、BLE：
- `mode_manager`：四個模式的 enum、每個模式的 `enter()`/`exit()` 介面（內容先留空殼），以及「切換模式」的邏輯。切換時要先呼叫舊模式 `exit()` 再呼叫新模式 `enter()`（鐵則二）。
- `input_hal`：開機掃描 Grove I2C（`M5.Ex_I2C`，記得先 `setExtOutput(true)`），辨識 CardKB `0x5F`／Scroll `0x40`／皆無，統一成輸入事件。
- `killswitch`：急停旗標，最高優先、不可被阻塞（鐵則三），先接到「停止當前模式、回到安全狀態」即可。

**第一步不要寫程式，先做這兩件事並等我確認：**
1. **按鍵配置決議**：`CLAUDE.md` 第 2 節「待決議」——正面鍵 A 同時被分配到切換模式、急停、模式 4 左鍵，互相衝突。請提出 2～3 個按鍵配置方案（可用 A、側鍵 B、電源鍵，以及短按／長按／雙擊），比較每個方案的誤觸風險與操作直覺，特別是模式 2 急停的可靠性。我選定後，你要把決議寫回 `CLAUDE.md`。
2. **實作計畫**：列出要建立的檔案、每個檔案的職責與對外介面、模組之間的呼叫關係（可用 Obsidian 相容的 Mermaid 圖），以及驗收方式。

我說「開始實作」後再寫程式。實作時：
- 非阻塞（用 `millis()` 或 FreeRTOS task），腳位與 I2C 位址寫成具名常數。
- 每個模組都要能單獨編譯；先 build 通過再往下。
- 螢幕要顯示：目前模式名稱、偵測到的輸入裝置（CardKB／Scroll／無）、急停狀態。
- 序列埠訊息會在我打開 Serial Monitor 前遺失，所以要能用按鍵重印狀態報告。

**驗收標準**：
1. 開機後，螢幕顯示模式名稱與輸入裝置。
2. 用決議的按鍵能在四個空模式之間循環切換，序列埠印出每次的 `exit()`/`enter()`。
3. 插 CardKB 或 Scroll 開機，能正確辨識（若手邊沒有配件，至少顯示「無」）。
4. 觸發急停後立即進入安全狀態，畫面有明確提示。

每一步結束請附：改了哪些檔、如何編譯燒錄（長按電源鍵到綠燈閃 → Upload → 短按電源鍵）、怎麼驗收。M0 完成時依 `CLAUDE.md` 第 9 節更新 `docs/PROGRESS.md`，再 `git commit` + `git push`。

=== PROMPT 結束 ===

---

## 之後的里程碑（依序，每個開新 Session）
- M1-A：模式 1 離線表情（Grok 風幾何臉：Idle 呼吸＋隨機眨眼，下方 25% 跑馬燈區）
- M1-B：模式 1 疊 ES8311 音量連動嘴型 ＋ MEMS 麥克風收音
- M1-C：Wi-Fi ＋ 後端 STT/LLM/TTS 串接，CardKB 靜音輸入
- M3：模式 3 RF（SmartRC + RCSwitch：學一組固定碼 → 存 NVS → 發射；RSSI 掃描圖；支援 315／433.92MHz）
- M4：模式 4 BMI270 → BLE 滑鼠
- M2：模式 2 USB HID 外掛（先 PC/macOS 版，再挑戰 iOS/AirPlay）
