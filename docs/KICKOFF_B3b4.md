# 接手啟動 prompt：M1-B / B3b-4 邊收邊播＋斷線處理

> 用法：開新的 Claude Code（或 Cowork）session，把下面整段貼進去。
> 產生於 2026-11-19（B3b-3 驗收通過、commit `7b85869` 之後）。

---

我在接手 M5StickS3 桌寵「小S」專案（~/Developer/m5sticks3，公開 repo MilesYP/m5sticks3）。這一片是 **M1-B / B3b-4 邊收邊播＋斷線處理**。
我是 PM、不親手寫 code；燒錄用 VS Code 的 PlatformIO 按鈕（Build ✓ / Upload → / Serial Monitor 🔌），中控用 `cd ~/Developer/m5sticks3/cloud && npx wrangler deploy` 部署。
回覆用繁體中文、術語要解釋。動工前先給計畫與要改的檔案，等我說「開始實作」才寫。Cowork／橋接環境不能 git push：commit 後給我一行 `cd ~/Developer/m5sticks3 && git push`。

【先讀】
- CLAUDE.md：鐵則六（音訊，第 9 點 Wi-Fi 省電＝滋滋聲）、**鐵則七（連網，B3b-2／B3b-3 實測：RTT、阻塞送出、每則訊息固定成本、μ-law 預設、熱點連線重置）**、第 8 節機密規則
- docs/PROGRESS.md 最上面 2~3 筆（最新是 B3b-3）
- docs/StickS3 執行計劃.md「切片拆分」M1-B 表（B3b-4、B3b-5、B3c 三列）
- docs/M1B-架構.md 第 4 節（協定：start 帶 up/down、format、audio_end）與第 9 節（GPT-Live 格式定案）
- firmware/src/voice_turn.*、net_hub.*、audio_io.*、adpcm.*、ui_expression.*、main.cpp；cloud/src/voice_session.js、adpcm.js

【目前狀態】
- 一輪語音（Desktop 按住 A）：錄音（PSRAM，上限 6 秒）→ 放開後整段上傳 PCM16（3200 bytes 一幀、每圈 2 幀）→ 中控暫存 → stop 時把 PCM 轉成下載格式回傳（預設 **μ-law**，合併成每則 ≤8040 bytes）→ 裝置收完 audio_end 才解壓、正規化、播放。表情由本機決定（Listening→Thinking→Speaking→Idle）。
- 實測（iPhone 熱點）：RTT 約 290～400ms、上傳約 36KB/s、下載約 23～26KB/s；5～6 秒錄音的等待回傳 μ-law 約 3.6 秒。
- 量測工具：每輪印 `[perf]`（各階段的主迴圈、ws.loop、送幀耗時、收幀數）、`[voice]`（格式、SNR、來回時間）；pong 印 RTT；序列埠 `f0/f1/f2` 切 PCM／ADPCM／μ-law、`k` 量 RTT、`w0/w1` Wi-Fi 開關、`?` 目前設定。
- 待確認：B3b-3 最後一版（v17-b3b3）燒錄後「開機第一個序列埠指令是否有效」——若無效，裝置會印 `不認得的指令「…」（hex: …）`。

【這一片要做的（細節先跟我討論）】
1. **邊收邊播**：中控 stop 後改成「照真實播放速度」一段一段送（模擬 GPT-Live 的 `session.output_audio.delta`），裝置先緩衝約 0.3 秒就開始播、邊播邊收。μ-law 需 16KB/s、熱點約 25KB/s（餘裕 1.5 倍），緩衝要能吸收 1～3 秒的卡頓；印出緩衝餘量、斷音（underrun）次數。注意 ES8311 同時只能錄或放（鐵則六第 1 點），邊收邊播只發生在回答階段。M5Unified `Speaker.playRaw` 的排隊行為要先查原始碼確認，不可臆測。
2. **查連線重置**：`ssl_client (-80) Connection lost`（閒置約每 40～80 秒、上傳中也發生過，前後 RTT 飆到 1～3 秒）。先量測（時間點、與 ping 間隔／省電／資料量的關係）再推原因，可對照家用 Wi-Fi。
3. **斷線畫面提示（我在 11-19 提出）**：斷線時畫面顯示斷線標誌、表情定格；恢復連線後繼續動或回到起始——哪一種、標誌長怎樣，先給我選項再做（參考 docs/StickS3 介面規格.html 的畫面規範）。回答播到一半斷線時要怎麼收尾也一併提方案。

【限制】
- 依鐵則六第 8 點、鐵則七第 8 點：先量測再改；推測不可直接當結論改程式。
- 版本標記印在每次錄音那一行（開機時印的會因序列埠未連上而遺失）。
- B3b-5（上傳邊錄邊傳＋網路移到獨立 task）是下一片，這片不要順手做；若邊收邊播發現非得先把網路移到獨立 task，先停下來跟我說。

【機密】HUB_TOKEN、Wi-Fi 帳密在 firmware/src/secrets.h（git 忽略），不印出、不讀值；`wrangler tail` 輸出含 token，貼之前要遮。commit 前確認 git status 沒有 secrets.h。

【收尾】
驗收：放開 A 後很快開始播、中途不斷音；斷線時畫面有提示、恢復後正常。
通過後：CLAUDE.md 鐵則七補結論；PROGRESS.md 最上面加一筆、改「目前狀態」；執行計劃 md/html 把 B3b-4 標 ✅、下一片改 B3b-5；寫下一片的 KICKOFF；git commit。
