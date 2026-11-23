# 接手啟動 prompt：M1.5 紅外線萬能遙控器（IR1 起）

> 用法：開新的 Claude Code（或 Cowork）session，把下面「---」以下整段貼進去。
> 產生於 2026-11-23（M4 空中體感滑鼠完成、介面設計總則定稿之後）。取代 `KICKOFF_P2-close_M15.md` 的第二部分。

---

我在接手 M5StickS3 桌寵專案（機器名 M5S3Pack，助理叫 Friday；不要叫「小S」）（~/Developer/m5sticks3，公開 repo MilesYP/m5sticks3）。這個 session 做 **M1.5 紅外線萬能遙控器**，從 **IR1（腳位驗證＋收碼）** 開始。
我是 PM、不親手寫 code；燒錄用 `cd ~/Developer/m5sticks3/firmware && pio run -t upload -t monitor`（port busy 時先 `kill $(lsof -t /dev/cu.usbmodem1101)`）；Friday 人設改完跑 `bash cloud/persona.sh`。回覆用繁體中文、術語要解釋。技術選擇請直接定，真的無法判斷才問我；動工前給一次計畫與要改的檔案，我說「開始實作」才寫；**畫面先出模擬圖給我看再動正式程式**。Cowork／橋接環境不能 git push，commit 後給我一行 push 指令。

【第 0 步：確認分支】
- M4-2（體感）在分支 `codex/m4-2`（工作目錄 `~/Developer/m5sticks3-m4-2`）完成並定稿。先 `git log --oneline -5` 看 main 是否已合併 `codex/m4-2`；沒有的話問我要不要先合併，**不要在舊的 main 上開 IR**。
- RF 軌是 Codex 在 `~/Developer/m5sticks3-rf`（`codex/rf-lab`）平行做的，不要碰。RF 跟這台 StickS3 共用，燒錄前確認我現在要測哪一邊。

【先讀】
- `CLAUDE.md`：鐵則一（Grove）、二（互斥狀態機）、四（**腳位絕不臆測**）、六第 1、4 點（ES8311 錄放互斥、**不要自己通斷喇叭放大器**）、九（畫面規則）、十一（P2 電源鍵與省電）、十二（M4，看模式進出怎麼關資源）、第 9 節協作方式
- **`docs/design/README.md` 與 `docs/design/COMPONENTS.md`（介面設計總則，11-23 定稿）**——IR 的畫面一律照它：狀態列「MODE 0N・紅外線」、清單用六角徽章（像體感選單／介面規格的 RF 清單 01/02/03＋名稱＋協定小字）、主要動作放底部黃黑斜紋帶、固定字用點陣產生器、閱讀字用 efontTW_12 一般字。看 `docs/design/img/` 的實際截圖
- `docs/PROGRESS.md` 最上面幾筆；`docs/StickS3 執行計劃.md` 的「M1.5 紅外線萬能遙控器」表；`docs/StickS3 介面規格.html` 第 D、5、5a 節
- 參考實作：`firmware/src/ui_airmenu.*`（選單＋等待＋按鍵分派＋畫面的完整範例）、`mode_manager.*`、`air_mouse.*`／`ble_mouse.*`（模式進出時資源開關）

【IR 切片（執行計劃）】
- IR1：實機驗證 G42 接收電視遙控器的 NEC 碼，螢幕與序列埠顯示協定與數值。G46 發射留待 IR3 實測。
- IR2：學習、命名、存 NVS（`Preferences`），重開機不消失。
- IR3：選單重放（冷氣這類長碼另外處理）。按下去，電視／冷氣真的動。

【11-24 查證與決議】
1. **腳位與供電**：[StickS3 官方腳位表](https://docs.m5stack.com/en/core/StickS3) 與 [Bruce 板卡設定](https://github.com/BruceDevices/firmware/blob/main/boards/m5stack-sticks3/m5stack-sticks3.ini) 一致，RX G42／TX G46。紅外線用 `EXT_5V` 供電，接收時依 [M5Stack 官方範例](https://docs.m5stack.com/en/arduino/m5sticks3/ir_nec) 關閉喇叭功放。沿用 M5Unified 0.3.0 的 `M5.Speaker.end()`，由既有回呼處理 PYG3。
2. **收碼**：官方要求由 RMT 硬體量脈衝。IR1 用 Arduino 2.0.17 綁定的 [ESP-IDF 4.4.7 RMT 驅動](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/peripherals/rmt.html)（`driver/rmt.h`），以非阻塞方式接收並解碼 NEC。`Arduino-IRremote 4.4.1`（MIT）與 `IRremoteESP8266 2.8.6`（LGPL-2.1）各自的標準接收路徑使用計時器或 GPIO 中斷，IR1 使用內建驅動即可。正式合併前再驗證 RMT 通道配置與模式退出時的資源釋放。
3. **模式歸屬**：Miles 選獨立「紅外線」模式，與 Bruce 的入口安排一致。B 循環加入一站，沿用 B 長按急停。正式整合時使用現有的音訊暫停／恢復接口，進入時關音訊，退出先釋放 RMT。模式畫面依設計總則先出模擬。
4. **IR1 檔案**：獨立的 `ir-test/platformio.ini`、`ir-test/src/main.cpp`、`ir-test/README.md`；沿用 `selftest/` 的板卡設定，先實機驗 G42 收碼。等 Miles 說「開始實作」再建立測試專案。

【限制】
- 急停最高優先、IR 發射前查 `killswitch::outputAllowed()`；主迴圈不可阻塞；網路只在網路 task。
- 不要動語音流程、Wi-Fi 優先順序、P2 按鍵與睡眠規則、M4 體感的連線／按鍵／游標行為（除非我要求）。
- 只用在自己的家電；不做干擾。
- 不能用大的中文字型（鐵則九第 15 點）。
- 不提交 `firmware/src/secrets.h`、`firmware/persona_local/`、`cloud/persona/miles.local.md`；不讀、不印 secrets 的值。
- 每個切片結束：更新 PROGRESS.md、CLAUDE.md、執行計劃 md／html、介面規格（含模擬截圖），新元件補進 `docs/design/COMPONENTS.md`；使用者看得到的新功能更新 `cloud/persona/friday.md` 並提醒我跑 `persona.sh`；commit＋push。
