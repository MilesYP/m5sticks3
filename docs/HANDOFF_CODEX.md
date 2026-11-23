# 交接給 Codex：M4 之後的正式韌體（狀態列字型 → M1.5 紅外線）

> 用法：開新的 Codex session（工作目錄 `~/Developer/m5sticks3`），把下面「---」以下整段貼進去。
> 產生於 2026-11-23（Claude Code：M4-2 畫面定稿、介面設計總則、`ui_kit` 抽出、`codex/m4-2` 已合併回 main）。

---

我是 Miles（PM，不親手寫 code）。請接手 M5StickS3 專案（機器名 M5S3Pack、助理 Friday）的正式韌體，工作目錄 `~/Developer/m5sticks3`（分支 `main`，已含 M4 體感與 `ui_kit`）。回覆用繁體中文、術語第一次出現時用一句白話解釋。先討論再實作：給計畫與要改的檔案，我說「開始實作」才寫；**畫面先出模擬圖給我看**。燒錄：`cd ~/Developer/m5sticks3/firmware && pio run -t upload -t monitor`（port busy 先 `kill $(lsof -t /dev/cu.usbmodem1101)`）。commit 後給我一行 `git push` 指令。

【開工前】
1. `cd ~/Developer/m5sticks3 && git pull && git log --oneline -5`，確認最上面有「ui_kit」與「M4-2」的 commit。
2. 依序讀：`AGENTS.md`（第 0.1 節是你這段期間的權限，11-23 已更新）→ `CLAUDE.md`（全部鐵則；第 9 節協作方式）→ `docs/PROGRESS.md` 最上面 3 筆 → **`docs/design/README.md`、`docs/design/COMPONENTS.md`（介面設計總則與共用元件，所有畫面照它）** → `docs/StickS3 介面規格.html` 第 D、5a 節。
3. 看 `docs/design/img/` 的截圖，知道現在的畫面長什麼樣。

【任務 1：狀態列中文字型（我還沒決定，先問我）】
- 預覽圖：`docs/design/img/statusbar_options.png`。A＝現行 efontTW_10（狀態列 12px）；B＝Noto Serif CJK TC Black 11px（高度不變）；C＝明朝 14px、狀態列加高到 16px（內容下移 4px）。表情頁和體感共用 `ui_kit::statusBar()`，改一處兩頁一起變。
- 先問我選哪個。選 A 就結案。選 B 或 C：
  - 狀態字（待機中、聆聽中、思考中、說話中、開心、警報、驚嚇、想睡、睡眠中、充電中、充飽、體感）用 `tools/gen_air_labels.py` 的方式產生點陣圖（字型 `NotoSerifTC-Black.otf` 從 github.com/notofonts/noto-cjk 的 `Serif/SubsetOTF/TC/` 下載到本機，不進 repo），`ui_kit::statusBar` 改畫點陣圖；找不到字時退回 efontTW_10。
  - C 要把表情頁（`ui_expression.cpp` 裡狀態列下方的 `top`、取景角、符號區、液體容器）與體感（`ui_airmenu.cpp` 各 y 座標）一起下移，先在模擬器確認沒有重疊或超出。
  - 改之前與改之後各跑一次 `bash tools/sim/regress.sh <資料夾>`，`diff -rq` 比對：B 應該只有狀態列那 12 列有差異；C 是預期的整體下移。把代表性截圖（表情各狀態、資訊頁、體感選單／已連線）給我看，我同意才燒。
  - 定稿後更新 `docs/design/README.md` 第 2、3、10 節、介面規格第 D 節、截圖。

【任務 2：M1.5 紅外線萬能遙控器】
- 照 `docs/KICKOFF_M15.md` 做（第 0 步合併已完成，跳過）：先查證 StickS3 紅外線腳位與供電（官方文件＋M5Unified，Bruce 標示 TX G46／RX G42 未驗證）、IR 函式庫在 Arduino 2.0.17 的支援與授權、IR 放在哪個模式（提 2～3 個方案讓我選），先做獨立 `ir-test/` 實機驗證，再併進 `firmware/`。
- IR 的畫面照設計總則：狀態列「MODE 0N・紅外線」、六角徽章清單（01/02/03＋名稱＋協定小字，參考 `ui_airmenu.cpp` 的 `drawMenu()`）、底部黃黑動作帶（`ui_kit::actionBand`）、固定字用點陣產生器、閱讀字用 efontTW_12 一般字不加粗。新元件要放進 `ui_kit` 並補進 `COMPONENTS.md`。

【一直都要遵守】
- 急停最高優先：任何發射／輸出前查 `killswitch::outputAllowed()`；主迴圈不阻塞；網路只在網路 task；藍牙開著時不可設 Wi-Fi `PS_NONE`。
- 不要動：語音流程、Wi-Fi 優先順序、P2 電源鍵與睡眠規則、M4 體感的連線／配對／按鍵／游標行為（我要求除外）。
- 只能用 efontTW_10／10_b／12 三種中文字型；大字一律點陣圖（Flash 目前 74.5%）。
- 不讀、不印、不提交 `firmware/src/secrets.h`、`firmware/persona_local/`、`cloud/persona/miles.local.md`。
- 只用在自己的家電；不做干擾。
- 每片結束：`docs/PROGRESS.md` 最上方加一筆（Session 類型寫「Codex」）、執行計劃 md／html 更新狀態、介面規格補截圖；新工程規則寫在 PROGRESS 該筆的「新規則」（我明確要求時才直接改 `CLAUDE.md`）；使用者看得到的新功能更新 `cloud/persona/friday.md` 並提醒我跑 `bash cloud/persona.sh`；版本號改 `firmware/src/audio_io.cpp` 的 `AUDIO_BUILD_TAG`；commit 小一點、一次一件事。

【待觀察（我會回報，不用主動改）】
- 體感已連線頁準星重畫（約 15 張／秒）對游標手感有沒有影響；若我說頓，降到 10 張／秒（`ui_airmenu.cpp` 的 `kVizMs`）或關掉準星動畫。
- P2-6 續航對照還沒量（方法見 `docs/KICKOFF_P2-close_M15.md` 第一部分第 2 點）。
