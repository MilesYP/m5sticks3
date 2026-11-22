# 接手啟動 prompt：P2 收尾（驗收＋續航對照）→ M1.5 紅外線萬能遙控器 IR1

> 用法：開新的 Claude Code（或 Cowork）session，把下面「---」以下整段貼進去。
> 產生於 2026-11-22（P2 邊用邊調到 30-p2k 之後）。

---

我在接手 M5StickS3 桌寵專案（機器名 M5S3Pack，助理叫 Friday；不要叫「小S」）（~/Developer/m5sticks3，公開 repo MilesYP/m5sticks3）。這個 session 先**把 P2 收尾**，再開始 **M1.5 紅外線萬能遙控器的 IR1**。
我是 PM、不親手寫 code；燒錄用 `cd ~/Developer/m5sticks3/firmware && pio run -t upload -t monitor`（port busy 時先 `kill $(lsof -t /dev/cu.usbmodem1101)`）；中控用 `cd cloud && npx wrangler deploy`，Friday 人設改完跑 `bash cloud/persona.sh`。回覆用繁體中文、術語要解釋。技術選擇請直接定，真的無法判斷才問我；動工前給一次計畫與要改的檔案，我說「開始實作」才寫；外觀先出模擬圖給我看再動正式程式。Cowork／橋接環境不能 git push，commit 後給我一行 `cd ~/Developer/m5sticks3 && git push`（橋接刪 git lock 要先拿刪除權限）。

【先讀】
- CLAUDE.md：**鐵則十一（P2 電源鍵與省電，整節）**、鐵則十（P1 省電分級）、鐵則九第 3 點（螢幕部分更新 `ui_fx::present`）、鐵則九第 13～17 點（A7 畫面、A8 甩碎、A4b 充電液體）、鐵則四（**腳位絕不臆測**）、鐵則六第 1、4 點（ES8311 錄放互斥、喇叭放大器）
- docs/PROGRESS.md 最上面幾筆（11-22 P2 整串：30-p2a～30-p2k）
- docs/KICKOFF_P2.md（規格、P2-0 基準、第 6 節驗收清單）
- docs/StickS3 執行計劃.md 的 P2 列與「M1.5 紅外線萬能遙控器」表；docs/StickS3 介面規格.html 第 3c、3d 節
- firmware/src/power_key.*、power_save.*、main.cpp（關螢幕／睡著時 A、B 規則）、battery_ui.cpp（畫面電量換算、3% 關機）、net_hub（`setRadioOff`）

【第一部分：P2 收尾】
目前 30-p2k，以下已在實機走過：電源鍵讀取（PM1 0x48 bit0 按著＝1）、燒錄不被看門狗打斷、`q9` 約 60 秒重置、長按畫面（四邊紅黃閃斜紋、六角框進度、框內大字＋秒數）、關螢幕／睡著時只有按住 A 1 秒或單按電源鍵會醒、睡著 10 秒自動關螢幕、ROOT／斷線狀態列標籤、甩碎持續晃動、資訊頁 MEM 紫色長條＋綠色最低線。
1. 帶我把 KICKOFF_P2 第 6 節還沒確認的項目逐條走完（列一張「已過／待測」表給我，待測的告訴我怎麼操作、看序列埠哪一行），重點：3～5 秒放開關機與關機後單按開機、5 秒進 root 後單按重置回新機制、連按五下強制關機、說話中長按關機、睡著或關螢幕 3 分鐘斷 Wi-Fi 與叫醒重連（`[pwr] Wi-Fi 斷開省電／重連`）、插線永不睡、`c0` 30 秒「電量不足」關機、問 Friday「怎麼進 root／怎麼關機／怎麼關螢幕」。
2. **P2-6 續航對照**：跟 P2-0 同一情境（充飽、`h0`、拔線、Desktop 表情頁放桌上不碰 30 分鐘、插回打 `h`），我把紀錄貼給你，你算每分鐘掉幾 % 與電壓斜率，跟基準（29-a4g：26 分鐘 −15%、約 0.58%／分、推估滿電 2.9 小時）比較。注意新版放著約 21 秒就關螢幕、3 分鐘斷 Wi-Fi，所以這次量的是「關螢幕待機」。
3. 全部過了：KICKOFF_P2、執行計劃 md／html 的 P2 列標 ✅、CLAUDE.md 鐵則十一標題改「✅」、PROGRESS 加一筆、commit。
- 已知、不用再提：插線＋用力搖迴圈 26～32ms（Miles 接受）；`friday.md` 超過 2000 字（之後精簡）。

【第二部分：M1.5 IR1（紅外線腳位驗證＋接收遙控器碼）】
- 執行計劃：IR1＝腳位實機驗證（Bruce 標示 TX G46／RX G42，**未驗證、不可當定論**）＋接收電視遙控器碼，螢幕顯示協定與數值；驗收：按遙控器，螢幕印出 NEC 等協定與碼。IR2＝學習、命名、存 NVS；IR3＝選單重放（冷氣長碼另外處理）。
- 動工前先查證、再給計畫：
  1. **StickS3 的紅外線發射／接收腳位**：查 M5Stack 官方 StickS3 文件（docs.m5stack.com 的 StickS3 頁、腳位表、IR 範例）與 M5Unified 原始碼，跟 Bruce 的設定交叉比對；官方文件提到「使用 IR 接收時要關閉 SPK 功放（M5PM1 PYG3）」——確認接收器的供電／致能是否也經 M5PM1，跟鐵則六第 4 點（不要自己通斷喇叭放大器）怎麼共存。
  2. **函式庫**：IRremoteESP8266 或 Arduino-IRremote 在 `espressif32@6.12.0`（Arduino 2.0.17）上對 ESP32-S3 的支援、版本、授權；RMT 通道會不會跟現有東西衝突。
  3. **模式歸屬**：IR 放在哪個模式（Desktop 內的功能、或新模式）、跟鐵則二互斥狀態機與 Friday 語音（ES8311 播放時功放開著）怎麼共存，提方案讓我選。
- 先寫一個最小測試（可以是獨立的 `ir-test/` PlatformIO 專案，設定照抄 `selftest/`），實機驗到腳位與收碼，再併進正式韌體。

【限制】
- 急停最高優先；主迴圈不可阻塞；網路只在網路 task。
- 不要動語音流程、Wi-Fi 優先順序邏輯（鐵則七第 12 點）、P2 已定案的按鍵與睡眠規則（除非我要求）。
- 不能用大的中文字型（鐵則九第 15 點）；大字用 `tools/gen_kanji.py` 產生點陣圖。
- 不提交 `firmware/src/secrets.h`、`firmware/persona_local/`、`cloud/persona/miles.local.md`；不讀、不印 secrets 的值。
- 每個切片結束：更新 PROGRESS.md、CLAUDE.md、執行計劃 md／html、需要時介面規格；使用者看得到的新功能要更新 `cloud/persona/friday.md` 並提醒我跑 `persona.sh`。
