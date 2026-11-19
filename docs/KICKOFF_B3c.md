# 接手啟動 prompt：M1-B / B3c 中控接 GPT-Live

> 用法：開新的 Claude Code（或 Cowork）session，把下面整段貼進去。
> 產生於 2026-11-19（B3b-5 驗收通過之後）。**本片做法已在 11-19 定案，照做、不再發散**；只有先導測試的數字要量，量完照下面的判定規則直接套用。

---

我在接手 M5StickS3 桌寵「小S」專案（~/Developer/m5sticks3，公開 repo MilesYP/m5sticks3）。這一片是 **M1-B / B3c 中控接 OpenAI GPT-Live**：對它說一句話，它用語音回答。
我是 PM、不親手寫 code；燒錄用 VS Code 的 PlatformIO 按鈕，中控用 `cd ~/Developer/m5sticks3/cloud && npx wrangler deploy`。回覆用繁體中文、術語要解釋。技術選擇請直接定，真的無法判斷才問我；動工前給一次計畫與要改的檔案，我說「開始實作」才寫。Cowork／橋接環境不能 git push（commit 前先請我允許刪檔），commit 後給我一行 `cd ~/Developer/m5sticks3 && git push`。橋接環境無法完整編譯：韌體用模擬標頭做語法檢查、純邏輯用 g++／node 測。

【先讀】
- CLAUDE.md：鐵則六、**鐵則七（第 9 點 DO 休眠與附件、第 10 點邊收邊播、第 11 點網路 task）**、第 8 節機密
- docs/PROGRESS.md 最上面 2 筆（最新是 B3b-5）
- docs/StickS3 執行計劃.md 的 B3c、B4 兩列（定案內容）；docs/M1B-架構.md 第 4 節（協定）
- cloud/src/voice_session.js、adpcm.js、worker.js；firmware/src/voice_turn.*、net_hub.*

【目前狀態】
- 裝置：按住 A 邊錄邊傳 μ-law 16kHz（每則 8000 bytes），`start` 帶 `id`/`up`/`down`/`pace`，放開送 `stop`；收中控 ADPCM 8kHz 邊收邊播；斷線從頭重送。不限錄音長度。
- 中控：目前是**原樣回傳**（echo）：收齊後正規化、轉 ADPCM 8kHz 回傳，`audio_end` 的 `pcm`＝收到的 PCM16 bytes（裝置拿來核對送出＝收到）。最近 60 秒錄音存在 DO storage：`clip:meta`＝{n, samples, t}、`clip:0..n-1`＝PCM16 16kHz（未正規化）每塊 100KB。
- OpenAI：pay-as-you-go，key 在 `wrangler secret` 的 `OPENAI_API_KEY`。

【GPT-Live 官方事實（11-19 查證，不要再假設別的）】
- 端點 `wss://api.openai.com/v1/live/sessions`；連上後第一則送 `session.start`（`session.model="gpt-live-1"`、`instructions`、`audio.format={type:"audio/pcm", rate:16000}`、`audio.output.voice`（預設 marin）、`input`＝歷史文字訊息），等 `session.started`（含 `expires_at`）。格式整個 session 固定、輸入輸出同格式。
- 送音：`session.input_audio.append`（`audio`＝base64 原始 bytes，**沒有確認回應**）。官方建議照錄音速度持續送（整檔一次灌不等於即時麥克風）。
- 收音：`session.output_audio.delta`（base64）。**沒有「回答講完」事件、沒有時間欄位**。
- 逐字稿：`session.input_transcript.delta`／`session.output_transcript.delta`，**都沒有 done**。
- **沒有「使用者講完」的指令**（模型自己判斷輪替）。`session.input_audio.mute`／`unmute` 可控制輸入（mute 不會停止已在產生的語音）。
- 結束：送 `session.close`，等 `session.closed`（reason 含 `expired`、`close_requested`、`connection_lost` 等）。用量：`session.usage.updated`。

【定案做法（照做）】
1. **連線**：DO 用 `fetch("https://api.openai.com/v1/live/sessions", {headers:{Upgrade:"websocket", Authorization:"Bearer "+key}})` 取得 `resp.webSocket` 再 `accept()`。**裝置按 A（`start`）才開 session**；回答完 **30 秒**沒新的 `start` 就 `session.close`。有外連時 DO 不會休眠；但 session 設定與對話紀錄仍存 storage／附件（鐵則七第 9 點）。
2. **上下文**：每輪把輸入／輸出逐字稿累積成文字，存 DO storage（只留最近 50 輪）；開新 session 時把最近 10 輪放進 `session.start` 的 `input`。`instructions`：「你是 StickS3 桌寵『小S』，用繁體中文、簡短口語回答；使用者是按住按鈕講話、放開才算講完。」session `expired` → 帶上下文自動重開。
3. **輸入**：μ-law 解回 PCM16 16kHz → base64 → `session.input_audio.append`；session 還沒 `started` 前先暫存。放開（`stop`）後中控補靜音讓模型判斷講完（長度依先導測試）。
4. **輸出**：`session.output_audio.delta` 解 base64 → 串流版 ADPCM 8kHz 編碼器（低通／降取樣／ADPCM 狀態要跨 delta 延續；**固定增益 1.0**，不做整段正規化）→ 每則 3 幀送裝置。**裝置還按著 A（還沒收到 `stop`）時模型先開口的音訊，中控扣住到 `stop` 之後才送**（ES8311 不能同時錄放）。
5. **回答結束**：輸出音訊停頓超過門檻（依先導測試）→ 送 `audio_end`（`pcm`＝這輪收到的上傳 PCM16 bytes、`g`=1、`ms`＝回覆長度）→ `state=idle`。
6. **模式欄位**：`start` 新增 `mode`：`gpt`（裝置預設）／`echo`（保留 B3b-5 原樣回傳，回歸測試用）；裝置序列埠 `m0`＝echo、`m1`＝gpt。`gpt` 模式裝置不印 SNR（回覆不是原音），完整性核對照舊。
7. **表情**：裝置維持本機決定（Listening → 放開 Thinking → 開始出聲 Speaking → 播完 Idle），不依中控 state。
9. **每日額度（11-19 加）**：DO storage 記當天（Asia/Taipei 日期）`{turns, audioSec}`（音訊秒數＝上傳＋回覆）；上限讀 wrangler 變數 `DAILY_MAX_TURNS`（預設 200）、`DAILY_MAX_AUDIO_SEC`（預設 1800），寫進 `wrangler.toml` 的 `[vars]`（不是 secret）。`start` 時已超過 → 不開 OpenAI session，回 `{"t":"error","code":"quota"}` 再送 `audio_end` n=0；回答進行中才超過 → 這輪照常講完、下一輪才拒絕。echo 模式不計。裝置收到 `code=quota` 印「今日額度已用完」、表情回 Idle。
8. 斷線重送（甲）在 gpt 模式：中控收到同一 `id` 的 `start` → 關掉舊 session、開新 session（帶上下文）、重收整段。

【第一步：先導測試（只動中控，不需裝置）】
中控加一個 token 保護的測試端點（例如 `/lab?t=<HUB_TOKEN>&...`），用 storage 裡那段錄音直接跑 GPT-Live，回傳時間報告。量四件事，**判定規則已定，量完直接套用、不另外問**：
| 量什麼 | 判定 |
| :-- | :-- |
| A. 錄音送完後要補多長靜音，模型才開口 | 靜音長度＝實測＋0.5 秒，最多 3 秒 |
| B. 比即時快（例如 4 倍速）送入，回答是否正常（逐字稿對、開口時間合理） | 正常 → 全速送；不正常 → 照真實速度送 |
| C. 送到一半停 2 秒不送任何東西，模型會不會搶話 | 不會 → 上傳中斷時不送；會 → 中斷時 `mute`、補上後 `unmute`；兩者都不行 → 先收齊整段再照真實速度餵（保底） |
| D. 回答中 `output_audio.delta` 的最長間隔 | 回答結束門檻＝最長間隔＋0.5 秒 |
每項至少跑 2 次，報告印出：開口延遲、delta 間隔分佈、逐字稿、`session.usage.updated`。結果寫進 `docs/M1B-架構.md`。

【限制】
- 鐵則六第 8 點、鐵則七第 8 點：先量測再改；推測不可直接當結論改程式。
- 版本標記 `AUDIO_BUILD_TAG` 改 `21-b3c`。
- B4（文字顯示、嘴巴連動）是下一片，這片不做。B5／B6 已移待辦，不做。
- 鐵則三：急停優先；網路 task 規則（鐵則七第 11 點）不可破壞：WebSocket 只在網路 task 呼叫。

【機密】OPENAI_API_KEY、HUB_TOKEN 只在 `wrangler secret` 與 `firmware/src/secrets.h`（git 忽略），不印出、不讀值；`wrangler tail` 輸出含 token，貼之前要遮。commit 前確認 git status 沒有 secrets.h。

【驗收】
1. 先導測試報告四項有數字，並依規則定出參數。
2. 對它說一句中文問題（約 3 秒），它用語音回答；印出「放開到開始播」的時間。
3. 連續兩輪：第二輪問「我剛剛問了什麼」，它答得出來（上下文有帶）。
4. 講 30 秒以上的長問題也能回答；`m0` echo 模式仍正常（回歸）。
5. 回答中途 `x` 斷線：已收到的播完、這輪結束；下一輪正常。
6. 印出這幾輪的 `session.usage.updated` 用量。
7. 把 `DAILY_MAX_TURNS` 暫設 2：第 3 輪被拒絕、裝置印額度用完；改回後恢復。

【收尾】
通過後：CLAUDE.md 補 GPT-Live 規則（鐵則七新點）；PROGRESS.md 最上面加一筆、改「目前狀態」；執行計劃 md/html 把 B3c 標 ✅、下一片改 B4；寫 `docs/KICKOFF_B4.md`；git commit。
