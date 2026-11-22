# 交接：M4-1 空中滑鼠手感收尾（2026-11-23，給 Claude Code／Codex）

> 11-23 起 Claude Code 與 Codex **暫時共同維護** `firmware/`（Claude 的 token 恢復前），規則見 `AGENTS.md` 第 0.1 節。
> 開工前：`git pull`，讀 `CLAUDE.md` 鐵則十二（特別是第 17、18 點）與 `docs/PROGRESS.md` 最上面 3 筆。

---

## 現況（版本 31-m4r＋`4c1df34` 修正）

- **M4-0／M4-2 BLE 滑鼠 ✅**：Mac、iPhone、iPad 都能配對、訂閱報告、`@1` 畫圈；B 按住 3 秒急停、B 短按回來後自動重連。
- Mac「配對完就斷」已結案：原因是 **Mac 上其他 app 的藍牙權限**（Brave、Teams…），不是韌體。Mac 測試前先確認沒有 app 有藍牙權限（Teams 開機要求時按不允許）。`ble-mouse-ref/`（Bluedroid 對照韌體）留著當日後對照工具。
- **M4-1 陀螺儀游標：實作完成、手感調整中**（`firmware/src/air_mouse.*`）
  - 管線：轉速 − 零點偏差 → 死區 → 每秒點數＝增益×轉速^指數 → 累積小數 → 每 10ms 最多一筆報告；累積量上限 2 筆（快甩不欠帳）。
  - 零點在 Desktop 開機就校正完成；進 AirMouse 會印 `[air]` 狀態。IMU 約 200 筆/秒、報告約 100 筆/秒。
  - A＝左鍵（按下／放開，前後凍結 80ms）→ 點擊、拖曳。
  - 拿法：像遙控器，螢幕朝上、頂端指向電腦；方向已實測正確（X＝−gz、Y＝−gx）。
  - 目前預設：靈敏度 `AIR_GAIN`＝25、曲線指數 `AIR_ACCEL_EXP`＝1.0（線性＝指哪跑哪）、死區 `AIR_DEADZONE_DPS`＝1.0。
  - 第一輪 Miles 回饋（預設 4、指數 1.2）：太鈍、沒有指哪跑哪、快甩跟不上 → 已改成上面的值，**31-m4r 尚未得到 Miles 的手感回饋**。

## 下一步

1. Miles 燒 31-m4r（`cd ~/Developer/m5sticks3/firmware && pio run -t upload -t monitor`），用序列埠 `@g`（靈敏度）、`@n`（曲線指數）、`@d`（死區）、`@x／@y`（翻方向）調到順手，`@` 看目前值，`@m` 開關每 0.1 秒量測。
2. 把 Miles 定案的數字寫回 `config.h` 的 `AIR_*`（註解寫日期與實測依據），版本號 `audio_io.cpp` 的 `AUDIO_BUILD_TAG` 加一號。
3. M4-1 驗收：游標跟手不抖；桌上放 1 分鐘不飄；Finder 點擊與拖曳；移動中 B 按住 3 秒急停立刻停。
4. 驗收通過 → 更新 `docs/PROGRESS.md`、`CLAUDE.md` 鐵則十二第 18 點、`docs/StickS3 執行計劃.*`（M4-1 ✅）→ commit。
5. 接著 **M4-3**：Scroll Unit 滾輪＝捲動、按壓＝右鍵（沒插 Scroll 時只有 A 左鍵）；AirMouse 畫面（連線狀態、靈敏度）——**畫面先做模擬給 Miles 看**（`tools/sim/`）。
6. 待辦（未排期）：設定後台＋LINE 入口，見執行計劃「設定後台與 LINE 入口待辦」。

## 注意

- 每筆報告前都經過 `ble_mouse::report()` → 急停閘門；不要繞過。
- AirMouse 模式 Wi-Fi 是關的、藍牙開著時不可設 Wi-Fi `PS_NONE`（鐵則十二第 7 點）。
- IMU 只在 `imu_pet::loop()` 讀，`air_mouse::loop()` 要排在它後面。
- 編譯以 Miles 的 Mac 為準；雲端環境下載不到 PlatformIO 工具鏈。
