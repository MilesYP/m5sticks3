// StickS3 中控 — Worker 進入點
// 路由：/health 健康檢查、/test 瀏覽器測試頁、/ws 升級成 WebSocket 交給 Durable Object。
//
// B3.5 連線 token 驗證：/ws 必須帶 ?t=<HUB_TOKEN>，不符就在升級成 WebSocket 之前回 401，
// 不建立連線、不進 Durable Object。HUB_TOKEN 用 `wrangler secret put HUB_TOKEN` 設定，不進 repo。
// 注意：token 在網址裡，`wrangler tail` 會印出完整網址 → 貼 tail 輸出給別人前先遮掉 ?t= 後面。
export { VoiceSession } from "./voice_session.js";

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    if (url.pathname === "/health") {
      return new Response("ok", { headers: { "content-type": "text/plain" } });
    }

    if (url.pathname === "/test") {
      return new Response(TEST_HTML, {
        headers: { "content-type": "text/html; charset=utf-8" },
      });
    }

    if (url.pathname === "/persona") {
      // Friday 的角色說明與使用者記憶（cloud/persona.sh 上傳）：要 token，存進同一個 DO 的 storage
      const auth = await checkToken(url.searchParams.get("t"), env.HUB_TOKEN);
      if (auth !== "ok") return new Response("unauthorized", { status: auth === "unset" ? 503 : 401 });
      return env.VOICE.get(env.VOICE.idFromName("miles")).fetch(request);
    }

    if (url.pathname === "/info") {
      // A5-2：查資訊頁雲端資料快取（?refresh=1 強制更新）；同樣要 token（含餘額）
      const auth = await checkToken(url.searchParams.get("t"), env.HUB_TOKEN);
      if (auth !== "ok") return new Response("unauthorized", { status: auth === "unset" ? 503 : 401 });
      return env.VOICE.get(env.VOICE.idFromName("miles")).fetch(request);
    }

    if (url.pathname === "/lab") {
      // B3c 先導測試：同樣要 token（會花 OpenAI 的錢），交給同一個 DO（錄音存在它的 storage）
      const auth = await checkToken(url.searchParams.get("t"), env.HUB_TOKEN);
      if (auth !== "ok") return new Response("unauthorized", { status: auth === "unset" ? 503 : 401 });
      return env.VOICE.get(env.VOICE.idFromName("miles")).fetch(request);
    }

    if (url.pathname === "/ws") {
      // B3.5：先驗 token，不符直接拒絕（不進 DO、不升級 WebSocket）
      const auth = await checkToken(url.searchParams.get("t"), env.HUB_TOKEN);
      if (auth === "unset") {
        // 中控沒設 HUB_TOKEN → 一律拒絕（fail closed），避免忘了設定時變成全部放行
        return new Response("hub token not configured", { status: 503 });
      }
      if (auth !== "ok") {
        return new Response("unauthorized", { status: 401 });
      }

      const id = env.VOICE.idFromName("miles"); // B1：先都連同一個 DO
      const stub = env.VOICE.get(id);
      return stub.fetch(request);
    }

    return new Response("StickS3 hub is running. 開 /test 測試、/ws 連線", { status: 200 });
  },

  // A5-2：Cron Trigger（wrangler.toml [triggers]，每 30 分鐘）→ 叫 DO 更新天氣、快訊、Costs，並推給連著的裝置。
  // /cron 只在這裡內部呼叫（fetch() 路由沒有轉送 /cron，外面打不進來）。
  async scheduled(event, env, ctx) {
    const stub = env.VOICE.get(env.VOICE.idFromName("miles"));
    ctx.waitUntil(stub.fetch("https://hub.internal/cron").then((r) => r.text()).then((t) => console.log("[cron] " + t.trim())));
  },
};

// ── B3.5：token 比對 ──
// 兩邊先各算 SHA-256 再用固定時間比較（crypto.subtle.timingSafeEqual，Cloudflare 內建）：
// 不論錯在第幾個字，比對時間都一樣，無法從回應快慢逐字猜 token；雜湊後長度固定，也不洩漏 token 長度。
async function sha256(text) {
  return crypto.subtle.digest("SHA-256", new TextEncoder().encode(text));
}

async function checkToken(given, expected) {
  if (!expected) return "unset";
  if (!given) return "bad";
  const [a, b] = await Promise.all([sha256(given), sha256(expected)]);
  return crypto.subtle.timingSafeEqual(a, b) ? "ok" : "bad";
}

const TEST_HTML = `<!doctype html><html lang="zh-Hant"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>StickS3 中控測試</title>
<style>
body{font:15px system-ui,"PingFang TC",sans-serif;max-width:640px;margin:24px auto;padding:0 16px;color:#15202a}
button{font:15px inherit;padding:9px 14px;border-radius:8px;border:0;background:#2f7de1;color:#fff;cursor:pointer}
button.sec{background:#e6e8eb;color:#111}
#log{margin-top:14px;background:#0d1218;color:#e6ecf2;border-radius:10px;padding:12px;height:340px;overflow:auto;white-space:pre-wrap;font:13px ui-monospace,monospace}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:10px 0}
.state{font-weight:700;font-size:18px}
small{color:#5a6672}
input[type=password]{flex:1;min-width:200px;font:15px inherit;padding:8px 10px;border:1px solid #c8ced4;border-radius:8px}
label{display:flex;gap:6px;align-items:center}
</style></head>
<body>
<h2>StickS3 中控測試</h2>
<small>這頁由中控自己提供，ws 位址會自動填好。先貼上 HUB_TOKEN 再按「連線」。</small>
<div class="row">
  <input id="tok" type="password" placeholder="貼上 HUB_TOKEN" autocomplete="off" spellcheck="false">
  <label><input type="checkbox" id="remember">記住在這個瀏覽器</label>
  <button id="forget" class="sec">清除</button>
</div>
<div class="row">
  <button id="conn">連線</button>
  <button id="disc" class="sec">斷線</button>
  <span>狀態：<span id="st" class="state">—</span></span>
</div>
<div class="row">
  <button id="start">長按 A（start）</button>
  <button id="stop" class="sec">放開 A（stop）</button>
  <button id="hist" class="sec">看對話紀錄</button>
  <button id="ping" class="sec">ping</button>
</div>
<div id="log"></div>
<script>
let ws=null;
const WS_URL=(location.protocol==='https:'?'wss://':'ws://')+location.host+'/ws';
const $=id=>document.getElementById(id);
function log(s,c){const d=document.createElement('div');if(c)d.style.color=c;d.textContent=s;$('log').appendChild(d);$('log').scrollTop=1e9;}
log('ws 位址：'+WS_URL,'#9cf');
// token 只存在這台電腦的這個瀏覽器（localStorage），頁面原始碼裡沒有 token
const KEY='sticks3_hub_token';
function lsGet(){try{return localStorage.getItem(KEY)||'';}catch(e){return '';}}
function lsSet(v){try{localStorage.setItem(KEY,v);}catch(e){}}
function lsDel(){try{localStorage.removeItem(KEY);}catch(e){}}
const saved=lsGet();
if(saved){$('tok').value=saved;$('remember').checked=true;log('已帶入這個瀏覽器記住的 token','#9cf');}
$('forget').onclick=()=>{lsDel();$('tok').value='';$('remember').checked=false;log('已清除記住的 token','#9cf');};
$('conn').onclick=()=>{
  if(ws&&ws.readyState<=1){log('已經在連線中，要重連先按「斷線」','#ff8');return;}
  const t=$('tok').value.trim();
  if($('remember').checked&&t)lsSet(t);else lsDel();
  if(!t)log('未填 token，照樣嘗試連線（應該會被拒）','#ff8');
  let opened=false;
  ws=new WebSocket(WS_URL+(t?'?t='+encodeURIComponent(t):''));
  ws.onopen=()=>{opened=true;log('● 已連線','#7fdca0');};
  ws.onclose=()=>{if(opened)log('○ 已斷線','#ff9');else log('✕ 連線被拒：token 未填或錯誤（或中控尚未設定 HUB_TOKEN）','#f88');};
  ws.onerror=()=>{};
  ws.onmessage=e=>{log('← '+e.data);try{const m=JSON.parse(e.data);if(m.t==='state')$('st').textContent=m.v;}catch{}};
};
$('disc').onclick=()=>{if(ws)ws.close();};
function send(o){if(!ws||ws.readyState!==1){log('尚未連線，先按「連線」','#ff8');return;}ws.send(JSON.stringify(o));log('→ '+JSON.stringify(o),'#9cf');}
$('start').onclick=()=>send({t:'start'});
$('stop').onclick=()=>send({t:'stop'});
$('hist').onclick=()=>send({t:'history'});
$('ping').onclick=()=>send({t:'ping'});
</script>
</body></html>`;
