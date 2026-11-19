// B3c 先導測試（/lab）：用 B3b-5 存在 DO storage 的最近一段錄音直接跑 GPT-Live，回傳純文字時間報告。
// 不需裝置。一次請求只跑一項：
//   /lab?t=<HUB_TOKEN>&test=info        看錄音資訊（不連 OpenAI、不花錢）
//   /lab?t=<HUB_TOKEN>&test=pin         把目前最近一段錄音「鎖定」成測試素材（之後裝置再錄也不會換掉）
// 11-20 第一輪實測發現：GPT-Live 沒講話時也每 100ms 送一則（靜音）output_audio.delta
//   → 「開口」要看每則的音量（RMS > VOICE_DB），不能看有沒有收到。
//   /lab?t=<HUB_TOKEN>&test=A&n=2       照真實速度送 → 補靜音直到開口（量 A），收完回答（量 D）
//   /lab?t=<HUB_TOKEN>&test=B&n=2       4 倍速送（&speed= 可改）→ 同上（量 B、D）
//   /lab?t=<HUB_TOKEN>&test=C&n=2       送到一半停 2 秒什麼都不送（&pause= 毫秒可改）→ 看會不會搶話（量 C、D）
//   /lab?t=<HUB_TOKEN>&test=M&n=2       同 C，但停頓 &mutedelay=800 毫秒後送 mute、恢復時 unmute（C 會搶話時的對策是否有效）
// 判定規則（docs/KICKOFF_B3c.md，量完直接套用）：
//   A 靜音長度＝實測＋0.5 秒（最多 3 秒）｜B 正常→全速送｜C 不搶話→中斷時不送｜D 結束門檻＝最長間隔＋0.5 秒
import { LiveSession, XS_INSTRUCTIONS, bytesFromB64, pcmFromBytes } from "./gpt_live.js";

const SR = 16000;
const CHUNK = 1600;              // 100ms
const VOICE_DB = -50;            // 輸出每則 RMS 高於這個（dBFS）＝在講話
const NO_VOICE_MS = 12000;       // 錄音送完後這麼久都沒講話 → 結束這次
const ANSWER_GAP_END_MS = 6000;  // 講話後連續 6 秒安靜 → 當作講完（量 D 用，故意放長）
const RUN_MAX_MS = 45000;        // 每次最多連線這麼久（照連線時間計費）

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function loadClip(storage, pfx = "clip:") {
  const meta = await storage.get(pfx + "meta");
  if (!meta || !meta.n) return null;
  const keys = [];
  for (let i = 0; i < meta.n; i++) keys.push(pfx + i);
  const m = await storage.get(keys);
  let bytes = 0;
  for (const k of keys) bytes += m.get(k) ? m.get(k).byteLength : 0;
  const u8 = new Uint8Array(bytes);
  let o = 0;
  for (const k of keys) { const b = m.get(k); if (b) { u8.set(new Uint8Array(b.buffer || b, b.byteOffset || 0, b.byteLength), o); o += b.byteLength; } }
  const pcm = new Int16Array(u8.buffer, 0, u8.length >> 1);
  return { meta, pcm, u8 };
}

async function pinClip(storage) {
  const c = await loadClip(storage, "clip:");
  if (!c) return null;
  const entries = { "labclip:meta": { ...c.meta, pinned: Date.now() } };
  let n = 0;
  for (let o = 0; o < c.u8.length; o += 100 * 1024) entries["labclip:" + n++] = c.u8.slice(o, Math.min(o + 100 * 1024, c.u8.length));
  entries["labclip:meta"].n = n;
  await storage.put(entries);
  return c;
}

function rmsDb(pcm) {
  if (!pcm.length) return -120;
  let acc = 0;
  for (let i = 0; i < pcm.length; i++) acc += pcm[i] * pcm[i];
  const r = Math.sqrt(acc / pcm.length);
  return r > 0 ? 20 * Math.log10(r / 32768) : -120;
}

// 每 100ms 一塊的人聲指標（一階差分 RMS，dBFS）＝裝置 VAD 同一指標，門檻 −55
function vadBlocks(pcm) {
  const out = [];
  for (let s = 0; s + CHUNK <= pcm.length; s += CHUNK) {
    let acc = 0;
    for (let i = s + 1; i < s + CHUNK; i++) { const d = pcm[i] - pcm[i - 1]; acc += d * d; }
    const rms = Math.sqrt(acc / (CHUNK - 1));
    out.push(rms > 0 ? 20 * Math.log10(rms / 32768) : -120);
  }
  return out;
}
function speechSpan(pcm) {
  const v = vadBlocks(pcm);
  let first = -1, last = -1;
  v.forEach((d, i) => { if (d > -55) { if (first < 0) first = i; last = i; } });
  return { first: first < 0 ? null : first * 100, last: last < 0 ? null : (last + 1) * 100, blocks: v.length };
}

function stats(arr) {
  if (!arr.length) return "（無）";
  const s = arr.slice().sort((a, b) => a - b);
  const q = (p) => s[Math.min(s.length - 1, Math.floor(p * (s.length - 1) + 0.5))];
  return `n=${s.length} 中位 ${q(0.5)} ms｜p90 ${q(0.9)}｜p99 ${q(0.99)}｜最大 ${s[s.length - 1]} ms`;
}

// 跑一次。cfg：{ speed, pauseAtMs, pauseMs }
async function runOnce(env, clip, cfg, log) {
  const pcm = clip.pcm;
  const clipMs = Math.round(pcm.length / 16);
  const span = speechSpan(pcm);
  const R = { cfg, clipMs, deltas: [], voice: [], firstVoiceAt: 0, lastVoiceAt: 0, inTx: "", outTx: "", errors: [], types: {} };
  const tOpen0 = Date.now();
  let sess;
  try { sess = await LiveSession.open(env.OPENAI_API_KEY); }
  catch (e) { R.fatal = e.message; return R; }
  R.connectMs = Date.now() - tOpen0;
  let firstTxIn = null, firstTxOut = null, lastTxOut = null, firstTxEv = null;
  sess.on((ev) => {
    R.types[ev.type] = (R.types[ev.type] || 0) + 1;
    if (ev.type === "session.output_audio.delta") {
      let n = 0, db = -120;
      try { const u = bytesFromB64(ev.delta || ""); n = u.length; db = rmsDb(pcmFromBytes(u)); } catch {}
      const v = db > VOICE_DB;
      R.deltas.push({ at: ev._at, bytes: n, db, v });
      if (v) { if (!R.voice.length) R.firstVoiceAt = ev._at; R.voice.push(ev._at); R.lastVoiceAt = ev._at; }
    } else if (ev.type === "session.input_transcript.delta") {
      R.inTx += ev.delta || "";
      if (!firstTxIn) firstTxIn = ev;
    } else if (ev.type === "session.output_transcript.delta") {
      R.outTx += ev.delta || "";
      if (!firstTxOut) firstTxOut = ev;
      lastTxOut = ev;
    } else if (ev.type === "error" || ev.type === "_socket.error") {
      R.errors.push(JSON.stringify(ev).slice(0, 300));
    }
    if (!firstTxEv && (ev.type === "session.input_transcript.delta" || ev.type === "session.output_transcript.delta")) firstTxEv = ev;
  });
  try { R.started = await sess.start({ instructions: XS_INSTRUCTIONS }); }
  catch (e) { R.fatal = e.message; await sess.close(5000); return R; }
  R.startMs = Date.now() - tOpen0;

  // ── 送錄音 ──
  const tS = Date.now();
  R.tStream = tS;
  let pausedAt = null, pauseDone = false, wallPause = 0;
  for (let i = 0, s = 0; s < pcm.length; i++, s += CHUNK) {
    const audioMs = i * 100;
    if (cfg.pauseAtMs != null && !pauseDone && audioMs >= cfg.pauseAtMs) {
      pausedAt = Date.now();
      if (cfg.muteDelay != null) {
        await sleep(cfg.muteDelay);
        sess.mute();
        R.mutedAt = Date.now();
        await sleep(Math.max(0, cfg.pauseMs - cfg.muteDelay));
        sess.unmute();
      } else {
        await sleep(cfg.pauseMs);
      }
      wallPause = Date.now() - pausedAt;
      pauseDone = true;
      R.pauseStart = pausedAt; R.pauseEnd = Date.now();
    }
    const due = tS + audioMs / cfg.speed + wallPause;
    const w = due - Date.now();
    if (w > 0) await sleep(w);
    sess.append(pcm.subarray(s, Math.min(s + CHUNK, pcm.length)));
  }
  // 最後一塊照速度送完的時刻
  const lastDue = tS + clipMs / cfg.speed + wallPause;
  if (lastDue > Date.now()) await sleep(lastDue - Date.now());
  const tEnd = Date.now();
  R.tSpeechEnd = tEnd;
  R.sendWallMs = tEnd - tS;
  // 錄音裡最後一塊有人聲的時刻（牆上時間）
  R.speechEndWall = span.last != null ? tS + span.last / cfg.speed + (cfg.pauseAtMs != null && span.last > cfg.pauseAtMs ? wallPause : 0) : tEnd;
  R.voiceBeforeEnd = R.voice.filter((a) => a < R.speechEndWall).length;

  // ── 之後像真的麥克風一樣持續送靜音（照真實速度），看模型何時開口、講多久、何時講完 ──
  const zero = new Int16Array(CHUNK);
  let silMs = 0;
  for (;;) {
    const now = Date.now();
    if (sess.sockClosed || now - tS > RUN_MAX_MS) break;
    if (!R.firstVoiceAt && now - tEnd > NO_VOICE_MS) break;
    if (R.firstVoiceAt && R.lastVoiceAt > R.speechEndWall && now - R.lastVoiceAt > ANSWER_GAP_END_MS) break;
    sess.append(zero);
    silMs += 100;
    const w = tEnd + silMs - Date.now();
    if (w > 0) await sleep(w);
  }
  R.silenceSentMs = silMs;
  const closeT = Date.now();
  await sess.close(15000);
  R.closeMs = Date.now() - closeT;
  R.closedEv = sess.closedEv;
  R.lastUsage = sess.lastUsage;
  R.firstTx = firstTxEv;
  R.firstTxOut = firstTxOut;
  R.lastTxOut = lastTxOut;
  return R;
}

function report(R, idx, span) {
  const L = [];
  const f = (v) => (v == null ? "—" : v + " ms");
  L.push(`── 第 ${idx} 次｜速度 ×${R.cfg.speed}${R.cfg.pauseAtMs != null ? `｜在 ${R.cfg.pauseAtMs} ms 處停 ${R.cfg.pauseMs} ms` : ""}${R.cfg.muteDelay != null ? `（停 ${R.cfg.muteDelay} ms 後 mute、恢復時 unmute）` : ""} ──`);
  if (R.fatal) { L.push("失敗：" + R.fatal); return L.join("\n"); }
  L.push(`連線 ${R.connectMs} ms → session.started ${R.startMs} ms｜送錄音牆上時間 ${R.sendWallMs} ms（錄音 ${R.clipMs} ms）`);
  const rel = (a) => (a ? a - R.speechEndWall : null);
  L.push(`[A] 開口（第一則音量 > ${VOICE_DB} dBFS）：最後一塊人聲後 ${f(rel(R.firstVoiceAt))}｜錄音送完後 ${f(R.firstVoiceAt ? R.firstVoiceAt - R.tSpeechEnd : null)}｜人還沒講完就出聲 ${R.voiceBeforeEnd} 則`);
  if (R.pauseStart) {
    const inPause = R.voice.filter((a) => a >= R.pauseStart && a <= R.pauseEnd + 500).length;
    const before = R.voice.filter((a) => a >= R.pauseStart && a < R.speechEndWall).length;
    L.push(`[C] 停頓期間（含恢復後 0.5 秒）出聲 ${inPause} 則｜停頓開始到最後一塊人聲之間出聲 ${before} 則 → ${inPause || before ? "會搶話 ✗" : "不會搶話 ✓"}`);
  }
  // 輸出音訊的音量分佈（確認靜音則真的是靜音、門檻合理）
  const sil = R.deltas.filter((d) => !d.v).map((d) => Math.round(d.db)).sort((a, b) => a - b);
  const vo = R.deltas.filter((d) => d.v).map((d) => Math.round(d.db)).sort((a, b) => a - b);
  const mid = (a) => (a.length ? a[a.length >> 1] : "—");
  L.push(`[音量] 輸出共 ${R.deltas.length} 則：靜音 ${sil.length} 則（${sil.length ? sil[0] + "～" + sil[sil.length - 1] : "—"} dBFS，中位 ${mid(sil)}）｜講話 ${vo.length} 則（${vo.length ? vo[0] + "～" + vo[vo.length - 1] : "—"}，中位 ${mid(vo)}）`);
  // D：回答中（第一則到最後一則講話之間）的安靜段
  if (R.voice.length > 1) {
    const gaps = [];
    for (let i = 1; i < R.voice.length; i++) gaps.push(R.voice[i] - R.voice[i - 1]);
    const top = gaps.map((g, i) => ({ g, at: R.voice[i] - R.voice[0] })).sort((a, b) => b.g - a.g).slice(0, 5).map((x) => `${x.g}@${x.at}`);
    L.push(`[D] 講話 ${R.voice[R.voice.length - 1] - R.voice[0] + 100} ms（${R.voice.length} 則有聲）｜有聲則之間的間隔 ${stats(gaps)}`);
    L.push(`[D] 前 5 大安靜段（ms@開口後 ms）：${top.join("、")}`);
  } else {
    L.push("[D] 沒有講話");
  }
  // 時間軸：每 500ms 一格，█＝有講話、·＝靜音、空白＝沒收到（從送錄音開始算）
  if (R.deltas.length) {
    const t0 = R.tStream, last = R.deltas[R.deltas.length - 1].at;
    let line = "";
    for (let t = t0; t <= last; t += 500) {
      const ds = R.deltas.filter((d) => d.at >= t && d.at < t + 500);
      line += !ds.length ? " " : ds.some((d) => d.v) ? "█" : "·";
    }
    const endCol = Math.floor((R.speechEndWall - t0) / 500);
    L.push(`[時間軸] 每格 0.5 秒，從送錄音開始；第 ${endCol + 1} 格是你講完的位置\n         ${line}`);
  }
  L.push(`[送出] 錄音 ${R.sendWallMs} ms（牆上）＋ 之後持續送靜音 ${R.silenceSentMs} ms`);
  L.push(`[逐字稿] 使用者：${R.inTx || "（無）"}`);
  L.push(`[逐字稿] 小S：${R.outTx || "（無）"}`);
  if (R.firstTx) L.push(`[逐字稿] 第一則 delta 原樣：${JSON.stringify(R.firstTx).slice(0, 240)}`);
  if (R.lastTxOut) L.push(`[逐字稿] 小S 最後一則 delta 原樣：${JSON.stringify(R.lastTxOut).slice(0, 240)}`);
  L.push(`[事件] ${Object.entries(R.types).map(([k, v]) => k + "×" + v).join("、")}`);
  if (R.errors.length) L.push(`[錯誤] ${R.errors.join("｜")}`);
  L.push(`[用量] 最後一次 session.usage.updated：${R.lastUsage ? JSON.stringify(R.lastUsage.usage).slice(0, 200) : "（無）"}｜session.closed 的 usage：${R.closedEv && R.closedEv.usage ? JSON.stringify(R.closedEv.usage) : "（無）"}`);
  L.push(`[結束] session.closed（等了 ${R.closeMs} ms）：${R.closedEv ? JSON.stringify(R.closedEv).slice(0, 600) : "（15 秒內沒收到，用量未確認）"}`);
  if (R.started) L.push(`[開始] session.started：${JSON.stringify(R.started).slice(0, 400)}`);
  return L.join("\n");
}

export async function runLab(request, env, storage) {
  const url = new URL(request.url);
  const test = (url.searchParams.get("test") || "info").toUpperCase();
  const n = Math.max(1, Math.min(3, parseInt(url.searchParams.get("n") || "2", 10) || 2));
  const out = [];
  out.push(`B3c 先導測試｜test=${test}｜${new Date().toISOString()}`);
  const desc = (c) => {
    const sp = speechSpan(c.pcm);
    let pk = 0;
    for (let i = 0; i < c.pcm.length; i++) { const a = Math.abs(c.pcm[i]); if (a > pk) pk = a; }
    return `${(c.pcm.length / SR).toFixed(2)} 秒｜存於 ${new Date(c.meta.t).toISOString()}｜峰值 ${pk}｜人聲（逐塊 > −55 dBFS）${sp.first ?? "—"}～${sp.last ?? "—"} ms`;
  };
  if (test === "PIN") {
    const c = await pinClip(storage);
    out.push(c ? "已鎖定成測試素材：" + desc(c) : "storage 裡沒有最近錄音（clip:meta）");
    return text(out);
  }
  const latest = await loadClip(storage, "clip:");
  const clip = await loadClip(storage, "labclip:");
  if (latest) out.push("最近一段錄音：" + desc(latest));
  if (!clip) {
    out.push("還沒鎖定測試素材。先對裝置講一句問題，再跑 test=pin。");
    return text(out);
  }
  out.push("測試素材（已鎖定）：" + desc(clip));
  const span = speechSpan(clip.pcm);
  if (test === "INFO") return text(out);
  if (span.last == null) { out.push("測試素材裡沒有人聲，不跑（避免白花錢）。重錄一句再 test=pin。"); return text(out); }

  let cfg;
  if (test === "A") cfg = { speed: 1, pauseAtMs: null, pauseMs: 0 };
  else if (test === "B") cfg = { speed: Math.max(1, parseFloat(url.searchParams.get("speed") || "4") || 4), pauseAtMs: null, pauseMs: 0 };
  else if (test === "C" || test === "M") {
    const mid = span.first != null && span.last != null ? Math.round((span.first + span.last) / 2 / 100) * 100 : Math.round(clip.pcm.length / 32 / 100) * 100;
    cfg = { speed: 1, pauseAtMs: mid, pauseMs: parseInt(url.searchParams.get("pause") || "2000", 10) || 2000 };
    if (test === "M") cfg.muteDelay = parseInt(url.searchParams.get("mutedelay") || "800", 10);
  } else {
    out.push("test 只能是 info、pin、A、B、C、M（D 在每一項裡一起量）");
    return text(out);
  }
  for (let i = 1; i <= n; i++) {
    const R = await runOnce(env, clip, cfg);
    out.push(report(R, i, span));
    console.log("[lab] " + test + " #" + i + " done");
  }
  return text(out);
}

function text(lines) {
  return new Response(lines.join("\n\n") + "\n", { headers: { "content-type": "text/plain; charset=utf-8" } });
}
