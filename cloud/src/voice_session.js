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
// B3b-4：邊收邊播。
//   - 新增 down=adpcm8k（先降到 8kHz 再 ADPCM，約 4KB/s），裝置預設用這個。
//   - stop 後一則一則送：pace=fast（預設，有多快送多快，模擬 OpenAI 產生得比即時快）
//     或 pace=rt（照真實播放速度送，最嚴苛的壓力測試）。新一輪 start 會取消還沒送完的上一輪。
//   - 音量正規化從裝置移到這裡（裝置邊收邊播拿不到整段峰值）；audio_end 帶增益 g 與音訊長度 ms。
//   - 連線關閉／錯誤記到 console（wrangler tail 看得到），量連線重置用。
//   - 這一輪的格式（up/down/pace）存進連線附件（serializeAttachment）：休眠式 WebSocket 閒置約 10 秒
//     DO 會被移出記憶體、this.audio 會清空，但連線與附件還在。11-19 實測按住 A 10 秒沒送任何訊息
//     → DO 休眠 → 中控忘了格式、照 pcm16 回傳 → 裝置當 ADPCM 解 → 大爆音。

// B3b-5：上傳改 μ-law 邊錄邊傳、錄音不設上限（Miles 定案）。
//   - start 多帶 id（每次按 A 遞增）；裝置斷線重連後用同一個 id 重送 start 再從頭重送，這裡照樣清空重收。
//   - up=mulaw：收到的是 μ-law（16kHz、每取樣 1 byte），stop 時解回 PCM16 再處理。
//   - audio_end 的 pcm＝收到的 PCM16 bytes，裝置拿來核對「送出＝收到」。
//   - 最近一段錄音（最多 60 秒）存進 storage，給 B3c 先導測試用。
// 每輪上限 64MB（μ-law 約 70 分鐘）：等於沒有上限，只防記憶體被塞爆（DO 記憶體 128MB）
const MAX_AUDIO_BYTES = 64 * 1024 * 1024;
const CLIP_MAX_SAMPLES = 16000 * 60;     // 存最近 60 秒
const CLIP_CHUNK_BYTES = 100 * 1024;     // storage 單一值上限 128KiB，切 100KB 一塊
import { runLab } from "./lab.js";
import { GptLink } from "./gpt_turn.js";
import { InfoHub } from "./info.js";
import { concatPcm16, concatU8, decodeMulaw, encodeDown, normalizePcm } from "./adpcm.js";

// B3b-3：裝置↔中控的音訊格式（start 帶 up／down，中控回 format 確認）
//   up：裝置上傳格式（目前只支援 pcm16＝16kHz PCM16 LE）
//   down：中控回傳格式 pcm16 或 adpcm（IMA ADPCM，見 adpcm.js）
const UP_FORMATS = ["pcm16", "mulaw"];
const DOWN_FORMATS = ["pcm16", "adpcm", "mulaw", "adpcm8k"];
const PACES = ["fast", "rt"];

export class VoiceSession {
  constructor(state, env) {
    this.state = state;
    this.env = env;
    this.storage = state.storage;
    // 每條連線一份音訊暫存。只在「一輪對話」這幾秒內存在；
    // DO 在一連串訊息之間不會休眠，暫存放記憶體即可（不寫 storage）。
    this.audio = new WeakMap();   // ws → { chunks: ArrayBuffer[], bytes, overflow, up, down, pace }
    this.turn = new WeakMap();    // ws → 目前這一輪的序號；送音訊途中序號變了＝被新一輪取消
    this.turnSeq = 0;
    // B3c：mode=gpt 的對話（GPT-Live 連線、上下文、額度）。有外連時 DO 不會休眠，這份物件留在記憶體。
    // A5-2：資訊頁雲端資料（天氣、快訊、美金額度）；broadcast＝送給所有連著的裝置
    this.info = new InfoHub(this.storage, env, (obj) => this.broadcast(obj));
    this.gpt = new GptLink(this.storage, env, (ws, obj) => this.send(ws, obj), this.info);
  }

  async fetch(request) {
    if (new URL(request.url).pathname === "/lab") return runLab(request, this.env, this.storage);   // B3c 先導測試
    if (new URL(request.url).pathname === "/persona") return this.persona(request);              // Friday 記憶
    if (new URL(request.url).pathname === "/cron") {                                              // A5-2：Cron Trigger
      const n = await this.info.refresh(false);
      return new Response("refreshed " + n + "\n");
    }
    if (new URL(request.url).pathname === "/info") return this.infoPage(request);                 // A5-2：查快取／強制更新
    if (request.headers.get("Upgrade") !== "websocket") {
      return new Response("expected websocket", { status: 426 });
    }
    const pair = new WebSocketPair();
    const client = pair[0];
    const server = pair[1];
    this.state.acceptWebSocket(server); // 休眠式：連線保持、閒置不計資源
    try { server.serializeAttachment({ t0: Date.now() }); } catch {}   // B3b-4：記連線開始時間（休眠後仍在）
    console.log("[ws] open");
    // A5-2：裝置連上 → 先送快取的資訊頁資料，再看要不要更新（過期才抓）
    setTimeout(() => this.sendInfo(server).then(() => this.info.refresh(false)).catch((e) => console.log("[info] " + e.message)), 300);
    return new Response(null, { status: 101, webSocket: client });
  }

  // ── 休眠 WebSocket 事件處理 ──
  async webSocketMessage(ws, message) {
    if (typeof message !== "string") {
      if (this.modeOf(ws) === "gpt") return this.gpt.onAudio(ws, message);   // B3c
      // B3b-2：二進位音訊幀 → 暫存，等 stop 再原樣傳回
      let buf = this.audio.get(ws);
      if (!buf) { buf = this.newTurn(ws); this.audio.set(ws, buf); }   // 休眠過：從附件找回這一輪的格式
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
        const pace = PACES.includes(msg.pace) ? msg.pace : "fast";
        // B3c：mode=gpt 接 GPT-Live；不帶或其他值＝echo（B3b-5 原樣回傳，回歸測試用；舊版韌體不帶 mode）
        const mode = msg.mode === "gpt" ? "gpt" : "echo";
        this.turn.set(ws, 0);   // 取消上一輪還沒送完的音訊
        console.log("[turn] start id=" + (msg.id ?? "-") + " mode=" + mode + " up=" + up + " down=" + down + " pace=" + pace);
        if (mode === "gpt") {
          // GPT 模式下載：串流 ADPCM 8kHz（預設）／ADPCM 16kHz／μ-law 16kHz（11-20 加，音質比較）
          const gdown = ["adpcm8k", "adpcm", "mulaw"].includes(down) ? down : "adpcm8k";
          this.saveFmt(ws, { up, down: gdown, pace, mode });
          this.audio.delete(ws);
          this.send(ws, { t: "format", up, down: gdown, pace, mode });
          if (await this.gpt.onStart(ws, msg, { up, down: gdown })) this.send(ws, { t: "state", v: "listening" });
          break;
        }
        this.saveFmt(ws, { up, down, pace, mode });
        this.audio.set(ws, { chunks: [], bytes: 0, overflow: false, up, down, pace });
        this.send(ws, { t: "format", up, down, pace });
        this.send(ws, { t: "state", v: "listening" });
        break;
      }

      case "stop": {
        if (this.modeOf(ws) === "gpt") { this.gpt.onStop(ws); break; }   // B3c
        // B3b-2：純傳輸測試——收到的音訊傳回（B3b-4：正規化後一則一則送，裝置邊收邊播）
        this.send(ws, { t: "state", v: "thinking" });
        const buf = this.audio.get(ws) || this.newTurn(ws);
        this.audio.delete(ws);
        const turn = ++this.turnSeq;
        this.turn.set(ws, turn);
        const pcm = await this.streamOut(ws, buf, turn);
        if (pcm) await this.saveClip(pcm);
        break;
      }

      case "ping":
        this.send(ws, { t: "pong" });
        break;

      case "info_req":   // A5-2：裝置序列埠 a → 先回快取，再強制更新（60 秒內最多一次）
        await this.sendInfo(ws);
        this.info.forceRefresh().catch((e) => console.log("[info] " + e.message));
        break;

      case "history": {
        // B3c：GPT 模式的上下文（最近 50 輪逐字稿）
        const ctx = await this.gpt.history();
        this.send(ws, { t: "history", turns: ctx.slice(-5) });
        break;
      }

      default:
        this.send(ws, { t: "error", s: "unknown type: " + msg.t });
    }
  }

  async webSocketClose(ws, code, reason, wasClean) {
    console.log("[ws] close code=" + code + " clean=" + wasClean + " reason=" + JSON.stringify(reason || "") + " 連線 " + this.aliveSec(ws) + " s");
    try {
      ws.close(code, "server closing");
    } catch {}
  }

  async webSocketError(ws, err) {
    // 記錄即可；休眠連線出錯會自動清掉（B3b-4：印出來找連線重置原因）
    console.log("[ws] error " + (err && err.message ? err.message : String(err)) + " 連線 " + this.aliveSec(ws) + " s");
  }

  // 連線附件：{ t0, fmt:{up,down,pace} }。休眠後 this.audio 會清空，附件不會。
  attach(ws) {
    try { return ws.deserializeAttachment() || {}; } catch { return {}; }
  }
  saveFmt(ws, fmt) {
    try { ws.serializeAttachment({ ...this.attach(ws), fmt }); } catch {}
  }
  modeOf(ws) {
    const f = this.attach(ws).fmt;
    return f && f.mode === "gpt" ? "gpt" : "echo";
  }
  newTurn(ws) {
    const f = this.attach(ws).fmt || { up: "pcm16", down: "pcm16", pace: "fast" };
    if (this.attach(ws).fmt) console.log("[ws] DO 休眠後恢復這一輪的格式 down=" + f.down);
    return { chunks: [], bytes: 0, overflow: false, up: f.up, down: f.down, pace: f.pace };
  }

  aliveSec(ws) {
    try {
      const a = ws.deserializeAttachment();
      return a && a.t0 ? ((Date.now() - a.t0) / 1000).toFixed(1) : "?";
    } catch { return "?"; }
  }

  // B3b-4：把這輪音訊正規化、編碼，一則一則送出。pace=rt 時照音訊長度等待（真實速度）。
  async streamOut(ws, buf, turn) {
    let msgs = [], durs = [], gain = 1, pcmBytes = 0, raw = null;
    if (!buf.overflow) {
      const pcm = buf.up === "mulaw" ? decodeMulaw(concatU8(buf.chunks)) : concatPcm16(buf.chunks);
      pcmBytes = pcm.length * 2;
      raw = pcm.slice(0, Math.min(pcm.length, CLIP_MAX_SAMPLES));   // 正規化之前留一份（B3c 先導測試用）
      gain = normalizePcm(pcm);
      ({ msgs, durs } = encodeDown(pcm, buf.down));
      console.log("[turn] stop：收到 " + buf.bytes + " bytes（" + buf.up + "）＝ " + (pcm.length / 16000).toFixed(1) + " 秒");
    }
    const t0 = Date.now();
    let sent = 0, audioMs = 0;
    for (let i = 0; i < msgs.length; i++) {
      if (this.turn.get(ws) !== turn) return null;   // 被新一輪取消：不送 audio_end
      try { ws.send(msgs[i]); } catch { break; }
      sent += msgs[i].byteLength;
      audioMs += durs[i];
      if (buf.pace === "rt" && i + 1 < msgs.length) {
        const wait = t0 + audioMs - Date.now();
        if (wait > 0) await this.sleep(wait);
      }
    }
    if (this.turn.get(ws) !== turn) return null;
    this.send(ws, { t: "audio_end", n: sent, fmt: buf.down, pcm: pcmBytes, g: Math.round(gain * 100) / 100, ms: Math.round(audioMs) });
    this.send(ws, { t: "state", v: "idle" });
    return raw;
  }

  // B3b-5：最近一段錄音（PCM16 16kHz，未正規化）存進 storage，B3c 先導測試直接拿來餵 GPT-Live
  async saveClip(pcm) {
    try {
      const bytes = new Uint8Array(pcm.buffer, pcm.byteOffset, pcm.byteLength);
      const entries = {};
      let n = 0;
      for (let o = 0; o < bytes.length; o += CLIP_CHUNK_BYTES) {
        entries["clip:" + n] = bytes.slice(o, Math.min(o + CLIP_CHUNK_BYTES, bytes.length));
        n++;
      }
      entries["clip:meta"] = { n, samples: pcm.length, t: Date.now() };
      await this.storage.put(entries);
      const old = [];
      for (let k = n; k < 20; k++) old.push("clip:" + k);
      await this.storage.delete(old);
    } catch (e) {
      console.log("[clip] 存錄音失敗 " + (e && e.message ? e.message : String(e)));
    }
  }

  // Friday 的記憶（B3c 補，11-20）：PUT /persona?name=friday|miles 上傳 markdown（≤16KB），GET 看目前內容長度。
  // 開新 GPT-Live session 時接在人設後面（gpt_turn.js）。改了重新上傳即可，不用重新部署。
  async persona(request) {
    const url = new URL(request.url);
    const name = url.searchParams.get("name");
    if (request.method === "PUT") {
      if (!["friday", "miles"].includes(name)) return new Response("name 只能是 friday 或 miles\n", { status: 400 });
      const text = await request.text();
      if (text.length > 16000) return new Response("太長（上限 16000 字元）\n", { status: 413 });
      await this.storage.put("persona:" + name, text);
      return new Response(`已更新 ${name}：${text.length} 字元（下一次開新對話生效；目前的對話閒置 30 秒關閉後就會用新的）\n`);
    }
    const f = (await this.storage.get("persona:friday")) || "";
    const m = (await this.storage.get("persona:miles")) || "";
    return new Response(`friday：${f.length} 字元｜miles：${m.length} 字元\n`);
  }

  // ── A5-2 資訊頁資料 ──
  async sendInfo(ws) {
    for (const m of await this.info.cachedMsgs()) this.send(ws, m);
  }
  broadcast(obj) {
    for (const ws of this.state.getWebSockets()) this.send(ws, obj);
  }
  // GET /info?t=…（看快取）、/info?t=…&refresh=1（強制更新）：部署後驗證資料來源用
  async infoPage(request) {
    const url = new URL(request.url);
    if (url.searchParams.get("refresh") === "1") await this.info.refresh(true);
    const [wx, news, costs] = await Promise.all([this.storage.get("info:wx"), this.storage.get("info:news"), this.storage.get("info:costs")]);
    const usd = await this.info.usdMsg();
    const body = { usd, wx, news, costs, devices: this.state.getWebSockets().length };
    return new Response(JSON.stringify(body, null, 2) + "\n", { headers: { "content-type": "application/json; charset=utf-8" } });
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
          { role: "system", content: "你是 M5S3Pack 裡的語音助理 Friday，用繁體中文、簡短親切口語回應。" },
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
