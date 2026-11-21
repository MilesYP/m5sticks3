// 資訊頁雲端資料（M1-A / A5-2，2026-11-20）：天氣、快訊、語音額度（美金）
//
// 資料來源（Miles 11-20 選定）：
//   - 天氣：中央氣象署開放資料「鄉鎮天氣預報－未來 3 天逐 3 小時」（F-D0047-0xx，依縣市；免費、要授權碼）
//       授權碼存 wrangler secret CWA_KEY；地點存 secret WX_TOWN（例如「臺北市中正區」，個人資料不進 repo）
//   - 快訊：中央社 RSS（feedburner），網址清單在 wrangler 變數 NEWS_FEEDS（逗號分隔）
//   - 語音額度：
//       今日花費＝GPT-Live 連線秒數 × LIVE_USD_PER_MIN／60＋上網查詢次數 × SEARCH_USD＋後端模型 token
//                （價格 11-20 查 developers.openai.com：gpt-live-1 $0.05/分、按秒計；web search $10/千次；
//                  gpt-5.6-luna $0.20／$1.20 每百萬 token）。session.closed 的 usage.seconds 為準，
//                  還開著的 session 先用經過時間估（pending）。
//       每日上限＝DAILY_MAX_USD（台北日期）
//       帳上餘額＝CREDIT_USD（從 CREDIT_SINCE 那天 00:00 UTC 起儲值的總額，Miles 儲值後手動改）
//                 − Costs API（GET /v1/organization/costs，要 admin key，secret OPENAI_ADMIN_KEY）到昨天（UTC）為止
//                 − 中控自己記的、Costs 還沒涵蓋那幾天（UTC）的花費
//                 沒設 admin key 時＝CREDIT_USD − 中控自己記的總花費（只算到這台裝置的語音）
//
// 裝置協定（每則都遠小於裝置收件解析緩衝 512 bytes）：
//   {"t":"info","k":"usd","today":0.42,"cap":2,"left":8.37,"at":<unix 秒>}   left 沒資料時省略
//   {"t":"info","k":"wx","at":…,"loc":"中正","ic":"sun|part|cloud|rain|storm","temp":23,"rh":80,"pop":30,"s":"多雲短暫陣雨"}
//   {"t":"info","k":"nw","i":0,"n":6,"at":…,"s":"標題"}   一則一個訊息，i＝0..n-1
// 裝置送 {"t":"info_req"} → 先回快取，再強制更新（60 秒內最多一次）。

// ── 縣市 → 資料集（未來 3 天逐 3 小時）──
const CWA_3DAY = {
  宜蘭縣: "001", 桃園市: "005", 新竹縣: "009", 苗栗縣: "013", 彰化縣: "017", 南投縣: "021",
  雲林縣: "025", 嘉義縣: "029", 屏東縣: "033", 臺東縣: "037", 花蓮縣: "041", 澎湖縣: "045",
  基隆市: "049", 新竹市: "053", 嘉義市: "057", 臺北市: "061", 高雄市: "065", 新北市: "069",
  臺中市: "073", 臺南市: "077", 連江縣: "081", 金門縣: "085",
};
const DEFAULT_TOWN = "臺北市中正區";
const DEFAULT_FEEDS = [
  "https://feeds.feedburner.com/rsscna/politics",
  "https://feeds.feedburner.com/rsscna/intworld",
  "https://feeds.feedburner.com/rsscna/finance",
  "https://feeds.feedburner.com/rsscna/technology",
  "https://feeds.feedburner.com/rsscna/lifehealth",
  "https://feeds.feedburner.com/rsscna/social",
];
export const INFO = {
  WX_EVERY_MS: 30 * 60e3,
  NEWS_EVERY_MS: 30 * 60e3,
  COSTS_EVERY_MS: 3 * 3600e3,
  FORCE_MIN_MS: 60e3,
  COSTS_MIN_MS: 10 * 60e3,   // Costs API 有頻率限制（11-20 連按 refresh 吃到 429）→ 強制更新也至少隔 10 分鐘
  NEWS_N: 6,
  NEWS_MAX_CHARS: 36,
  SPEND_KEEP_DAYS: 10,
};

// ── 日期工具 ──
export function taipeiDate(now = Date.now()) {
  return new Date(now + 8 * 3600e3).toISOString().slice(0, 10);
}
export function utcDate(now = Date.now()) {
  return new Date(now).toISOString().slice(0, 10);
}
function num(v, d) {
  const x = parseFloat(v);
  return Number.isFinite(x) ? x : d;
}

// ── 價格與上限（wrangler [vars]）──
export function prices(env) {
  return {
    livePerMin: num(env.LIVE_USD_PER_MIN, 0.05),
    search: num(env.SEARCH_USD, 0.01),
    inPerM: num(env.BACKEND_IN_USD_PER_M, 0.2),
    cachedPerM: num(env.BACKEND_CACHED_USD_PER_M, 0.02),
    outPerM: num(env.BACKEND_OUT_USD_PER_M, 1.2),
  };
}
export function quotaLimits(env) {
  const t = parseInt(env.DAILY_MAX_TURNS, 10);
  return { turns: Number.isFinite(t) ? t : 200, usd: num(env.DAILY_MAX_USD, 2) };
}
export function liveUsd(env, seconds) {
  return (Math.max(0, seconds) / 60) * prices(env).livePerMin;
}
// Responses usage（input_tokens／output_tokens／input_tokens_details.cached_tokens）＋搜尋次數 → 美金
export function backendUsd(env, usage, searches) {
  const p = prices(env);
  let usd = (searches || 0) * p.search;
  if (usage) {
    const inp = usage.input_tokens || 0;
    const cached = (usage.input_tokens_details && usage.input_tokens_details.cached_tokens) || 0;
    const out = usage.output_tokens || 0;
    usd += ((inp - cached) * p.inPerM + cached * p.cachedPerM + out * p.outPerM) / 1e6;
  }
  return usd;
}

// ── 天氣 ──
export function parseTown(s) {
  const t = (s || DEFAULT_TOWN).trim().replace(/^台/, "臺");
  const county = t.slice(0, 3);
  const town = t.slice(3) || "";
  const ds = CWA_3DAY[county];
  return ds ? { county, town, dataset: "F-D0047-" + ds } : null;
}
export function wxIcon(desc, code) {
  const c = parseInt(code, 10);
  const d = desc || "";
  if (d.includes("雷") || (c >= 15 && c <= 18) || (c >= 33 && c <= 36) || c === 41) return "storm";
  if (d.includes("雨") || d.includes("雪")) return "rain";
  if (d.startsWith("晴") && !d.includes("雲")) return "sun";
  if (d.includes("晴")) return "part";
  if (d) return "cloud";
  if (c === 1) return "sun";
  if (c >= 2 && c <= 3) return "part";
  return "cloud";
}
const pick = (o, ...keys) => { for (const k of keys) if (o && o[k] != null) return o[k]; return undefined; };
// 新舊兩種 JSON 格式都吃（2024 起欄位改大寫開頭、元素名改中文；舊版是 T／RH／PoP6h／Wx）
export function parseCwa(json, now = Date.now()) {
  const rec = pick(json, "records") || {};
  const locs = pick(rec, "Locations", "locations") || [];
  const loc0 = Array.isArray(locs) ? locs[0] : locs;
  const list = pick(loc0, "Location", "location") || [];
  const loc = Array.isArray(list) ? list[0] : list;
  if (!loc) throw new Error("CWA 回應沒有地點資料");
  const els = pick(loc, "WeatherElement", "weatherElement") || [];
  const find = (...names) => els.find((e) => {
    const n = String(pick(e, "ElementName", "elementName") || "");
    return names.some((x) => n === x);
  });
  const valueAt = (el, keys, strict = false) => {
    if (!el) return undefined;
    const times = pick(el, "Time", "time") || [];
    let best = null, bestD = Infinity;
    for (const t of times) {
      const st = Date.parse(pick(t, "StartTime", "startTime", "DataTime", "dataTime") || "");
      const en = Date.parse(pick(t, "EndTime", "endTime") || "");
      let d;
      if (Number.isFinite(en)) d = en <= now ? Infinity : Math.max(0, st - now);   // 區間：還沒結束的第一段
      else d = Math.abs(st - now);                                                  // 時間點：最接近現在
      if (d < bestD) { bestD = d; best = t; }
    }
    if (!best) return undefined;
    const vals = pick(best, "ElementValue", "elementValue") || [];
    const v0 = Array.isArray(vals) ? vals[0] : vals;
    if (v0 == null) return undefined;
    if (typeof v0 !== "object") return v0;
    for (const k of keys) if (v0[k] != null) return v0[k];
    return strict ? undefined : Object.values(v0)[0];
  };
  const wxEl = find("天氣現象", "Wx");
  const desc = valueAt(wxEl, ["Weather", "value"]);
  const code = valueAt(wxEl, ["WeatherCode"], true);   // 舊格式沒有這欄 → 圖示只看文字
  const n = (x) => { const v = parseFloat(x); return Number.isFinite(v) ? Math.round(v) : null; };
  return {
    temp: n(valueAt(find("溫度", "T"), ["Temperature", "value"])),
    rh: n(valueAt(find("相對濕度", "RH"), ["RelativeHumidity", "value"])),
    pop: n(valueAt(find("3小時降雨機率", "6小時降雨機率", "PoP3h", "PoP6h", "PoP12h"), ["ProbabilityOfPrecipitation", "value"])),
    desc: desc ? String(desc) : "",
    code: code != null ? String(code) : "",
    town: String(pick(loc, "LocationName", "locationName") || ""),
  };
}

// ── 快訊（RSS）──
function decodeEntities(s) {
  return s
    .replace(/<!\[CDATA\[([\s\S]*?)\]\]>/g, "$1")
    .replace(/&#x([0-9a-f]+);/gi, (_, h) => String.fromCodePoint(parseInt(h, 16)))
    .replace(/&#(\d+);/g, (_, d) => String.fromCodePoint(parseInt(d, 10)))
    .replace(/&quot;/g, '"').replace(/&apos;/g, "'").replace(/&lt;/g, "<").replace(/&gt;/g, ">")
    .replace(/&nbsp;/g, " ").replace(/&amp;/g, "&");
}
export function parseRss(xml) {
  const items = [];
  const re = /<(item|entry)\b[\s\S]*?<\/\1>/gi;
  let m;
  while ((m = re.exec(xml))) {
    const blk = m[0];
    const tm = /<title\b[^>]*>([\s\S]*?)<\/title>/i.exec(blk);
    if (!tm) continue;
    const title = decodeEntities(tm[1]).replace(/<[^>]+>/g, "").replace(/\s+/g, " ").trim();
    const dm = /<(pubDate|updated|published|dc:date)\b[^>]*>([\s\S]*?)<\/\1>/i.exec(blk);
    const t = dm ? Date.parse(decodeEntities(dm[2]).trim()) : NaN;
    if (title) items.push({ title, t: Number.isFinite(t) ? t : 0 });
  }
  return items;
}
export function cleanTitle(s, max = INFO.NEWS_MAX_CHARS) {
  let t = s.replace(/[｜|]\s*(中央社|CNA).*$/i, "").replace(/\s+/g, " ").trim();
  const chars = [...t];
  return chars.length > max ? chars.slice(0, max).join("") : t;
}
export function mergeNews(lists, n = INFO.NEWS_N) {
  const seen = new Set(), all = [];
  for (const l of lists) for (const it of l) {
    const k = it.title;
    if (seen.has(k)) continue;
    seen.add(k);
    all.push(it);
  }
  all.sort((a, b) => b.t - a.t);
  return all.slice(0, n).map((x) => cleanTitle(x.title));
}

// ── 帳上餘額 ──
// costs：{sum（CREDIT_SINCE 起到 through 前一天為止）, through（UTC 日期，Costs 沒涵蓋的第一天）}
// spendByUtc：{ "YYYY-MM-DD": 中控自己記的美金 }
export function balance(env, costs, spendByUtc, spendTotal) {
  const credit = parseFloat(env.CREDIT_USD);
  if (!Number.isFinite(credit)) return null;
  if (costs && costs.through) {
    let local = 0;
    for (const [d, v] of Object.entries(spendByUtc || {})) if (d >= costs.through) local += v;
    return credit - costs.sum - local;
  }
  return credit - (spendTotal || 0);
}

const round2 = (x) => Math.round(x * 100) / 100;
const round3 = (x) => Math.round(x * 1000) / 1000;
const sec = (ms) => Math.floor(ms / 1000);

export class InfoHub {
  // broadcast(obj)：送給目前所有連著的裝置
  constructor(storage, env, broadcast) {
    this.storage = storage;
    this.env = env;
    this.broadcast = broadcast;
    this.pendingFn = () => 0;   // GptLink 設定：還開著的 session 估計花費（美金）
    this.lastForce = 0;
    this.refreshing = null;
    this.errors = {};   // 最近一次各來源的錯誤（/info 頁面顯示，部署後除錯用）
  }
  log(s) { console.log("[info] " + s); }
  fail(src, msg) { this.errors[src] = msg; this.log(src + "：" + msg); return null; }

  // ── 花費記帳（GptLink 呼叫）──
  async addSpend(usd, why) {
    if (!(usd > 0)) return;
    // 今日花費放獨立的 usd:<台北日期>（不和 GptLink 的 quota:<日期> 共用物件，避免兩邊讀改寫互蓋）
    const qk = "usd:" + taipeiDate();
    const uk = "spendutc:" + utcDate();
    const [q, u, tot, anc] = await Promise.all([this.storage.get(qk), this.storage.get(uk), this.storage.get("spend:total"), this.anchor()]);
    const day = (q || 0) + usd;
    const put = { [qk]: day, [uk]: (u || 0) + usd, "spend:total": (tot || 0) + usd };
    if (anc) put["bal:anchor"] = { id: anc.id, spent: anc.spent + usd };   // 校正後的累計花費
    await this.storage.put(put);
    this.log(`記帳 +$${usd.toFixed(4)}（${why}）→ 今日 $${day.toFixed(3)}`);
    await this.pushUsd();
  }
  // 11-21 餘額校正（wrangler [vars] BALANCE_USD＋BALANCE_SET）：回傳 {id, usd, spent}；沒設＝null。
  // BALANCE_SET 換了（重新校正）就把累計花費歸零
  async anchor() {
    const usd = parseFloat(this.env.BALANCE_USD);
    const id = String(this.env.BALANCE_SET || "").trim();
    if (!Number.isFinite(usd) || !id) return null;
    const a = await this.storage.get("bal:anchor");
    if (a && a.id === id) return { id, usd, spent: a.spent || 0 };
    await this.storage.put("bal:anchor", { id, spent: 0 });
    this.log(`餘額校正：$${usd}（${id}），累計花費歸零`);
    return { id, usd, spent: 0 };
  }
  async todayUsd() {
    const d = (await this.storage.get("usd:" + taipeiDate())) || 0;
    return d + (this.pendingFn() || 0);
  }
  async spendByUtc() {
    const m = await this.storage.list({ prefix: "spendutc:" });
    const out = {};
    for (const [k, v] of m) out[k.slice(9)] = v;
    return out;
  }
  async pruneSpend() {
    const m = await this.storage.list({ prefix: "spendutc:" });
    const cut = utcDate(Date.now() - INFO.SPEND_KEEP_DAYS * 86400e3);
    const costs = await this.storage.get("info:costs");
    const old = [...m.keys()].filter((k) => k.slice(9) < cut && (!costs || k.slice(9) < costs.through));
    const today = "usd:" + taipeiDate();
    const oldUsd = [...(await this.storage.list({ prefix: "usd:" })).keys()].filter((k) => k < today);
    old.push(...oldUsd);
    if (old.length) await this.storage.delete(old);
  }
  async usdMsg() {
    const L = quotaLimits(this.env);
    const [today, costs, byUtc, tot, anc] = await Promise.all([
      this.todayUsd(), this.storage.get("info:costs"), this.spendByUtc(), this.storage.get("spend:total"), this.anchor(),
    ]);
    // 有校正就用校正值－之後的花費（還開著的 session 估計花費也扣）；沒有才用 Costs API 推算
    const left = anc ? anc.usd - anc.spent - (this.pendingFn() || 0) : balance(this.env, costs, byUtc, tot);
    const m = { t: "info", k: "usd", today: round3(today), cap: round2(L.usd), at: sec(Date.now()) };
    if (left != null) m.left = round2(left);
    return m;
  }
  async pushUsd() {
    const m = await this.usdMsg();
    this.broadcast(m);
    await this.storage.put("info:lastDay", taipeiDate());
  }

  // ── B5（11-21）：給 Friday 的「機器與即時資訊」快照（GPT-Live session 開始時接在指示後面）──
  // 新聞直接念這裡的標題（不用上網查、省搜尋費）；額度、電量、版本等機器本身的問題照這裡回答。
  async briefing() {
    const tpe = (ms) => new Date(ms + 8 * 3600e3).toISOString().slice(5, 16).replace("T", " ");
    const ago = (ms) => { const m = Math.round((Date.now() - ms) / 60000); return m < 1 ? "剛剛" : m < 60 ? `${m} 分鐘前` : `${Math.round(m / 60)} 小時前`; };
    const L = quotaLimits(this.env);
    const [u, wx, nw, dev, q] = await Promise.all([
      this.usdMsg(), this.storage.get("info:wx"), this.storage.get("info:news"), this.storage.get("dev:status"),
      this.storage.get("quota:" + taipeiDate()),
    ]);
    const lines = [`【機器與即時資訊】（快照時間：台北 ${tpe(Date.now())}；問到這些直接照這裡回答，不用上網查）`];
    const used = u.today || 0;
    // 11-21：餘額單獨一行排最前面（原本跟「今天還剩」寫在同一行，Friday 問餘額時答成今日剩餘）
    if (u.left != null)
      lines.push(`- 帳上餘額：$${u.left.toFixed(2)}（OpenAI 帳戶還剩的錢，跟 M5S3Pack 資訊頁顯示的是同一個數字）。` +
        `使用者問「還剩多少」「餘額」「額度剩多少」「還有多少錢」一律答這個數字；只有明確問「今天還能用多少」才答下一行的每日剩餘`);
    lines.push(`- 每日上限：今天已用 $${used.toFixed(2)}／每日上限 $${L.usd}（今天還能再用 $${Math.max(0, L.usd - used).toFixed(2)}）；今天第 ${(q && q.turns) || 0} 輪對話（上限 ${L.turns} 輪）`);
    if (dev) {
      const chg = dev.chg === 2 ? "插著線、已充飽" : dev.chg === 1 ? "插著線充電中" : "沒插線";
      const net = dev.net && dev.net !== "-" ? `Wi-Fi 連在${dev.net}、訊號 ${dev.rssi} dBm` : "Wi-Fi 狀態不明";
      const upMin = Math.round((dev.up || 0) / 60);
      lines.push(`- M5S3Pack 本機（${ago(dev.at)}回報）：電量 ${dev.bat != null && dev.bat >= 0 ? dev.bat + "%" : "讀不到"}${dev.mv ? `（電池 ${(dev.mv / 1000).toFixed(2)}V，${chg}）` : `（${chg}）`}；${net}；韌體版本 ${dev.ver || "?"}；開機 ${upMin >= 60 ? Math.floor(upMin / 60) + " 小時 " + (upMin % 60) + " 分" : upMin + " 分鐘"}`);
    }
    if (wx) lines.push(`- 天氣（${wx.loc || "本地"}，${ago(wx.fetched)}更新）：${wx.desc || ""}，${wx.temp ?? "?"}°C、濕度 ${wx.rh ?? "?"}%、降雨機率 ${wx.pop ?? "?"}%`);
    if (nw && nw.items && nw.items.length) {
      lines.push(`- 中央社快訊（${ago(nw.fetched)}更新，使用者說「播新聞」「有什麼新聞」就照順序簡短念這幾則，每則一句；要細節或更新的再上網查）：`);
      nw.items.forEach((t, i) => lines.push(`  ${i + 1}. ${t}`));
    }
    return lines.join("\n");
  }
  async saveDevStatus(msg) {
    const n = (x, lo, hi) => (Number.isFinite(+x) ? Math.min(hi, Math.max(lo, Math.round(+x))) : null);
    const s = (x) => String(x || "").slice(0, 12);
    await this.storage.put("dev:status", {
      at: Date.now(), bat: n(msg.bat, -1, 100), chg: n(msg.chg, 0, 2), mv: n(msg.mv, 0, 6000),
      ver: s(msg.ver), net: s(msg.net), rssi: n(msg.rssi, -127, 0), up: n(msg.up, 0, 1e9),
    });
  }

  // ── 快取訊息 ──
  async cachedMsgs() {
    const out = [await this.usdMsg()];
    const wx = await this.storage.get("info:wx");
    if (wx) out.push(wxMsg(wx));
    const nw = await this.storage.get("info:news");
    if (nw) out.push(...newsMsgs(nw));
    return out;
  }

  // ── 定時更新（cron 每 30 分鐘、裝置連上、info_req）──
  async refresh(force = false) {
    if (this.refreshing) return this.refreshing;
    this.refreshing = (async () => {
      const now = Date.now();
      const out = [];
      try {
        const [wx, nw, costs, lastDay] = await Promise.all([
          this.storage.get("info:wx"), this.storage.get("info:news"), this.storage.get("info:costs"), this.storage.get("info:lastDay"),
        ]);
        const jobs = [];
        const guard = (src, p) => p.catch((e) => this.fail(src, String(e && e.message || e)));
        if (force || !wx || now - wx.fetched > INFO.WX_EVERY_MS - 60e3) jobs.push(guard("wx", this.fetchWx()).then((w) => w && out.push(wxMsg(w))));
        if (force || !nw || now - nw.fetched > INFO.NEWS_EVERY_MS - 60e3) jobs.push(guard("news", this.fetchNews()).then((n) => n && out.push(...newsMsgs(n))));
        let usdDirty = lastDay !== taipeiDate();   // 換日（台北）→ 今日花費歸零要送
        const costsAge = costs ? now - costs.fetched : Infinity;
        if (costsAge > INFO.COSTS_MIN_MS && (force || costsAge > INFO.COSTS_EVERY_MS - 60e3 || costs.through < utcDate())) {
          jobs.push(guard("costs", this.fetchCosts()).then((c) => { if (c) usdDirty = true; }));
        }
        await Promise.all(jobs.map((p) => p.catch((e) => this.log("更新失敗 " + (e && e.message)))));
        if (usdDirty || force) out.unshift(await this.usdMsg());
        await this.storage.put("info:lastDay", taipeiDate());
        await this.pruneSpend();
        for (const m of out) this.broadcast(m);
      } finally {
        this.refreshing = null;
      }
      return out.length;
    })();
    return this.refreshing;
  }
  async forceRefresh() {
    if (Date.now() - this.lastForce < INFO.FORCE_MIN_MS) return 0;
    this.lastForce = Date.now();
    return this.refresh(true);
  }

  async fetchWx() {
    const key = this.env.CWA_KEY;
    if (!key) return this.fail("wx", "沒設定 CWA_KEY（wrangler secret put CWA_KEY）");
    const tw = parseTown(this.env.WX_TOWN);
    if (!tw) return this.fail("wx", `WX_TOWN 格式錯（${[...(this.env.WX_TOWN || "")].length} 字；要「縣市＋鄉鎮區」，例：臺北市中正區）`);
    const u = `https://opendata.cwa.gov.tw/api/v1/rest/datastore/${tw.dataset}?Authorization=${encodeURIComponent(key)}&format=JSON` +
      (tw.town ? "&LocationName=" + encodeURIComponent(tw.town) : "");
    let r;
    try {
      // 照實表明身分的 User-Agent（Workers 預設不帶；不帶時被氣象署前端擋下）
      r = await fetch(u, { headers: { accept: "application/json", "user-agent": "sticks3-hub/1.0 (personal desk weather display)" } });
    } catch (e) {
      return this.fail("wx", `連不到氣象署（${tw.dataset}）：${e && e.message}`);
    }
    const body = await r.text();
    if (!r.ok) return this.fail("wx", `氣象署 HTTP ${r.status}（${tw.dataset} ${tw.town}）：${body.slice(0, 200)}`);
    let j, w;
    try {
      j = JSON.parse(body);
      w = parseCwa(j);
    } catch (e) {
      return this.fail("wx", `解析失敗（${tw.dataset} ${tw.town}）：${e && e.message}｜回應開頭：${body.slice(0, 300)}`);
    }
    if (w.temp == null && !w.desc) return this.fail("wx", `回應裡找不到溫度與天氣（${tw.dataset} ${tw.town}）｜回應開頭：${body.slice(0, 300)}`);
    const loc = (w.town || tw.town || tw.county).replace(/[區鄉鎮市]$/, "");
    const out = { fetched: Date.now(), loc, temp: w.temp, rh: w.rh, pop: w.pop, desc: w.desc, ic: wxIcon(w.desc, w.code) };
    await this.storage.put("info:wx", out);
    delete this.errors.wx;
    this.log(`天氣 ${tw.county}${tw.town}：${out.desc} ${out.temp}°C 濕度 ${out.rh}% 降雨 ${out.pop}% → ${out.ic}`);
    return out;
  }

  async fetchNews() {
    const urls = (this.env.NEWS_FEEDS ? String(this.env.NEWS_FEEDS).split(",") : DEFAULT_FEEDS).map((s) => s.trim()).filter(Boolean);
    const lists = await Promise.all(urls.map(async (u) => {
      try {
        const r = await fetch(u, { headers: { "user-agent": "Mozilla/5.0 (sticks3-hub; personal desk device)", accept: "application/rss+xml, application/xml, text/xml" } });
        if (!r.ok) { this.log(`快訊 ${u} HTTP ${r.status}`); return []; }
        return parseRss(await r.text());
      } catch (e) {
        this.log(`快訊 ${u} 失敗 ${e && e.message}`);
        return [];
      }
    }));
    const items = mergeNews(lists);
    if (!items.length) { this.log("快訊：全部來源都沒有資料，保留上次"); return null; }
    const out = { fetched: Date.now(), items };
    await this.storage.put("info:news", out);
    this.log(`快訊 ${items.length} 則：${items[0]}…`);
    return out;
  }

  // Costs API：CREDIT_SINCE（UTC 日期）00:00 起，按天（UTC）彙總；只算到昨天（今天的桶還在累積）
  async fetchCosts() {
    const key = this.env.OPENAI_ADMIN_KEY;
    const since = String(this.env.CREDIT_SINCE || "").trim();
    if (!key || !/^\d{4}-\d{2}-\d{2}$/.test(since)) {
      if (!key) this.log("餘額：沒設定 OPENAI_ADMIN_KEY，用中控自己記的花費估");
      return null;
    }
    const today = utcDate();
    const start = Math.floor(Date.parse(since + "T00:00:00Z") / 1000);
    const end = Math.floor(Date.parse(today + "T00:00:00Z") / 1000);
    let sum = 0, page = null, n = 0;
    if (end > start) {
      for (let guard = 0; guard < 10; guard++) {
        const q = new URLSearchParams({ start_time: String(start), end_time: String(end), bucket_width: "1d", limit: "180" });
        if (page) q.set("page", page);
        const r = await fetch("https://api.openai.com/v1/organization/costs?" + q, { headers: { authorization: "Bearer " + key } });
        if (!r.ok) throw new Error("Costs API HTTP " + r.status + " " + (await r.text()).slice(0, 200));
        const j = await r.json();
        for (const b of j.data || []) {
          n++;
          for (const res of b.results || []) sum += num(res.amount && res.amount.value, 0);
        }
        if (!j.has_more || !j.next_page) break;
        page = j.next_page;
      }
    }
    const out = { fetched: Date.now(), sum, through: today, days: n };
    await this.storage.put("info:costs", out);
    this.log(`Costs API：${since} 到 ${today} 前共 $${sum.toFixed(3)}（${n} 天）`);
    return out;
  }
}

export function wxMsg(w) {
  const m = { t: "info", k: "wx", at: sec(w.fetched), loc: w.loc || "", ic: w.ic || "cloud", s: [...(w.desc || "")].slice(0, 16).join("") };
  if (w.temp != null) m.temp = w.temp;
  if (w.rh != null) m.rh = w.rh;
  if (w.pop != null) m.pop = w.pop;
  return m;
}
export function newsMsgs(nw) {
  const n = nw.items.length;
  return nw.items.map((s, i) => ({ t: "info", k: "nw", i, n, at: sec(nw.fetched), s }));
}
