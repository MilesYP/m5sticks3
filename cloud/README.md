# StickS3 中控（Cloudflare Worker + Durable Object）

M1-B 的雲端中控。裝置用一條常開 WebSocket 連到這裡；中控持 OpenAI 連線、存對話、做工具呼叫。
架構見 `../docs/M1B-架構.md`。目前進度：**B1 骨架**（WebSocket 協定＋存對話，尚未接 OpenAI）。

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

## 本機開發（可選）
```bash
npx wrangler dev           # 本機跑，印出 http://localhost:8787
```

## 測試 B1（不用板子、用網址）
部署後直接開： `https://sticks3-hub.<你的子網域>.workers.dev/test`
1. 按「連線」→ log 顯示「已連線」。
2. 按「長按 A(start)」→ 收到 `state=listening`。
3. 按「放開 A(stop)」→ 依序 thinking → 一句模擬回覆 → speaking → idle。
4. 按「看對話紀錄」→ 回傳剛存那筆。

## B3 才需要（先不用）
```bash
npx wrangler secret put OPENAI_API_KEY   # 貼上你的 OpenAI API key；只存在 Worker，不進 repo
```

## 協定（裝置 ↔ 中控）
裝置送：`{"t":"start"}` / 二進位音訊 / `{"t":"stop"}` / `{"t":"ping"}` / `{"t":"history"}`
中控送：`{"t":"state","v":"listening|thinking|speaking|idle"}` / `{"t":"text","role":"assistant","s":"…"}` / `{"t":"pong"}` / `{"t":"history","turns":[…]}`
