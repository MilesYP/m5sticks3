// GPT 模式的一輪對話（M1-B / B3c＋B4）：裝置 ↔ 中控 ↔ GPT-Live
//
// 定案做法（docs/KICKOFF_B3c.md）：
//   - 按 A（start）才開 session；回答完 IDLE_CLOSE_MS 沒新的 start 就 session.close
//     （GPT-Live 照連線時間計費，約 $3／小時 → 這 30 秒每輪約 $0.025）
//   - 上下文：每輪輸入／輸出逐字稿存 storage "ctx"（最近 50 輪），開新 session 帶最近 10 輪
//   - 輸入：μ-law → PCM16 → session.input_audio.append；session 還沒 started 前先存著
//     stop 後補 PAD_MS 靜音（照真實速度）讓模型判斷講完
//   - 輸出：output_audio.delta → 串流 ADPCM 8kHz（固定增益 1.0）→ 每則 3 幀送裝置；
//     還沒收到 stop 時模型先開口的音訊扣住，stop 之後才送（ES8311 不能同時錄放）
//   - 11-20 先導測試：GPT-Live 沒講話時也每 100ms 送一則「靜音」output_audio.delta → 一律看音量（RMS > VOICE_DB）
//     判斷有沒有在講話：開口前的靜音不送；開口後全部照送（含停頓，否則裝置緩衝見底斷音）；
//     stop 後像真的麥克風一樣持續送靜音（照真實速度），直到這輪結束（官方建議持續送音）
//   - 回答結束：最後一則有聲之後安靜超過 END_GAP_MS → audio_end（pcm＝這輪收到的上傳 PCM16 bytes、g=1、ms＝回覆長度）
//   - 同一個 id 的 start（裝置斷線重送）→ 關舊 session、開新的（帶上下文）、重收整段
//   - session closed（expired 等）時：還沒開口 → 帶上下文重開並重送這輪已收到的音訊；已在回答 → 用已收到的結束
//   - 每日額度（台北日期）：turns／audioSec 超過 DAILY_MAX_TURNS／DAILY_MAX_AUDIO_SEC 就拒絕
//   - B4：逐字稿每 TEXT_EVERY_MS 合併送 {"t":"text","role":"user|assistant","s":"…"}；小S 的字也扣到 stop 之後
import { LiveSession, XS_INSTRUCTIONS, XS_DELEGATION, historyToInput, bytesFromB64, pcmFromBytes } from "./gpt_live.js";
import { makeStreamEncoder, decodeMulaw } from "./adpcm.js";
import { toTw } from "./s2t.js";

// ── 先導測試定出的參數（docs/M1B-架構.md 第 4 節「B3c 先導測試」）──
export const B3C = {
  PAD_MS: 1700,          // [A] stop 後至少補這麼長的靜音（11-20 實測錄音送完後 1.10／1.20 秒開口＋0.5 秒）；之後仍持續送到這輪結束
  FEED_FAST: true,       // [B] 11-20 實測 4 倍速送逐字稿正確、開口正常 → 可以快送
  // [C] 11-20 實測：送到一半停 2 秒會搶話（4／4 次）；停 0.8 秒後 mute 也擋不住（2／2 次），甚至在使用者句間換氣時就開口
  //     → 規則「兩者都不行 → 先收齊整段再餵（保底）」：放開 A 之後才把整段送進去，速度用 [B] 驗證過的 4 倍
  //     代價：開口約在放開後 3 秒＋問題長度／4。要改回邊講邊送，把這裡改 "none"
  //     11-20 實機：buffer 模式開口 3～4 秒、15 秒長問題 8 秒，且那次逐字稿後半段遺失（推測 4 倍速造成）
  //     → Miles 定案改回邊講邊送（none）：開口約 1～1.7 秒；代價是使用者句間停頓時模型可能先答一半（stop 前的音訊扣住）
  UPLOAD_GAP: "none",    // none＝收到就送（中斷時不送）、mute＝中斷時 mute、buffer＝先收齊再餵
  FEED_SPEED: 4,         // buffer 模式餵進去的速度（倍速；[B] 驗證過 4 倍）
  END_GAP_MS: 1500,      // [D] 11-20 實測長回答（12～15 秒）有聲則之間最長 915／800 ms ＋ 0.5 秒，取整
  VOICE_DB: -50,         // 輸出每則 RMS 高於這個（dBFS）＝在講話（11-20 實測：靜音中位 −120、最大 −53；講話 −50～−18）
  STALL_MS: 800,         // 上傳多久沒新資料算「中斷」（裝置每則 0.5 秒音訊；11-20 實測停頓約 1.5 秒模型就搶話）
  ABANDON_MS: 20000,     // 錄音中這麼久沒資料、也沒 stop → 放棄這輪（裝置 15 秒就放棄）
  NO_ANSWER_MS: 15000,   // 補完靜音後這麼久沒開口 → 結束這輪（n=0）
  IDLE_CLOSE_MS: 30000,
  TEXT_EVERY_MS: 300,
  TEXT_MAX_BYTES: 240,   // 每則 text 的 s 上限（UTF-8）；裝置收件解析緩衝 512 bytes
  CTX_KEEP: 50,
  CTX_SEND: 10,
  PACE_LEAD_MS: 300,     // 照真實速度送時，最多比真實時間超前多少
  WEB_SEARCH: true,      // 11-20 Miles 定案：Responses 委派＋web_search（查天氣、新聞）
  DELEGATE_MAX_MS: 30000,// 委派（上網查）最多等多久；查詢中不算「講完」
};

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
// 回覆音量（11-20 實機：固定增益 1.0 太小聲、聽起來有點破——小音量經 8kHz ADPCM 壓縮後雜訊相對明顯；
// 裝置喇叭已開到最大）→ 可固定放大 OUT_GAIN 倍（wrangler 變數），超過 −2.7 dBFS 的部分柔性壓縮不硬削。
// 實聽：2.5 倍比較大聲但更「爆」（ADPCM 雜訊跟著放大），Miles 覺得 1 比較清楚 → 預設 1；沙沙聲來自 8kHz 下載格式。
// 仍是固定增益（不做整段正規化，邊收邊送拿不到整段峰值）。之後 Scroll 滾輪調音量在待辦。
const KNEE = 24000;
function applyGain(pcm, g, st) {
  if (g === 1) return pcm;
  const out = new Int16Array(pcm.length);
  for (let i = 0; i < pcm.length; i++) {
    let y = pcm[i] * g;
    const a = Math.abs(y);
    if (a > KNEE) { y = Math.sign(y) * (KNEE + (a - KNEE) * 0.25); st.soft++; }
    if (y > 32767) { y = 32767; st.clip++; } else if (y < -32768) { y = -32768; st.clip++; }
    out[i] = Math.round(y);
    const r = Math.abs(pcm[i]);
    if (r > st.peak) st.peak = r;   // 放大前的原始峰值
  }
  return out;
}

function rmsDb(pcm) {
  if (!pcm.length) return -120;
  let acc = 0;
  for (let i = 0; i < pcm.length; i++) acc += pcm[i] * pcm[i];
  const r = Math.sqrt(acc / pcm.length);
  return r > 0 ? 20 * Math.log10(r / 32768) : -120;
}
const ZERO100 = new Int16Array(1600);

// ── 每日額度 ──
export function taipeiDate(now = Date.now()) {
  return new Date(now + 8 * 3600e3).toISOString().slice(0, 10);
}
export function quotaLimits(env) {
  const t = parseInt(env.DAILY_MAX_TURNS, 10);
  const a = parseInt(env.DAILY_MAX_AUDIO_SEC, 10);
  return { turns: Number.isFinite(t) ? t : 200, audioSec: Number.isFinite(a) ? a : 1800 };
}

// 字串依 UTF-8 長度切段（不切斷字元）
export function splitUtf8(s, maxBytes) {
  const out = [];
  let cur = "", n = 0;
  for (const ch of s) {
    const b = ch.codePointAt(0) < 0x80 ? 1 : ch.codePointAt(0) < 0x800 ? 2 : ch.codePointAt(0) < 0x10000 ? 3 : 4;
    if (n + b > maxBytes && cur) { out.push(cur); cur = ""; n = 0; }
    cur += ch; n += b;
  }
  if (cur) out.push(cur);
  return out;
}

export class GptLink {
  constructor(storage, env, sendJson) {
    this.storage = storage;
    this.env = env;
    this.sendJson = sendJson;   // (ws, obj)
    this.sess = null;           // 目前可用的 LiveSession（已 started）
    this.opening = null;        // 正在開的 Promise
    this.turn = null;
    this.idleTimer = null;
    this.tickTimer = null;
    this.pumping = false;
    this.cfg = B3C;
  }

  log(s) { console.log("[gpt] " + s); }
  outGain() {
    const g = parseFloat(this.env.OUT_GAIN);
    return Number.isFinite(g) && g > 0 && g <= 8 ? g : 1;
  }

  // ── 裝置 start（mode=gpt）。回傳 false＝被拒（額度），呼叫端不要再送 listening ──
  async onStart(ws, msg, fmt) {
    const id = msg.id ?? null;
    const prev = this.turn;
    const resend = !!(prev && !prev.done && id != null && prev.id === id);
    if (!resend) {
      const key = "quota:" + taipeiDate();
      const q = (await this.storage.get(key)) || { turns: 0, audioSec: 0 };
      const L = quotaLimits(this.env);
      if (q.turns >= L.turns || q.audioSec >= L.audioSec) {
        this.log(`額度已滿：今天 ${q.turns} 輪／${q.audioSec.toFixed(0)} 秒（上限 ${L.turns} 輪／${L.audioSec} 秒）→ 拒絕`);
        if (prev && !prev.done) this.cancel(prev, "額度拒絕");
        this.turn = null;
        this.sendJson(ws, { t: "error", code: "quota", s: `今日額度已用完（${q.turns}/${L.turns} 輪、${Math.round(q.audioSec)}/${L.audioSec} 秒）` });
        this.sendJson(ws, { t: "audio_end", n: 0, fmt: fmt.down, pcm: 0, g: 1, ms: 0 });
        this.sendJson(ws, { t: "state", v: "idle" });
        return false;
      }
      q.turns += 1;
      await this.storage.put(key, q);
      if (q.turns === 1) this.pruneQuota(key);
    }
    if (prev && !prev.done) {
      // 上一輪還沒結束（斷線重送或新一輪搶先）：session 狀態已經被上一輪的音訊影響 → 關掉重開
      if (resend) prev.resendDiscard = true;   // 會整段重收，這份逐字稿不存
      this.cancel(prev, resend ? "同一 id 重送" : "被新一輪取代");
      this.dropSession(resend ? "斷線重送" : "新一輪取代未完成的上一輪");
    }
    if (this.idleTimer) { clearTimeout(this.idleTimer); this.idleTimer = null; }
    const now = Date.now();
    const t = {
      id, ws, up: fmt.up, down: fmt.down, t0: now,
      upPcm: [], upSamples: 0, feedIdx: 0, feedClock: 0, lastFrameAt: now, muted: false,
      stopped: false, stopAt: 0, padSent: 0, padStart: 0, padEndAt: 0,
      enc: makeStreamEncoder(fmt.down), held: [], sentBytes: 0,
      outStarted: false, firstOutAt: 0, lastOutAt: 0, silPend: [], voicedN: 0,
      delegating: 0, delegateAt: 0, searches: 0,
      lvl: { peak: 0, soft: 0, clip: 0 },   // 放大前的峰值、柔性壓縮取樣數、硬削取樣數
      inTx: "", outTx: "", inSent: 0, outSent: 0, lastTextAt: 0, outPendMs: 0,
      inTw: "", outTw: "",   // 已送出的繁體文字（對整段累積文字轉換，才不會把跨段的詞轉錯）
      sess: null, err: null, done: false, resend,
    };
    this.turn = t;
    this.log(`start id=${id ?? "-"}${resend ? "（重送）" : ""}`);
    this.ensureSession(t);
    this.startTick();
    return true;
  }

  pruneQuota(keep) {
    this.storage.list({ prefix: "quota:" }).then((m) => {
      const old = [...m.keys()].filter((k) => k !== keep);
      if (old.length) this.storage.delete(old);
    }).catch(() => {});
  }

  // ── session ──
  ensureSession(t) {
    if (this.sess && !this.sess.closedEv && !this.sess.sockClosed) {
      t.sess = this.sess;
      this.pump();
      return;
    }
    const p = (async () => {
      const t0 = Date.now();
      const s = await LiveSession.open(this.env.OPENAI_API_KEY);
      s.on((ev) => this.onLive(s, ev));
      const ctx = ((await this.storage.get("ctx")) || []).slice(-this.cfg.CTX_SEND);
      // Friday 的角色說明與使用者記憶（/persona 上傳）接在人設後面；instructions 上限 16,384 token，這兩份控制在幾千字內
      const pf = (await this.storage.get("persona:friday")) || "";
      const pm = (await this.storage.get("persona:miles")) || "";
      const instructions = [XS_INSTRUCTIONS, pf, pm].filter(Boolean).join("\n\n");
      // 聲音（11-20 Miles：男性、美國口音；ChatGPT App 的 Ember 不在 API 清單 → 預設 meridian＝北美口音男聲）
      // wrangler 變數 GPT_VOICE 可換（不能叫 VOICE：那是 Durable Object 綁定的名字）（session 開始時固定，換了要等下一個 session）
      const voice = (this.env.GPT_VOICE || "meridian").trim();
      const opts = { instructions, input: historyToInput(ctx), voice };
      if (this.cfg.WEB_SEARCH) {
        try {
          await s.start({ ...opts, delegation: XS_DELEGATION });
          this.log(`session 就緒 ${Date.now() - t0} ms（帶上下文 ${ctx.length} 輪、可上網查、記憶 ${pf.length + pm.length} 字）`);
          return s;
        } catch (e) {
          // 委派設定被拒（例如帳號沒有那個後端模型）→ 不帶委派重開一次，至少能對話
          this.log("帶委派開 session 失敗，改不帶委派：" + e.message);
          s.close(3000);
          const s2 = await LiveSession.open(this.env.OPENAI_API_KEY);
          s2.on((ev) => this.onLive(s2, ev));
          await s2.start(opts);
          this.log(`session 就緒 ${Date.now() - t0} ms（帶上下文 ${ctx.length} 輪、不能上網查）`);
          return s2;
        }
      }
      await s.start(opts);
      this.log(`session 就緒 ${Date.now() - t0} ms（帶上下文 ${ctx.length} 輪）`);
      return s;
    })();
    this.opening = p;
    p.then((s) => {
      if (this.opening !== p) { s.close(5000); return; }   // 已被取代
      this.opening = null;
      this.sess = s;
      if (this.turn === t && !t.done) { t.sess = s; this.pump(); }
    }).catch((e) => {
      if (this.opening === p) this.opening = null;
      this.log("開 session 失敗：" + e.message);
      if (this.turn === t && !t.done) t.err = e.message;
    });
  }

  dropSession(why) {
    const s = this.sess;
    this.sess = null;
    this.opening = null;   // 正在開的那個開好後會自己關掉
    if (s) this.closeLive(s, why);
  }

  closeLive(s, why) {
    s.close(15000).then((ev) => {
      this.log(`session 關閉（${why}）：${ev ? JSON.stringify(ev).slice(0, 300) : "15 秒內沒收到 session.closed（用量未確認）"}`);
      if (s.lastUsage) this.log("最後用量 " + JSON.stringify(s.lastUsage).slice(0, 300));
    });
  }

  onLive(s, ev) {
    if (ev.type === "session.usage.updated") { this.lastUsage = ev; return; }
    if (ev.type === "error") { this.log("OpenAI error " + JSON.stringify(ev).slice(0, 300)); }
    if (ev.type === "session.closed" || ev.type === "_socket.close") {
      if (ev.type === "session.closed") this.log("session.closed " + JSON.stringify(ev).slice(0, 200));
      if (this.sess !== s) return;
      this.sess = null;
      const t = this.turn;
      if (t && !t.done && t.sess === s) {
        t.sess = null;
        if (!t.outStarted) {
          this.log("回答前 session 結束 → 帶上下文重開、重送這輪已收到的音訊");
          t.feedIdx = 0; t.feedClock = 0; t.padSent = 0; t.padStart = 0; t.padEndAt = 0;
          this.ensureSession(t);
        }
        // 已在回答：不會再有 delta，結束判斷照常（停頓超過門檻）
      }
      return;
    }
    const t = this.turn;
    if (!t || t.done || t.sess !== s) return;   // 上一輪晚到的資料：丟掉
    switch (ev.type) {
      case "session.output_audio.delta": {
        const pcm = pcmFromBytes(bytesFromB64(ev.delta || ""));
        const now = Date.now();
        const voiced = rmsDb(pcm) > this.cfg.VOICE_DB;
        if (!t.outStarted) {
          if (!voiced) break;   // 開口前的靜音：不送
          t.outStarted = true; t.firstOutAt = now;
          this.log(t.stopped ? `開口：stop 後 ${now - t.stopAt} ms` : `模型在 stop 前先開口（扣住）：start 後 ${now - t.t0} ms`);
          if (t.stopped) this.sendJson(t.ws, { t: "state", v: "speaking" });
        }
        // 開口後的安靜段照常送（11-20 實機：扣著等下一句再補送會讓裝置緩衝見底而斷音）；
        // 結束判斷只看「最後一則有聲」，所以講完後最多多送 END_GAP_MS 的靜音
        if (voiced) { t.lastOutAt = now; t.voicedN++; }
        const msgs = t.enc.push(applyGain(pcm, this.outGain(), t.lvl));
        if (t.stopped) this.sendAudio(t, msgs); else t.held.push(...msgs);
        break;
      }
      case "session.delegation.created":
        // 上網查詢開始：查完之前不算講完（模型通常先說「我查一下」再安靜幾秒）
        t.delegating++; t.delegateAt = Date.now(); t.searches++;
        this.log("委派後端（上網查）開始：" + JSON.stringify(ev).slice(0, 200));
        break;
      case "response.event": {
        const n = ev.event || {};
        if (n.type === "response.completed" || n.type === "response.failed" || n.type === "response.incomplete" || n.type === "response.cancelled") {
          if (t.delegating > 0) t.delegating--;
          const u = n.response && n.response.usage;
          this.log(`委派結束 ${n.type}，耗時 ${Date.now() - t.delegateAt} ms${u ? "｜後端用量 " + JSON.stringify(u).slice(0, 200) : ""}`);
          t.lastOutAt = Math.max(t.lastOutAt, Date.now());   // 查完後給模型時間開口
        } else if (n.type && n.type.includes("web_search") && n.type.endsWith("completed")) {
          this.log("web_search 完成");
        }
        break;
      }
      case "session.input_transcript.delta": t.inTx += ev.delta || ""; t.lastInAt = Date.now(); break;
      case "session.output_transcript.delta":
        // B4：記下這段字開始時回覆音訊已產生多少 ms，裝置播到那裡才顯示（11-20 實機：字跑得比聲音快）
        if (t.outTx.length === t.outSent) t.outPendMs = Math.round(t.enc.inMs());
        t.outTx += ev.delta || "";
        t.lastOutTxAt = Date.now();
        break;
    }
  }

  // ── 裝置上傳的音訊 ──
  onAudio(ws, buf) {
    const t = this.turn;
    if (!t || t.done || t.stopped) return;
    const u8 = new Uint8Array(buf);
    const pcm = t.up === "mulaw" ? decodeMulaw(u8) : pcmFromBytes(u8);
    t.upPcm.push(pcm);
    t.upSamples += pcm.length;
    t.lastFrameAt = Date.now();
    if (t.muted && t.sess) { t.sess.unmute(); t.muted = false; this.log("上傳恢復 → unmute"); }
    this.pump();
  }

  onStop(ws) {
    const t = this.turn;
    if (!t || t.done || t.stopped) return;
    t.stopped = true;
    t.stopAt = Date.now();
    if (t.muted && t.sess) { t.sess.unmute(); t.muted = false; }
    this.log(`stop：收到 ${(t.upSamples / 16000).toFixed(1)} 秒`);
    this.sendJson(ws, { t: "state", v: t.outStarted ? "speaking" : "thinking" });
    if (t.held.length) { this.sendAudio(t, t.held); t.held = []; }
    this.flushText(t);
    this.pump();
  }

  sendAudio(t, msgs) {
    for (const m of msgs) {
      try { t.ws.send(m); t.sentBytes += m.byteLength; } catch { /* 裝置斷線：已收到的播完，這輪照常收尾 */ }
    }
  }

  // ── 把音訊餵給 GPT-Live（session 就緒才送；照設定決定速度），stop 後補靜音 ──
  async pump() {
    if (this.pumping) return;
    this.pumping = true;
    try {
      for (;;) {
        const t = this.turn;
        if (!t || t.done || !t.sess || t.sess.closedEv || t.sess.sockClosed) break;
        const s = t.sess;
        const now = Date.now();
        if (t.feedIdx < t.upPcm.length) {
          const buffered = this.cfg.UPLOAD_GAP === "buffer";
          if (buffered && !t.stopped) break;
          const paced = !this.cfg.FEED_FAST || buffered;
          const speed = buffered ? this.cfg.FEED_SPEED : 1;
          if (paced) {
            if (!t.feedClock) t.feedClock = now;
            const ahead = t.feedClock - now;
            if (ahead > this.cfg.PACE_LEAD_MS) { await sleep(ahead - this.cfg.PACE_LEAD_MS); continue; }
          }
          const pcm = t.upPcm[t.feedIdx++];
          s.append(pcm);
          t.feedClock = Math.max(t.feedClock || now, now - this.cfg.PACE_LEAD_MS) + pcm.length / 16 / speed;
          continue;
        }
        if (t.stopped) {
          // stop 後持續送靜音（照真實速度）直到這輪結束，像真的麥克風；至少 PAD_MS（先導測試 A）
          const pacedFeed = !this.cfg.FEED_FAST || this.cfg.UPLOAD_GAP === "buffer";
          if (!t.padStart) t.padStart = pacedFeed ? Math.max(now, t.feedClock || 0) : now;
          const due = t.padStart + t.padSent;
          if (due > now) { await sleep(due - now); continue; }
          s.append(ZERO100);
          t.padSent += 100;
          if (!t.padEndAt && t.padSent >= this.cfg.PAD_MS) t.padEndAt = Date.now();
          continue;
        }
        break;
      }
    } finally {
      this.pumping = false;
    }
  }

  // ── 每 100ms 檢查：上傳中斷、文字、回答結束、逾時 ──
  startTick() {
    if (this.tickTimer) return;
    this.tickTimer = setInterval(() => this.tick(), 100);
  }
  stopTick() {
    if (this.tickTimer) { clearInterval(this.tickTimer); this.tickTimer = null; }
  }
  tick() {
    const t = this.turn;
    if (!t || t.done) { this.stopTick(); return; }
    const now = Date.now();
    if (now - t.lastTextAt >= this.cfg.TEXT_EVERY_MS) this.flushText(t);
    if (!t.stopped) {
      const idle = now - t.lastFrameAt;
      if (this.cfg.UPLOAD_GAP === "mute" && t.sess && !t.muted && idle > this.cfg.STALL_MS) {
        t.sess.mute(); t.muted = true;
        this.log(`上傳中斷 ${idle} ms → mute`);
      }
      if (idle > this.cfg.ABANDON_MS) { this.cancel(t, "錄音中太久沒資料"); this.dropSession("放棄這輪"); }
      return;
    }
    if (t.err && !t.outStarted) {
      this.sendJson(t.ws, { t: "error", code: "openai", s: String(t.err).slice(0, 200) });
      this.finish(t, "OpenAI 連線失敗");
      return;
    }
    // 上網查詢中：不算講完（最多等 DELEGATE_MAX_MS）
    if (t.delegating > 0 && now - t.delegateAt < this.cfg.DELEGATE_MAX_MS) return;
    if (t.outStarted) {
      if (now - Math.max(t.lastOutAt, t.stopAt) >= this.cfg.END_GAP_MS) this.finish(t, "ok");
      return;
    }
    if (t.padEndAt && now - t.padEndAt > this.cfg.NO_ANSWER_MS) { this.finish(t, "模型沒開口"); return; }
    if (now - t.stopAt > this.cfg.PAD_MS + this.cfg.NO_ANSWER_MS + 10000) this.finish(t, "session 一直沒就緒");
    // 註：padEndAt＝補滿 PAD_MS 的時刻；之後仍持續送靜音
  }

  // final＝這輪結束：全部送出；否則最後一個字先留著（下一段可能和它組成詞，例：头｜发）
  flushText(t, final = false) {
    t.lastTextAt = Date.now();
    const send = (role, s, ms) => {
      for (const part of splitUtf8(s, this.cfg.TEXT_MAX_BYTES)) this.sendJson(t.ws, ms == null ? { t: "text", role, s: part } : { t: "text", role, s: part, ms });
    };
    const now = Date.now();
    const next = (raw, sentTw, lastAt) => {
      const tw = toTw(raw);
      const all = final || now - (lastAt || 0) > 600;   // 0.6 秒沒新字：最後一個字也送
      const upto = all ? tw.length : Math.max(0, tw.length - 1);
      if (upto <= sentTw.length) return null;
      // 正常情況 tw 以 sentTw 開頭；若前面因新字而改寫（很少見），只能接著送新的部分
      return { add: tw.slice(sentTw.length, upto), sent: tw.slice(0, upto) };
    };
    if (t.inTx.length > t.inSent || t.inTw.length < t.inTx.length) {
      const r = next(t.inTx, t.inTw, t.lastInAt);
      if (r) { send("user", r.add); t.inTw = r.sent; }
      t.inSent = t.inTx.length;
    }
    if (t.stopped && (t.outTx.length > t.outSent || t.outTw.length < t.outTx.length)) {
      const r = next(t.outTx, t.outTw, t.lastOutTxAt);
      if (r) { send("assistant", r.add, t.outPendMs); t.outTw = r.sent; }
      t.outSent = t.outTx.length;
    }
  }

  // 上一輪不要了（不送 audio_end；裝置那邊已經開始新的一輪）
  cancel(t, why) {
    if (t.done) return;
    t.done = true;
    this.log(`取消 id=${t.id ?? "-"}（${why}）`);
    this.account(t, false);
  }

  finish(t, why) {
    if (t.done) return;
    t.done = true;
    if (t.held.length) { this.sendAudio(t, t.held); t.held = []; }
    t.silPend = [];   // 講完後的尾巴靜音不送
    this.sendAudio(t, t.enc.flush());
    this.flushText(t, true);
    const ms = Math.round(t.enc.outMs);
    this.sendJson(t.ws, { t: "audio_end", n: t.sentBytes, fmt: t.down, pcm: t.upSamples * 2, g: this.outGain(), ms });
    this.sendJson(t.ws, { t: "state", v: "idle" });
    const lat = t.outStarted ? t.firstOutAt - t.stopAt : null;
    this.log(`結束 id=${t.id ?? "-"}（${why}）：上傳 ${(t.upSamples / 16000).toFixed(1)} 秒｜開口 ${lat == null ? "—" : lat + " ms（stop 後）"}｜回覆 ${(ms / 1000).toFixed(1)} 秒（有聲 ${t.voicedN} 則）、${t.sentBytes} bytes｜音量：增益 ×${this.outGain()}、原始峰值 ${(20 * Math.log10((t.lvl.peak || 1) / 32768)).toFixed(1)} dBFS、柔壓 ${t.lvl.soft}／硬削 ${t.lvl.clip} 取樣｜你：${toTw(t.inTx.trim())}｜Friday：${toTw(t.outTx.trim())}`);
    if (this.lastUsage) this.sendJson(t.ws, { t: "usage", s: JSON.stringify(this.lastUsage.usage ?? this.lastUsage).slice(0, 400) });
    this.account(t, true);
    this.stopTick();
    if (this.idleTimer) clearTimeout(this.idleTimer);
    this.idleTimer = setTimeout(() => {
      this.idleTimer = null;
      if ((!this.turn || this.turn.done) && this.sess) this.dropSession(`回答完 ${this.cfg.IDLE_CLOSE_MS / 1000} 秒沒新對話`);
    }, this.cfg.IDLE_CLOSE_MS);
  }

  // 存上下文、記額度音訊秒數
  async account(t, complete) {
    try {
      const user = toTw(t.inTx.trim()), assistant = toTw(t.outTx.trim());
      if (!t.resendDiscard && (user || assistant) && (complete || assistant)) {
        const ctx = (await this.storage.get("ctx")) || [];
        ctx.push({ t: Date.now(), user, assistant });
        while (ctx.length > this.cfg.CTX_KEEP) ctx.shift();
        await this.storage.put("ctx", ctx);
      }
      const key = "quota:" + taipeiDate();
      const q = (await this.storage.get(key)) || { turns: 0, audioSec: 0 };
      q.audioSec += t.upSamples / 16000 + t.enc.outMs / 1000;
      q.searches = (q.searches || 0) + t.searches;   // 上網查詢次數（只記錄，不設上限）
      await this.storage.put(key, q);
    } catch (e) {
      this.log("存上下文／額度失敗 " + (e && e.message));
    }
  }

  async history() {
    return (await this.storage.get("ctx")) || [];
  }
}
