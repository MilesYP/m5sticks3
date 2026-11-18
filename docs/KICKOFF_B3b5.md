# 接手啟動 prompt：M1-B / B3b-5 上傳邊錄邊傳＋網路背景化

> 用法：開新的 Claude Code（或 Cowork）session，把下面整段貼進去。
> 產生於 2026-11-19（B3b-4 驗收通過之後）。

---

我在接手 M5StickS3 桌寵「小S」專案（~/Developer/m5sticks3，公開 repo MilesYP/m5sticks3）。這一片是 **M1-B / B3b-5 上傳邊錄邊傳＋網路背景化**。
我是 PM、不親手寫 code；燒錄用 VS Code 的 PlatformIO 按鈕（Build ✓ / Upload → / Serial Monitor 🔌），中控用 `cd ~/Developer/m5sticks3/cloud && npx wrangler deploy` 部署。
回覆用繁體中文、術語要解釋。動工前先給計畫與要改的檔案，等我說「開始實作」才寫。Cowork／橋接環境不能 git push（commit 前要先請我允許刪檔，否則 git 會留 lock 檔）：commit 後給我一行 `cd ~/Developer/m5sticks3 && git push`。橋接環境連不到 PlatformIO 套件伺服器、無法完整編譯：用模擬標頭做語法檢查、純邏輯部分用 g++／node 測，完整編譯以我 Mac 為準。

【先讀】
- CLAUDE.md：鐵則六（音訊）、**鐵則七（連網，第 1～2 點上傳速度與 sendBIN 阻塞、第 7 點連線重置與重連卡 2.3 秒、第 9 點中控 DO 閒置 10 秒休眠、第 10 點邊收邊播與 M5Unified playRaw 規則）**、第 8 節機密規則
- docs/PROGRESS.md 最上面 2～3 筆（最新是 B3b-4）
- docs/StickS3 執行計劃.md「切片拆分」M1-B 表（B3b-5、B3c 兩列）
- docs/M1B-架構.md 第 4 節（協定：start 帶 up/down/pace、format、audio_end 的 g／ms；B3b-4 邊收邊播段落）與第 9 節（GPT-Live 格式）
- firmware/src/voice_turn.*、net_hub.*、audio_io.*、adpcm.*、main.cpp、diag_cmd.*；cloud/src/voice_session.js、adpcm.js

【目前狀態】
- 一輪語音：按住 A 錄音（PSRAM，**上限 6 秒**，`CAP_SAMPLES`）→ 放開後**整段**上傳 PCM16（3200 bytes 一幀、主迴圈每圈 2 幀，`sendBIN` 阻塞）→ 中控正規化、轉 ADPCM 8kHz、全速一則一則回傳 → 裝置邊收邊播（環形緩衝、播放 task 自餵）。
- 實測：放開 A 到開始播約 6.5～7 秒，**幾乎全卡在上傳**（188KB 要 5～6 秒；壞的晚上 RTT 630～760ms 時上傳只剩約 9.5KB/s、要 19 秒）。上傳期間主迴圈平均 200～670ms 一圈（畫面卡）。中控送完後 0.6～1.4 秒就開始播、0 斷音。
- 斷線重連（TLS 交握）會卡主迴圈約 2.3 秒，錄音中發生就少錄 2 秒（B3b-4 log）。
- `ssl -80` 連線重置原因未查到：B3b-4 已加 `[link]` 紀錄，我會貼閒置時收到的紀錄給你。

【這一片要做的（細節先跟我討論）】
1. **網路移到獨立 FreeRTOS task**：`ws.loop()`、`sendBIN`、重連都不再卡主迴圈（錄音、畫面、急停要一直有反應）。要定清楚跨 task 的資料交接：上傳用佇列或環形緩衝；收到的音訊回呼會改在網路 task 執行（`audio_io::streamWrite` 目前是單一生產者設計，生產者換 task 要確認）；`voice_turn` 狀態機留在主迴圈還是怎麼同步；`net_hub` 的旗標與計數哪些要 atomic。task 的核心、優先權、堆疊大小要說明理由（急停 task 在 core 0，鐵則三）。
2. **邊錄邊傳**：按下 A 就開始送，錄音進 PSRAM 大緩衝，送不完累積在緩衝、不掉資料；印出累積待送量。放開後約 1 秒內送完是目標。
3. **上傳頻寬**：PCM16 要 32KB/s，熱點實測 9.5～36KB/s，**很可能跟不上**。是否也壓縮上傳（例如 `up=adpcm`，中控解回 PCM16 再給 GPT-Live）要先跟我比較：OpenAI 辨識品質的風險、裝置編碼 CPU、延遲。先量測再決定。
4. **拿掉 6 秒上限**：錄音緩衝改成邊錄邊送的環形緩衝；中控每輪 256KB 上限一併檢查。
5. 看我貼的 `[link]` 紀錄，判斷連線重置要不要在這片處理。

【限制】
- 依鐵則六第 8 點、鐵則七第 8 點：先量測再改；推測不可直接當結論改程式（B3b-4 我推測爆音是重連造成，log 一看就錯了）。
- 版本標記印在每次錄音那一行（`AUDIO_BUILD_TAG`）。
- B3c（接 GPT-Live）是下一片，這片不要順手做。
- 鐵則三：急停 task 與 ISR 優先，網路 task 不可影響它。

【機密】HUB_TOKEN、Wi-Fi 帳密在 firmware/src/secrets.h（git 忽略），不印出、不讀值；`wrangler tail` 輸出含 token，貼之前要遮。commit 前確認 git status 沒有 secrets.h。

【收尾】
驗收：按住 A 講 10 秒以上不斷、畫面不卡；放開後約 1 秒內開始播；錄音中斷線重連不少錄。
通過後：CLAUDE.md 鐵則七補結論；PROGRESS.md 最上面加一筆、改「目前狀態」；執行計劃 md/html 把 B3b-5 標 ✅、下一片改 B3c；寫下一片的 KICKOFF；git commit。
