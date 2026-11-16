// VoiceSession — 一個使用者的語音對話 session（Durable Object）
//
// B1 範圍：把裝置↔中控的 WebSocket 協定跑通，並把對話存進 DO storage。
//   - 支援休眠 WebSocket（state.acceptWebSocket）：待機不佔資源。
//   - 收到 start → 回 state=listening。
//   - 收到 stop  → 模擬一輪：thinking → 存一筆對話 → 回覆文字 → speaking → idle。
//   - 二進位音訊幀：B1 先忽略（B3 接 OpenAI 時才處理）。
export class VoiceSession {
  constructor(state, env) {
    this.state = state;
    this.env = env;
    this.storage = state.storage;
  }

  async fetch(request) {
    if (request.headers.get("Upgrade") !== "websocket") {
      return new Response("expected websocket", { status: 426 });
    }
    const pair = new WebSocketPair();
    const client = pair[0];
    const server = pair[1];
    this.state.acceptWebSocket(server); // 休眠式：連線保持、閒置不計資源
    return new Response(null, { status: 101, webSocket: client });
  }

  // ── 休眠 WebSocket 事件處理 ──
  async webSocketMessage(ws, message) {
    if (typeof message !== "string") {
      // B1：忽略二進位音訊（B3 OpenAI 接上後才轉送）
      return;
    }
    let msg;
    try {
      msg = JSON.parse(message);
    } catch {
      return this.send(ws, { t: "error", s: "bad json" });
    }

    switch (msg.t) {
      case "start":
        this.send(ws, { t: "state", v: "listening" });
        break;

      case "stop": {
        // B3a：真的呼叫 OpenAI（文字），確認 key / 額度 / 模型可用
        this.send(ws, { t: "state", v: "thinking" });

        let reply;
        try {
          reply = await this.askOpenAI(
            "使用者按住按鈕說了一句話（B3a 階段還沒串真實音訊）。" +
            "請用繁體中文、親切口語、一到兩句話回應，像個桌上語音小助理。"
          );
        } catch (e) {
          reply = "OpenAI 呼叫失敗：" + e.message;
        }

        const turn = { ts: Date.now(), user: "(語音 B3a)", assistant: reply };
        await this.appendTurn(turn);

        this.send(ws, { t: "text", role: "assistant", s: reply });
        this.send(ws, { t: "state", v: "speaking" });
        await this.sleep(1200);
        this.send(ws, { t: "state", v: "idle" });
        break;
      }

      case "ping":
        this.send(ws, { t: "pong" });
        break;

      case "history": {
        const log = (await this.storage.get("log")) || [];
        this.send(ws, { t: "history", turns: log });
        break;
      }

      default:
        this.send(ws, { t: "error", s: "unknown type: " + msg.t });
    }
  }

  async webSocketClose(ws, code, reason, wasClean) {
    try {
      ws.close(code, "server closing");
    } catch {}
  }

  async webSocketError(ws, err) {
    // 記錄即可；休眠連線出錯會自動清掉
  }

  // ── 工具 ──
  async askOpenAI(userText) {
    if (!this.env.OPENAI_API_KEY) {
      throw new Error("沒有設定 OPENAI_API_KEY（wrangler secret put）");
    }
    const r = await fetch("https://api.openai.com/v1/chat/completions", {
      method: "POST",
      headers: {
        authorization: "Bearer " + this.env.OPENAI_API_KEY,
        "content-type": "application/json",
      },
      body: JSON.stringify({
        model: "gpt-4o-mini", // B3a 測試用便宜模型；B3b 改 GPT-Live 語音
        messages: [
          { role: "system", content: "你是 StickS3 桌寵『小S』，用繁體中文、簡短親切口語回應。" },
          { role: "user", content: userText },
        ],
      }),
    });
    if (!r.ok) {
      const t = await r.text();
      throw new Error("HTTP " + r.status + " " + t.slice(0, 300));
    }
    const data = await r.json();
    const msg = data.choices && data.choices[0] && data.choices[0].message;
    return (msg && msg.content && msg.content.trim()) || "(OpenAI 回了空內容)";
  }

  sleep(ms) {
    return new Promise((r) => setTimeout(r, ms));
  }

  send(ws, obj) {
    try {
      ws.send(JSON.stringify(obj));
    } catch {}
  }

  async appendTurn(turn) {
    const log = (await this.storage.get("log")) || [];
    log.push(turn);
    while (log.length > 50) log.shift(); // 只留最近 50 輪
    await this.storage.put("log", log);
  }
}
