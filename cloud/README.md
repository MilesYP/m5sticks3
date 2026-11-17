# StickS3 中控（Cloudflare Worker + Durable Object）

M1-B 的雲端中控。裝置用一條常開 WebSocket 連到這裡；中控持 OpenAI 連線、存對話、做工具呼叫。
架構見 `../docs/M1B-架構.md`。目前進度：B1～B3a 完成（WebSocket 協定、存對話、OpenAI 文字回覆），**B3.5 起 `/ws` 需要 token**。

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
3. 按「放開 A(stop)」→ 依序 thinking → 一句模擬回覆 → speaking → idle。
4. 按「看對話紀錄」→ 回傳剛存那筆。

## OpenAI 金鑰（B3a 起已設定）
```bash
npx wrangler secret put OPENAI_API_KEY   # 貼上你的 OpenAI API key；只存在 Worker，不進 repo
```

## 協定（裝置 ↔ 中控）
連線：`wss://…workers.dev/ws?t=<HUB_TOKEN>`
裝置送：`{"t":"start"}` / 二進位音訊 / `{"t":"stop"}` / `{"t":"ping"}` / `{"t":"history"}`
中控送：`{"t":"state","v":"listening|thinking|speaking|idle"}` / `{"t":"text","role":"assistant","s":"…"}` / `{"t":"pong"}` / `{"t":"history","turns":[…]}`
