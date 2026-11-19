# 接手啟動 prompt：M1-A / A3 IMU 互動（表情對搖晃、拍打、靜置、聲音有反應）

> 用法：開新的 Claude Code（或 Cowork）session，把下面整段貼進去。
> 產生於 2026-11-20（M1-B 完成之後）。

---

我在接手 M5StickS3 桌寵「小S」專案（~/Developer/m5sticks3，公開 repo MilesYP/m5sticks3）。這一片是 **M1-A / A3 IMU 互動**：讓表情對搖晃、拍打、放著不動、聲音有反應。
我是 PM、不親手寫 code；燒錄用 VS Code 的 PlatformIO 按鈕。回覆用繁體中文、術語要解釋。技術選擇請直接定，真的無法判斷才問我；動工前給一次計畫與要改的檔案，我說「開始實作」才寫。Cowork／橋接環境不能 git push（commit 前先請我允許刪檔），commit 後給我一行 `cd ~/Developer/m5sticks3 && git push`。橋接環境無法完整編譯：韌體用模擬標頭做語法檢查、純邏輯用 g++ 測。

【先讀】
- CLAUDE.md：鐵則二（互斥狀態機）、鐵則三（急停）、鐵則六（音訊，第 7 點麥克風待機常開、第 10 點人聲指標）、鐵則七第 11 點（網路 task）
- docs/PROGRESS.md 最上面 2 筆（最新是 M1-B / B3c＋B4）
- docs/StickS3 執行計劃.md 的 A3 列；docs/StickS3 介面規格.html 第 2 節（表情狀態與觸發表）
- firmware/src/ui_expression.*、voice_turn.*、audio_io.*（`vadStats`／`silenceMs` 人聲指標）、main.cpp、config.h

【目前狀態】
- 九種表情已做好（Idle／Listening／Thinking／Speaking／Happy／Angry／Scared／Sleepy／Asleep，顏色固定、平滑過渡），目前沒有任何東西觸發 Happy／Angry／Scared／Sleepy／Asleep。
- Desktop 模式下 **A＝按住講話**（GPT-Live 助理 Friday）；Listening／Thinking／Speaking 由 voice_turn 本機決定。B 短按切模式、B 按住 3 秒急停。
- 下方有對話區（B4），語音結束後停 5 秒收起。

【這片要做（規格來自介面規格第 2 節）】
- 放著不動 6 秒 → 想睡（Sleepy），11 秒 → 睡著（Asleep）。
- 搖晃 → 驚嚇（Scared）；拍打（清醒時）→ 隨機開心／生氣；拍打（睡覺時）→ 生氣（被吵醒）。
- 聲音（睡覺時）→ 驚嚇或生氣（麥克風待機本來就開著，用現有人聲指標；門檻先量測再定）。
- 傾斜 → 眼睛看向傾斜方向（若做會拖長這片，可列到 A4）。
- **語音優先**：voice_turn 忙（錄音／等待／播放）時 IMU 反應不得蓋掉 Listening／Thinking／Speaking，也不能讓它進入睡著。按 A 講話一律叫醒。
- 反應持續一段時間後回 Idle；不可阻塞主迴圈（鐵則：非阻塞、急停最高優先）。

【限制】
- 搖晃、拍打、靜置的門檻**先量測再定**（鐵則六第 8 點同理）：先做序列埠印 IMU 加速度／角速度的量測模式，請我實際搖、拍、放著，再由數據定門檻。不要憑經驗猜數字。
- IMU 用 M5Unified 的 `M5.Imu`（BMI270，內部 I2C `0x68`）。
- 版本標記 `AUDIO_BUILD_TAG` 改 `22-a3`。
- 完成後更新 `cloud/persona/friday.md`（「現在會做的事」加表情互動），並提醒我跑 `cloud/persona.sh`。

【驗收】
1. 放著不動：6 秒想睡、11 秒睡著；拿起來或按 A 會醒。
2. 搖晃一次 → 驚嚇；拍一下 → 開心或生氣；睡著時拍 → 生氣。
3. 睡著時拍手或講話 → 驚嚇或生氣。
4. 語音對話中搖、拍不影響 Listening／Thinking／Speaking，對話正常。
5. B 長按 3 秒急停照常立即生效。

【收尾】
通過後：CLAUDE.md 補 IMU 門檻規則；PROGRESS.md 最上面加一筆、改「目前狀態」；執行計劃 md/html 把 A3 標 ✅、下一片改 A4；寫 `docs/KICKOFF_A4.md`；更新 friday.md；git commit。
