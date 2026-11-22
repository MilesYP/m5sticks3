# 接手啟動 prompt：M4-0 收尾（Mac 藍牙連上秒斷）→ M4-1 陀螺儀游標

> 用法：開新的 Claude Code（或 Cowork）session，把下面「---」以下整段貼進去。產生於 2026-11-23 00:10（韌體 31-m4j）。

---

我在接手 M5StickS3 桌寵專案（機器名 M5S3Pack，助理叫 Friday）（~/Developer/m5sticks3，公開 repo MilesYP/m5sticks3，AGPL-3.0-or-later）。這個 session 先把 **M4-0（BLE 藍牙滑鼠最小版）收尾**，重點是 **Mac 連上秒斷**，過了再做 **M4-1（陀螺儀 → 游標）**。
我是 PM、不親手寫 code；燒錄 `cd ~/Developer/m5sticks3/firmware && pio run -t upload -t monitor`（port busy 先 `kill $(lsof -t /dev/cu.usbmodem1101)`）。回覆繁體中文、術語要解釋；技術選擇直接定，真的無法判斷才問；外觀先出模擬圖。橋接環境不能 git push：commit 後給我一行 `cd ~/Developer/m5sticks3 && git push`（橋接刪 git lock 要先拿刪除權限）。**commit 前先 `git branch --show-current` 確認在 main**（Codex 曾在同一資料夾切分支，現在它應該在 `~/Developer/m5sticks3-rf` worktree）。雲端環境連不到 PlatformIO 伺服器，編譯以我的 Mac 為準；NimBLE 原始碼可 `git clone --branch 2.5.0 https://github.com/h2zero/NimBLE-Arduino` 查 API。

【先讀】
- CLAUDE.md：**鐵則十二（M4 空中滑鼠，第 1～14 點）**、鐵則二、三、六第 9 點（錄音時 Wi-Fi 關省電）、第 8 節授權
- docs/PROGRESS.md 最上面 11-22 的 M4-0 系列（31-m4a～31-m4j）
- firmware/src/ble_mouse.*、mode_manager.cpp（AirMouse enter/exit、currentReady）、ui_modeswitch.cpp（過場等資源）、net_hub.cpp（setRadioOff 序號、setPsNone）、firmware/scripts/patch_nimble.py
- AGENTS.md（RF 軌由 Codex 在 rf-lab/ 平行做，不要碰 rf-lab/、docs/rf/）

【目前狀態（31-m4j，全部實機確認）】
- ✅ 切模式：AirMouse ↔ Desktop 快速來回不當機；離開時藍牙真的關（deinit 競態已用 patch_nimble.py 修）；過場等資源（Desktop 等中控連上約 2.5 秒、AirMouse 等藍牙廣播）；回 Desktop 後 Friday 第一次就回。
- ✅ iPhone 配對、加密、連線間隔 15ms（但 `@1` 畫圓還沒實測）。
- ✅ 資訊頁不讀晶片溫度（防中斷看門狗）；充電「充飽」判定實機觸發。
- ❌ **Mac**：附近裝置看得到、按連線 → Mac 跳配對確認 → 按同意 → 我們這邊「配對完成（已加密）｜綁定 是｜驗證 否｜金鑰 16」→ **Mac 主動斷線（0x213）**，沒走到「主機訂閱滑鼠報告」。Mac 會跳「設定藍牙滑鼠」視窗（認得是滑鼠）。要在 Mac「忘記此裝置」才能再連。
  - 已試（都斷）：預設（主動要求加密＋SC＋IRK）、`@s0`（不主動要求 → Mac 根本不配對，24 秒沒加密）、`@c0`（舊式配對）、`@c0 @k0`。**但這幾輪都沒在裝置端 `@f`（已配對一直是 2 台），不是乾淨狀態**；Mac 端 `log stream` 也還沒抓。
  - 注意：Mac 選了 **7.5ms 連線間隔**（iPhone 是 15ms）。我們廣播的偏好間隔是 7.5～15ms（`setPreferredParams(6, 12)`）。**Apple 配件設計指南對 HID 的最短間隔是 11.25ms**——這是目前最可疑的原因，還沒試。

【這個 session 要做】
1. 新版韌體：把偏好連線間隔改成符合 Apple 規範（例如 12～24＝15～30ms，或 9～12＝11.25～15ms；連上後也可用 `updateConnParams` 主動要求），做成序列埠可切（類似 `@s/@c/@k`）；順便修小 bug：`s_conn_t` 斷線時沒歸零，重連後會印出錯的「連上 22 秒仍未加密」。
2. 帶我做乾淨的 Mac 測試：Mac 忘記此裝置 → AirMouse → `@f`（要看到「→ 0 台」）→ Mac 連線；同時 Mac 終端機跑
   `log stream --style compact --predicate 'process == "bluetoothd"' | grep -iE "disconnect|pair|smp|hid|error|reason|fail"`，看 Mac 自己為什麼斷。
3. 還是不行的下一步候選：HID Information 的 flags、加 Boot Mouse Input Report、PnP 的 VID 來源、報告描述的格式；或換別的 Mac 對照。
4. Mac 通過後完成 M4-0 驗收：Mac／iPhone `@1` 畫圓、畫圓中 B 按住 3 秒急停、iPad 配對；然後 PROGRESS、CLAUDE.md、執行計劃標 M4-0 ✅、commit。
5. 接著 M4-1（陀螺儀轉速 → 游標、靜止自動校正零點、死區、加速曲線；Miles 已定：拿起來就動、A 短按左鍵／按住拖曳）：先給計畫與要改的檔案，我說「開始實作」再寫。

【限制】急停最高優先；主迴圈不可阻塞（`ble_mouse::end()` 等斷線最多 0.65 秒是例外，被過場蓋住）；網路只在網路 task；藍牙開著時 Wi-Fi 不可設 PS_NONE；不要動語音流程、P2 按鍵與睡眠規則；不提交 secrets.h、persona_local/、miles.local.md。每個切片結束更新 PROGRESS.md、CLAUDE.md、執行計劃。
