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
        // B1 模擬管線（尚未接 OpenAI）
        this.send(ws, { t: "state", v: "thinking" });

        const turn = {
          ts: Date.now(),
          user: "(模擬音訊)",
          assistant: "收到，這是 B1 中控的模擬回覆。",
        };
        await this.appendTurn(turn);

        this.send(ws, { t: "text", role: "assistant", s: turn.assistant });
        this.send(ws, { t: "state", v: "speaking" });
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
