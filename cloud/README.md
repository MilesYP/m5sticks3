# StickS3 中控（Cloudflare Worker + Durable Object）

M1-B 的雲端中控。裝置用一條常開 WebSocket 連到這裡；中控持 OpenAI 連線、存對話、做工具呼叫。
架構見 `../docs/M1B-架構.md`。目前進度：**M1-B 完成**（11-20，B3c＋B4 接 GPT-Live，助理 Friday）；以下為歷史紀錄——B1～B3b-3 完成（WebSocket 協定、存對話、token 驗證、音訊原樣回傳、下載壓縮 μ-law／ADPCM）；**B3.5 起 `/ws` 需要 token**。B3b-2 起 `stop` 不再呼叫 OpenAI（`askOpenAI()` 留給 B3c）。

## 一次性準備
```bash
cd cloud
npm install                # 裝 wrangler（Cloudflare CLI）
npx wrangler login         # 開瀏覽器授權你的 CF 帳號
```

## 部署（會自動建立 Worker 專案）
```bash
npx wrangler deploy
```
部署完會印出網址，像 `https://sticks3-hub.<你的子網域>.workers.dev`。
WebSocket 位址＝把它改成 `wss://…workers.dev/ws`。

## 連線 token（B3.5，必做）
`/ws` 必須帶 `?t=<HUB_TOKEN>`，不符回 401（不建立連線）；中控沒設 `HUB_TOKEN` 時一律回 503。
同一組 token 要同時存在「中控 secret」與「裝置 `firmware/src/secrets.h`」。在 Mac 終端機整段貼上執行（token 不會顯示在畫面上，最後會放進剪貼簿讓你另存）：
```bash
cd ~/Developer/m5sticks3 && TOKEN=$(openssl rand -hex 24) && printf %s "$TOKEN" | (cd cloud && npx wrangler secret put HUB_TOKEN) && sed -i '' '/HUB_TOKEN/d' firmware/src/secrets.h && echo "#define HUB_TOKEN \"$TOKEN\"" >> firmware/src/secrets.h && printf %s "$TOKEN" | pbcopy && unset TOKEN && grep -cE '^#define HUB_TOKEN "[0-9a-f]{48}"$' firmware/src/secrets.h
```
最後印出 `1` 才算成功。之後要 `npx wrangler deploy`（中控）並重新燒錄韌體（裝置）。換 token 也是重跑這一行。

> ⚠️ token 在網址裡：`npx wrangler tail` 會印出完整網址。**把 tail 輸出貼給別人（含 AI）前，先遮掉 `?t=` 後面那串。**

## 本機開發（可選）
```bash
npx wrangler dev           # 本機跑，印出 http://localhost:8787
```

## 用 /test 頁測試（不用板子、用網址）
部署後直接開： `https://sticks3-hub.<你的子網域>.workers.dev/test`
0. 在密碼框貼上 HUB_TOKEN；勾「記住在這個瀏覽器」下次會自動帶入（只存在這台電腦的這個瀏覽器，「清除」可刪掉）。不填或填錯 → 顯示「連線被拒」。
1. 按「連線」→ log 顯示「已連線」。
2. 按「長按 A(start)」→ 收到 `state=listening`。
3. 按「放開 A(stop)」→ 依序 thinking → `{"t":"audio_end","n":0,…}`（/test 頁不送音訊）→ idle。
4. 按「看對話紀錄」→ 回傳剛存那筆。

## OpenAI 金鑰（B3a 起已設定）
```bash
npx wrangler secret put OPENAI_API_KEY   # 貼上你的 OpenAI API key；只存在 Worker，不進 repo
```

## 協定（裝置 ↔ 中控）
連線：`wss://…workers.dev/ws?t=<HUB_TOKEN>`
裝置送：`{"t":"start","up":"pcm16","down":"mulaw|adpcm|pcm16"}` / 二進位音訊（PCM16 16kHz LE，3200 bytes 一幀）/ `{"t":"stop"}` / `{"t":"ping"}` / `{"t":"history"}`
中控送：`{"t":"format","up":…,"down":…}`（確認格式，不認得的退回 pcm16）/ `{"t":"state","v":"listening|thinking|speaking|idle"}` / 二進位音訊（依 down 格式，合併成每則 ≤8040 bytes）/ `{"t":"audio_end","n":<bytes>,"fmt":…,"pcm":<原始 bytes>}` / `{"t":"pong"}` / `{"t":"history","turns":[…]}`
編碼器在 `src/adpcm.js`（IMA ADPCM、μ-law），細節見 `../docs/M1B-架構.md` 第 4 節。


## GPT-Live（B3c＋B4，11-20）
- 部署前一次性：`npm install`（裝 `opencc-js` 簡轉繁）。
- 設定（`wrangler.toml` 的 `[vars]`，可用 `npx wrangler deploy --var 名稱:值` 暫時覆蓋）：`DAILY_MAX_USD`／`DAILY_MAX_TURNS`（每日額度，A5-2 起以美金為主）、`OUT_GAIN`（回覆音量倍數，預設 1）、`GPT_VOICE`（聲音，預設 `meridian`）。
- Friday 的記憶：改 `persona/friday.md`（進 repo）或 `persona/miles.local.md`（不進 repo，範本 `persona/miles.example.md`）後跑 `cloud/persona.sh`（不用重新部署；`cloud/persona.sh show` 看中控存的長度）。
- 先導測試：`cloud/lab.sh info|pin|all|more`（報告存 `docs/lab/`，不進 repo）。
- 協定（`mode`、`text`、`error`、`usage`）見 `../docs/M1B-架構.md` 第 4 節「B3c＋B4 協定」。

## 資訊頁雲端資料（A5-2，11-20）：天氣、快訊、語音額度（美金）
程式在 `src/info.js`；Cron Trigger 每 30 分鐘更新（`wrangler.toml` 的 `[triggers]`），裝置連上、每輪對話結束、換日時也會送。

**一次性設定（secret 都不進 repo；指令先複製貼到終端機，停在提示時才去複製 key，畫面不顯示）**
```bash
# 1. 中央氣象署授權碼（opendata.cwa.gov.tw → 會員 → API授權碼）
cd ~/Developer/m5sticks3/cloud && read -rs -p "貼上授權碼後按 Enter：" K && echo && printf '%s' "$K" | npx wrangler secret put CWA_KEY; unset K
# 2. 天氣地點：縣市＋鄉鎮區，例「臺北市中正區」（個人資料，所以也用 secret）
cd ~/Developer/m5sticks3/cloud && read -r -p "地點（例 臺北市中正區）：" K && printf '%s' "$K" | npx wrangler secret put WX_TOWN; unset K
# 3. OpenAI 管理員金鑰（platform.openai.com/settings/organization/admin-keys 建立，只給讀取權限即可）：查餘額用
cd ~/Developer/m5sticks3/cloud && read -rs -p "貼上 admin key 後按 Enter：" K && echo && printf '%s' "$K" | npx wrangler secret put OPENAI_ADMIN_KEY; unset K
```
三個都是選配：沒設 `CWA_KEY` 就沒天氣；沒設 `OPENAI_ADMIN_KEY` 時餘額＝`CREDIT_USD`－中控自己記的花費（只算這台裝置的語音）。

**變數（`wrangler.toml` 的 `[vars]`，改完 `npx wrangler deploy`）**：`DAILY_MAX_USD`（每日上限，美金）、`CREDIT_USD`／`CREDIT_SINCE`（儲值總額與起算日，UTC；儲值後把金額加上去）、`LIVE_USD_PER_MIN` 等價格、`NEWS_FEEDS`（中央社 RSS）。

**驗證**：`https://sticks3-hub.<子網域>.workers.dev/info?t=<HUB_TOKEN>` 看快取（加 `&refresh=1` 強制更新）。網址含 token，只在自己的瀏覽器開。

**餘額怎麼算、誤差**：餘額＝`CREDIT_USD`－Costs API 從 `CREDIT_SINCE` 到昨天（UTC，台北早上 8 點換日）的花費－中控記的今天花費。Costs API 有延遲，剛過 UTC 換日那幾個小時「昨天」可能還沒結算完，餘額會高估幾分錢；中控自己記的今天花費是估計（GPT-Live 用 `usage.seconds`、上網查用搜尋次數與 token），跟後台可能差幾分錢。
