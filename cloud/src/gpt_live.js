// GPT-Live 連線包裝（M1-B / B3c）
//
// 官方事實（2026-11-19 查證 developers.openai.com）：
//   - 端點 wss://api.openai.com/v1/live/sessions（不帶 query），Authorization: Bearer <key>
//   - 第一則送 session.start，設定全部放在 session 物件裡：model、instructions、audio.format、
//     audio.output.voice、input（歷史文字：user＝input_text、assistant＝output_text），等 session.started
//   - 送音 session.input_audio.append（audio＝base64 原始 bytes，沒有確認回應），PCM 每塊要偶數 bytes
//   - 收音 session.output_audio.delta（delta＝base64）；沒有「回答講完」事件、沒有時間欄位
//   - 逐字稿 session.input_transcript.delta／session.output_transcript.delta（delta 欄位），沒有 done
//   - session.input_audio.mute／unmute（回 muted／unmuted），mute 不會停止已在產生的語音
//   - 結束：先等 session.closed 的處理掛好再送 session.close；約 15 秒沒來就當作未確認
//   - 計費照連線時間（約 $3／小時），session.usage.updated 是累計值，不能相加
// Cloudflare Worker 對外開 WebSocket：fetch() 帶 Upgrade 標頭 → resp.webSocket → accept()。
export const LIVE_URL = "https://api.openai.com/v1/live/sessions";
export const LIVE_MODEL = "gpt-live-1";
export const LIVE_RATE = 16000;   // 與裝置相同，不用重取樣

// ── base64 ↔ bytes（btoa 只吃字串，分段組，避免參數過多爆堆疊）──
export function b64FromBytes(u8) {
  let s = "";
  for (let i = 0; i < u8.length; i += 0x8000) s += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000));
  return btoa(s);
}
export function bytesFromB64(b64) {
  const s = atob(b64);
  const u = new Uint8Array(s.length);
  for (let i = 0; i < s.length; i++) u[i] = s.charCodeAt(i);
  return u;
}
// bytes（PCM16 LE）→ Int16Array（不保證對齊，逐取樣讀）
export function pcmFromBytes(u8) {
  const n = u8.length >> 1;
  const out = new Int16Array(n);
  for (let i = 0; i < n; i++) {
    const v = u8[2 * i] | (u8[2 * i + 1] << 8);
    out[i] = v >= 0x8000 ? v - 0x10000 : v;
  }
  return out;
}

export class LiveSession {
  // 開連線（還沒送 session.start）。失敗丟例外，訊息含 HTTP 狀態與回應前 300 字。
  static async open(apiKey) {
    if (!apiKey) throw new Error("沒有設定 OPENAI_API_KEY（wrangler secret put）");
    const resp = await fetch(LIVE_URL, { headers: { Upgrade: "websocket", Authorization: "Bearer " + apiKey } });
    const ws = resp.webSocket;
    if (!ws) {
      let body = "";
      try { body = (await resp.text()).slice(0, 300); } catch {}
      throw new Error("GPT-Live 連線失敗 HTTP " + resp.status + " " + body);
    }
    ws.accept();
    return new LiveSession(ws);
  }

  constructor(ws) {
    this.ws = ws;
    this.t0 = Date.now();
    this.started = null;      // session.started 事件
    this.closedEv = null;     // session.closed 事件
    this.lastUsage = null;    // 最近一次 session.usage.updated（累計值）
    this.sockClosed = false;
    this.handlers = [];
    ws.addEventListener("message", (e) => this._onMsg(e.data));
    ws.addEventListener("close", (e) => {
      this.sockClosed = true;
      this._emit({ type: "_socket.close", code: e.code, reason: e.reason, _at: Date.now() });
    });
    ws.addEventListener("error", (e) => {
      this._emit({ type: "_socket.error", message: e && e.message ? e.message : String(e), _at: Date.now() });
    });
  }

  _onMsg(data) {
    if (typeof data !== "string") return;   // 官方協定全是 JSON 文字
    let ev;
    try { ev = JSON.parse(data); } catch { return; }
    ev._at = Date.now();
    if (ev.type === "session.started") this.started = ev;
    else if (ev.type === "session.closed") this.closedEv = ev;
    else if (ev.type === "session.usage.updated") this.lastUsage = ev;
    this._emit(ev);
  }
  _emit(ev) {
    for (const h of this.handlers.slice()) {
      try { h(ev); } catch (e) { console.log("[live] handler error " + (e && e.message)); }
    }
  }
  on(h) { this.handlers.push(h); return () => { this.handlers = this.handlers.filter((x) => x !== h); }; }

  send(obj) {
    if (this.sockClosed) return false;
    try { this.ws.send(JSON.stringify(obj)); return true; } catch { return false; }
  }

  // 等某種事件（或 socket 關閉）；逾時回 null
  waitFor(type, ms) {
    return new Promise((resolve) => {
      let off = null;
      const timer = setTimeout(() => { if (off) off(); resolve(null); }, ms);
      off = this.on((ev) => {
        if (ev.type === type || ev.type === "_socket.close") {
          clearTimeout(timer); off(); resolve(ev.type === type ? ev : null);
        }
      });
    });
  }

  // session.start 後等 session.started；回傳 started 事件或丟例外
  async start({ instructions, voice = "marin", input = [], delegation = null }, ms = 10000) {
    const p = this.waitFor("session.started", ms);
    const errs = [];
    const off = this.on((ev) => { if (ev.type === "error") errs.push(ev); });
    const session = { model: LIVE_MODEL, audio: { format: { type: "audio/pcm", rate: LIVE_RATE }, output: { voice } } };
    if (instructions) session.instructions = instructions;
    if (input && input.length) session.input = input;
    if (delegation) session.delegation = delegation;
    this.send({ type: "session.start", session });
    const ev = await p;
    off();
    if (!ev) throw new Error("沒等到 session.started" + (errs.length ? "：" + JSON.stringify(errs[0]).slice(0, 300) : ""));
    return ev;
  }

  append(pcm) {   // Int16Array（Workers 是 little-endian，可直接取 bytes）
    if (!pcm.length) return true;
    return this.send({ type: "session.input_audio.append", audio: b64FromBytes(new Uint8Array(pcm.buffer, pcm.byteOffset, pcm.byteLength)) });
  }
  mute() { return this.send({ type: "session.input_audio.mute" }); }
  unmute() { return this.send({ type: "session.input_audio.unmute" }); }

  // 送 session.close 並等 session.closed（最多 ms），再關 socket。回傳 closed 事件或 null。
  async close(ms = 15000) {
    if (!this.closedEv && !this.sockClosed) {
      const p = this.waitFor("session.closed", ms);
      this.send({ type: "session.close" });
      await p;
    }
    try { this.ws.close(1000, "done"); } catch {}
    return this.closedEv;
  }
}

// history（[{role, text}]）→ session.input 的格式
export function historyToInput(turns) {
  const out = [];
  for (const t of turns) {
    if (t.user) out.push({ type: "message", role: "user", content: [{ type: "input_text", text: t.user }] });
    if (t.assistant) out.push({ type: "message", role: "assistant", content: [{ type: "output_text", text: t.assistant }] });
  }
  return out;
}

// Friday 的基本人設（B3c 定案文字；11-20 機器名改 M5S3Pack，不再叫小S）
// 11-20：問天氣、新聞時模型會把問題「委派」給後端（session.delegation.created）。預設 delegation＝client
// 要我們自己接工具；Miles 定案改用 Responses 委派＋OpenAI 內建 web_search（搜尋在 OpenAI 端執行）。
// 費用（11-20 官方價格頁）：web_search 每 1000 次 $10＋搜尋內容 token；後端 gpt-5.6-luna 每百萬 token $0.2／$1.2。
export const XS_INSTRUCTIONS =
  "你叫 Friday，是住在口袋終端 M5S3Pack（M5StickS3＋RF Pack）裡的語音助理，機器一律叫 M5S3Pack、不要叫小S；用繁體中文、簡短口語回答；使用者是按住按鈕講話、放開才算講完。" +
  "需要即時資訊（天氣、新聞、股價、日期時間）時交給後端上網查，查的時候先簡短說一句「我查一下」，" +
  "拿到結果再用兩三句口語講重點；地點沒講就以台灣為主。";
export const XS_DELEGATION = {
  type: "responses",
  responses: {
    model: "gpt-5.6-luna",
    instructions: "你是語音助理的後端。用網路搜尋查最新資訊，以繁體中文回傳精簡重點（三句以內、附日期），台灣在地資訊優先。",
    tools: [{ type: "web_search" }],
    tool_choice: "auto",
  },
};
