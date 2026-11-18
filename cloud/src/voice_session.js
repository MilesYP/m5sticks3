// VoiceSession — 一個使用者的語音對話 session（Durable Object）
//
// B1 範圍：把裝置↔中控的 WebSocket 協定跑通，並把對話存進 DO storage。
//   - 支援休眠 WebSocket（state.acceptWebSocket）：待機不佔資源。
//   - 收到 start → 回 state=listening。
//   - 收到 stop  → 模擬一輪：thinking → 存一筆對話 → 回覆文字 → speaking → idle。
//   - 二進位音訊幀：B1 先忽略（B3 接 OpenAI 時才處理）。
//
// B3b-2（純傳輸測試，先不接 OpenAI）：
//   - start → 清空這條連線的音訊暫存，回 state=listening。
//   - 二進位幀 → 依序暫存（每輪上限 MAX_AUDIO_BYTES，超過就丟掉這輪並回 error）。
//   - stop  → state=thinking → 把暫存的幀「原封不動」照順序傳回
//             → {"t":"audio_end","n":<bytes>} → state=idle。
//   B3a 的「stop 就呼叫 OpenAI 文字」已撤下；askOpenAI() 保留給 B3c。
// B3b-3：start 帶 {up, down} 格式 → 回 {"t":"format",up,down}；down=adpcm 時 stop 回傳的是
//   ADPCM 幀（4:1），audio_end 加上 fmt 與原始 PCM bytes（pcm）。

// 每輪最多收多少音訊：16kHz × 2 bytes × 6 秒 = 192000，留一點餘裕
const MAX_AUDIO_BYTES = 256 * 1024;
import { concatPcm16, encodeAdpcmFrames, encodeMulaw, packMessages, sliceBytes } from "./adpcm.js";

// B3b-3：裝置↔中控的音訊格式（start 帶 up／down，中控回 format 確認）
//   up：裝置上傳格式（目前只支援 pcm16＝16kHz PCM16 LE）
//   down：中控回傳格式 pcm16 或 adpcm（IMA ADPCM，見 adpcm.js）
const UP_FORMATS = ["pcm16"];
const DOWN_FORMATS = ["pcm16", "adpcm", "mulaw"];
// 下載每則 WebSocket 訊息的上限（合併以減少則數，見 adpcm.js packMessages）
const DOWN_MSG_BYTES = 8040;   // ＝10 個 ADPCM 幀；PCM／μ-law 也用同一上限，比較才公平

export class VoiceSession {
  constructor(state, env) {
    this.state = state;
    this.env = env;
    this.storage = state.storage;
    // 每條連線一份音訊暫存。只在「一輪對話」這幾秒內存在；
    // DO 在一連串訊息之間不會休眠，暫存放記憶體即可（不寫 storage）。
    this.audio = new WeakMap();   // ws → { chunks: ArrayBuffer[], bytes, overflow, up, down }
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
      // B3b-2：二進位音訊幀 → 暫存，等 stop 再原樣傳回
      let buf = this.audio.get(ws);
      if (!buf) { buf = { chunks: [], bytes: 0, overflow: false, up: "pcm16", down: "pcm16" }; this.audio.set(ws, buf); }
      if (buf.overflow) return;
      const n = message.byteLength;
      if (buf.bytes + n > MAX_AUDIO_BYTES) {
        buf.overflow = true;
        buf.chunks = [];
        return this.send(ws, { t: "error", s: "audio too large (max " + MAX_AUDIO_BYTES + " bytes)" });
      }
      buf.chunks.push(message);
      buf.bytes += n;
      return;
    }
    let msg;
    try {
      msg = JSON.parse(message);
    } catch {
      return this.send(ws, { t: "error", s: "bad json" });
    }

    switch (msg.t) {
      case "start": {
        // 不認得的格式一律退回 pcm16（舊版裝置不帶欄位也走這裡）
        const up = UP_FORMATS.includes(msg.up) ? msg.up : "pcm16";
        const down = DOWN_FORMATS.includes(msg.down) ? msg.down : "pcm16";
        this.audio.set(ws, { chunks: [], bytes: 0, overflow: false, up, down });
        this.send(ws, { t: "format", up, down });
        this.send(ws, { t: "state", v: "listening" });
        break;
      }

      case "stop": {
        // B3b-2：純傳輸測試——收到的音訊原封不動傳回
        this.send(ws, { t: "state", v: "thinking" });
        const buf = this.audio.get(ws) || { chunks: [], bytes: 0, overflow: false, up: "pcm16", down: "pcm16" };
        this.audio.delete(ws);
        // B3b-3：down=adpcm 時把收到的 PCM 壓成 ADPCM 幀再送；pcm16 則原樣回傳
        let out = [];
        if (!buf.overflow) {
          const pcm = concatPcm16(buf.chunks);
          if (buf.down === "adpcm") out = packMessages(encodeAdpcmFrames(pcm), DOWN_MSG_BYTES);
          else if (buf.down === "mulaw") out = sliceBytes(encodeMulaw(pcm), DOWN_MSG_BYTES);
          else out = sliceBytes(new Uint8Array(pcm.buffer), DOWN_MSG_BYTES);   // 8040 是偶數，不會切斷取樣
        }
        let sent = 0;
        for (const c of out) {
          try { ws.send(c); sent += c.byteLength; } catch { break; }
        }
        this.send(ws, { t: "audio_end", n: sent, fmt: buf.down, pcm: buf.bytes });
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
