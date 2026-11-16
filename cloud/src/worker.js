// StickS3 中控 — Worker 進入點
// 路由：/health 健康檢查、/test 瀏覽器測試頁、/ws 升級成 WebSocket 交給 Durable Object。
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

    if (url.pathname === "/ws") {
      const id = env.VOICE.idFromName("miles"); // B1：先都連同一個 DO
      const stub = env.VOICE.get(id);
      return stub.fetch(request);
    }

    return new Response("StickS3 hub is running. 開 /test 測試、/ws 連線", { status: 200 });
  },
};

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
</style></head>
<body>
<h2>StickS3 中控 B1 測試</h2>
<small>這頁由中控自己提供，網址的 ws 會自動填好，直接按下面的按鈕就能測。</small>
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
$('conn').onclick=()=>{
  ws=new WebSocket(WS_URL);
  ws.onopen=()=>log('● 已連線','#7fdca0');
  ws.onclose=()=>log('○ 已斷線','#ff9');
  ws.onerror=()=>log('✕ 連線錯誤','#f88');
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
