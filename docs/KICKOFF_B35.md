# 接手啟動 prompt：M1-B / B3.5 連線 token 驗證

> 用法：開新的 Claude Code session，把下面整段貼進去。
> 產生於 2026-11-18（B3b-1 驗收通過之後）。

---

我在接手一個進行中的硬體專案，請先讀 repo 再動手，一個切片一個切片做。

【專案】M5StickS3（ESP32-S3）桌寵「小S」。程式在 ~/Developer/m5sticks3，公開 repo MilesYP/m5sticks3。
開發者 Miles 是 PM、不親手寫 code；你負責出可編譯的程式、解釋術語、帶他用 VS Code 的 PlatformIO 按鈕（Build ✓ / Upload → / Serial Monitor 🔌）燒錄驗收。回覆用繁體中文、術語要解釋。

【先讀這些】
- CLAUDE.md：工程鐵則（**鐵則六是 11-18 新增的音訊規則**）、已驗證腳位、按鍵配置。
- docs/PROGRESS.md：跨 session 進度，讀最上面 2~3 筆。
- docs/StickS3 執行計劃.md：「切片拆分」一節有所有里程碑的切片表。
- docs/M1B-架構.md：語音中控架構與 WebSocket 協定。
- cloud/src/worker.js、cloud/src/voice_session.js、firmware/src/net_hub.*。

【目前狀態（2026-11-18）】
- M1-B 已完成 B1、B2a、B2b、B3a、B3b-1。中控 https://sticks3-hub.yenpang.workers.dev（/ws、/test、/health），在 cloud/，用 `wrangler deploy` 部署。
- B3b-1（本機錄放音）剛驗收通過。

【這一片：B3.5 連線 token 驗證】
問題：中控網址寫在公開 repo，/ws 沒有任何驗證。B3c 接上 OpenAI 後，任何人連上就能花 Miles 的 API 額度。
目標：裝置連 /ws 要帶 token，中控比對不符就在升級成 WebSocket 之前拒絕（回 401），不建立連線、不進 Durable Object。
要先和 Miles 確認的設計點（列出取捨再讓他選）：
1. token 放哪：WebSocket 網址的 query string（例 `/ws?t=...`，簡單，但可能出現在 Cloudflare 的請求紀錄裡），或 HTTP header（裝置端 links2004 WebSockets 可用 `setExtraHeaders` 加；但瀏覽器的 WebSocket 不能自訂 header，/test 頁就得改用 query 或 subprotocol）。
2. /test 頁怎麼帶 token：頁面上加輸入框讓 Miles 手動貼，**不可把 token 寫進頁面原始碼**。
3. 比對要用固定時間比較（避免從回應時間猜出 token）。
機密規則：
- 中控端：`wrangler secret put HUB_TOKEN`（Miles 自己貼值）。
- 裝置端：`firmware/src/secrets.h` 加 `HUB_TOKEN`（git 忽略），`secrets.h.example` 加佔位字串。
- token 產生方式給 Miles 一行指令即可（例如 `openssl rand -hex 24`）。**絕不印出、不進 repo、不叫他貼給你**。
驗收：(1) 正確 token 的裝置能連、能照常按 A 切表情；(2) /test 頁不填或填錯 token 會被拒；(3) 序列埠看得到被拒時的錯誤訊息，裝置會照常每 5 秒重連。

【這片之後】
- B3b-2：裝置音訊以 WebSocket 二進位幀串給中控，中控原封不動回傳讓裝置播放（純傳輸測試）。Miles 已選「方案 A：放開 A 後整段上傳」，通過再改邊錄邊傳。每幀 100ms（1600 樣本、3200 bytes），16kHz PCM16 單聲道。
- B3c：中控接 OpenAI 即時語音（模型 `gpt-live-1`，`wss://api.openai.com/v1/realtime`；**11-19 更正：GPT-Live 端點是 `wss://api.openai.com/v1/live/sessions`，見 `docs/M1B-架構.md` 第 9 節**）。**實作前先查官方文件確認音訊格式**：官方範例是 24kHz PCM16，裝置是 16kHz，重取樣放中控還是裝置要先定案。

【OpenAI / 費用】
- Miles 是 ChatGPT Plus（不是 Pro），沒有免費 API 額度，走 pay-as-you-go，已儲值 US$10。
- OPENAI_API_KEY 已用 `wrangler secret put` 存在 Worker，絕不要印出、不要進 repo、不要叫他貼給你。

【工作規則】
- 每個切片：先說要改哪些檔＋計畫 → Miles 同意 → 寫 → Build/Upload（cloud 用 `wrangler deploy`）→ 實機／/test 驗收 → git commit → 下一片。
- 機密只進 secrets.h / wrangler secret，不進 repo。
- 做完更新 docs/PROGRESS.md（最上面加一筆、改「目前狀態」）與 docs/StickS3 執行計劃.{md,html} 的切片狀態。
- Miles 習慣用 VS Code 按鈕而非終端機指令（wrangler 例外）。
- 改音訊相關程式前先看 CLAUDE.md 鐵則六；問題先量測再改，不要照原始碼推理就動手。
