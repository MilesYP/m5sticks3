#include "net_hub.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <WebSocketsClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <atomic>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adpcm.h"
#include "audio_io.h"
#include "config.h"
#include "secrets.h"

// B3.5：中控要求連線帶 token（?t=HUB_TOKEN），沒有就連不上 → 編譯時直接擋下
#ifndef HUB_TOKEN
#error "secrets.h 缺少 HUB_TOKEN：照 secrets.h.example 加一行 #define HUB_TOKEN \"...\"（值與 wrangler secret 的 HUB_TOKEN 相同）"
#endif

namespace {

// ═══════════════════════════════════════════════════════════════════════
//  跨 task 交接（B3b-5）
// ═══════════════════════════════════════════════════════════════════════

// ── (1) 指令佇列：主迴圈 → 網路 task ──
enum class Cmd : uint8_t { UploadBegin, UploadFinish, UploadAbort, Ping, DropLink, WifiOn, WifiOff };
struct CmdMsg {
  Cmd c;
  uint32_t id;
  char up[8];
  char down[10];
  char pace[6];
};
QueueHandle_t s_cmdq = nullptr;
TaskHandle_t s_task = nullptr;

void post(const CmdMsg& m) {
  if (s_cmdq) xQueueSend(s_cmdq, &m, pdMS_TO_TICKS(50));
}
void postSimple(Cmd c) {
  CmdMsg m{};
  m.c = c;
  post(m);
}

// ── (2) 收件佇列：網路 task → 主迴圈（單一生產者／單一消費者，PSRAM）──
// 每則：4 bytes 標頭（bit31＝二進位、其餘＝長度）＋資料，補齊到 4 的倍數。
// 尾端放不下一整則時寫一個「繞回」標頭，從頭開始寫。讀寫位置只增不減，取餘數得實際位置。
constexpr uint32_t RX_CAP      = 1024 * 1024;
constexpr uint32_t RX_MIN_FREE = 32 * 1024;     // 剩不到這麼多就暫停 ws.loop()（反壓；單則最大約 15KB）
constexpr uint32_t RX_BIN      = 0x80000000u;
constexpr uint32_t RX_WRAP     = 0x7FFFFFFFu;
uint8_t* s_rx = nullptr;
std::atomic<uint32_t> s_rx_w{0}, s_rx_r{0};

uint32_t rxFree() { return RX_CAP - (s_rx_w.load(std::memory_order_relaxed) - s_rx_r.load(std::memory_order_acquire)); }

bool rxPush(bool bin, const uint8_t* d, size_t len) {
  if (!s_rx || len >= RX_CAP / 2) return false;
  const uint32_t need = 4 + (((uint32_t)len + 3) & ~3u);
  uint32_t w = s_rx_w.load(std::memory_order_relaxed);
  const uint32_t r = s_rx_r.load(std::memory_order_acquire);
  uint32_t pos = w % RX_CAP;
  const uint32_t tail = RX_CAP - pos;
  const uint32_t total = need + (tail < need ? tail : 0);
  if (RX_CAP - (w - r) < total) return false;
  if (tail < need) {
    memcpy(s_rx + pos, &RX_WRAP, 4);
    w += tail;
    pos = 0;
  }
  const uint32_t h = (uint32_t)len | (bin ? RX_BIN : 0);
  memcpy(s_rx + pos, &h, 4);
  memcpy(s_rx + pos + 4, d, len);
  s_rx_w.store(w + need, std::memory_order_release);
  return true;
}

// 看佇列最前面那則（不取出）；沒有就回 false
bool rxPeek(bool* bin, const uint8_t** d, size_t* len) {
  if (!s_rx) return false;
  uint32_t r = s_rx_r.load(std::memory_order_relaxed);
  const uint32_t w = s_rx_w.load(std::memory_order_acquire);
  if (r == w) return false;
  uint32_t pos = r % RX_CAP;
  uint32_t h;
  memcpy(&h, s_rx + pos, 4);
  if (h == RX_WRAP) {
    r += RX_CAP - pos;
    s_rx_r.store(r, std::memory_order_release);
    if (r == w) return false;
    pos = 0;
    memcpy(&h, s_rx, 4);
  }
  *bin = (h & RX_BIN) != 0;
  *len = h & ~RX_BIN;
  *d = s_rx + pos + 4;
  return true;
}

void rxPop(size_t len) {
  const uint32_t r = s_rx_r.load(std::memory_order_relaxed);
  s_rx_r.store(r + 4 + (((uint32_t)len + 3) & ~3u), std::memory_order_release);
}

// ── (3) log 佇列：網路 task 寫、主迴圈代印（序列埠只有主迴圈在寫，USB 沒接時也不會卡住網路 task）──
constexpr uint32_t LOG_CAP = 4096;
char s_log[LOG_CAP];
std::atomic<uint32_t> s_log_w{0}, s_log_r{0}, s_log_drop{0};

void netlog(const char* fmt, ...) {   // 只在網路 task 呼叫（單一生產者）
  char line[224];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  if (n <= 0) return;
  if (n >= (int)sizeof(line)) n = sizeof(line) - 1;
  const uint32_t w = s_log_w.load(std::memory_order_relaxed);
  const uint32_t r = s_log_r.load(std::memory_order_acquire);
  if (LOG_CAP - (w - r) < (uint32_t)n) { s_log_drop.fetch_add(1); return; }
  for (int i = 0; i < n; ++i) s_log[(w + i) % LOG_CAP] = line[i];
  s_log_w.store(w + n, std::memory_order_release);
}

// ── (4) 主迴圈也會讀的狀態（atomic）──
std::atomic<bool>     s_link{false};
std::atomic<uint32_t> s_down_since{0};     // 這次斷線的時刻（0＝開機後還沒連上過）
std::atomic<uint32_t> s_ping_ms{20000};
std::atomic<bool>     s_job_active{false}, s_job_done{false};
std::atomic<uint32_t> s_job_sent{0}, s_resends{0}, s_lost{0}, s_tx_audio{0};
std::atomic<bool>     s_perf_reset{false};
std::atomic<bool>     s_ps_manual{false};

// Wi-Fi 層事件（在 Wi-Fi 事件 task 裡只記錄，網路 task 再印）
std::atomic<uint8_t>  s_wifi_reason{0};
std::atomic<bool>     s_wifi_drop_new{false};

void onWifiDisconnected(arduino_event_id_t, arduino_event_info_t info) {
  s_wifi_reason.store(info.wifi_sta_disconnected.reason);
  s_wifi_drop_new.store(true);
}

// 主迴圈的回呼
net_hub::StateCb    s_state_cb = nullptr;
net_hub::BinCb      s_bin_cb = nullptr;
net_hub::AudioEndCb s_end_cb = nullptr;
net_hub::FormatCb   s_fmt_cb = nullptr;
net_hub::BinGate    s_gate = nullptr;

// ═══════════════════════════════════════════════════════════════════════
//  以下只在網路 task 裡用
// ═══════════════════════════════════════════════════════════════════════
WebSocketsClient s_ws;
bool     s_ws_started = false;
bool     s_wifi_on = true;
bool     s_wifi_logged = false;
uint32_t s_last_ping = 0, s_ping_sent = 0;
uint32_t s_link_since = 0, s_last_pong = 0, s_last_rtt = 0;
uint32_t s_rx_link = 0, s_tx_link = 0, s_drops = 0;

// 量測（網路 task 寫、主迴圈讀；只是診斷數字，不需要嚴格同步）
struct Perf { uint32_t n = 0; uint64_t sum = 0; uint32_t max = 0;
  void add(uint32_t us) { ++n; sum += us; if (us > max) max = us; } };
Perf s_p_loop, s_p_send;
uint32_t s_bins_total = 0, s_rx_paused = 0, s_rx_peak = 0, s_rx_dropped = 0;

// ── 上傳工作 ──
constexpr size_t MSG_BYTES = 8000;   // 每則 8000 bytes（鐵則七第 5 點：大訊息比較快）
uint8_t s_tx[MSG_BYTES];
struct Job {
  bool active = false, start_sent = false, stop_sent = false, finishing = false, resend = false, mulaw = true;
  uint32_t id = 0, gen = 0, pos = 0;
  char start_json[128] = "";
} s_job;

void jobOnLinkLost() {
  if (s_job.active && s_job.start_sent && !s_job.stop_sent) {
    s_job.start_sent = false;
    s_job.resend = true;
    netlog("[up] 上傳途中斷線 → 連回後用同一個 id（%lu）從頭重送\n", (unsigned long)s_job.id);
  }
}

void logLinkDown(const char* why, size_t len) {
  const uint32_t now = millis();
  ++s_drops;
  char wifi[48];
  if (WiFi.status() == WL_CONNECTED) snprintf(wifi, sizeof(wifi), "Wi-Fi 層仍連著");
  else snprintf(wifi, sizeof(wifi), "Wi-Fi 層也斷（原因 %u）", (unsigned)s_wifi_reason.load());
  netlog("[link] 斷線 #%lu｜連線維持 %.1f s｜距上次 pong %.1f s（最後 RTT %lu ms）｜這次連線收 %.1f KB／送 %.1f KB｜"
         "RSSI %d dBm｜省電 %s｜%s｜原因：%.*s\n",
         (unsigned long)s_drops, (now - s_link_since) / 1000.0f,
         s_last_pong ? (now - s_last_pong) / 1000.0f : -1.0f, (unsigned long)s_last_rtt,
         s_rx_link / 1024.0f, s_tx_link / 1024.0f, (int)WiFi.RSSI(), WiFi.getSleep() ? "開" : "關",
         wifi, len ? (int)len : 1, len ? why : "-");
}

void markDown() {
  uint32_t t = millis();
  if (t == 0) t = 1;
  s_down_since.store(t);
  s_link.store(false);
  jobOnLinkLost();
}

void onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      const uint32_t now = millis();
      if (s_down_since.load() || s_drops)
        netlog("[link] 已連線（斷線 %.1f s 後恢復）\n", (now - s_down_since.load()) / 1000.0f);
      s_link_since = now;
      s_last_pong = 0; s_last_rtt = 0;
      s_rx_link = 0; s_tx_link = 0;
      s_down_since.store(0);
      s_link.store(true);
      netlog("[hub] WS connected\n");
      break;
    }
    case WStype_DISCONNECTED:
      if (s_link.load()) {   // 連著才算一次斷線（重連失敗也會進來，不重複記）
        logLinkDown((const char*)payload, length);
        markDown();
      }
      // payload 是斷線原因；token 錯誤時為 "WebSocket handshake failed - HTTP 401"
      netlog("[hub] WS disconnected%s%.*s\n", length ? ": " : "", (int)length, length ? (const char*)payload : "");
      break;
    case WStype_TEXT:
      s_rx_link += length;
      if (length < 64 && strstr((const char*)payload, "\"t\":\"pong\"")) {   // pong 在這裡算 RTT，不進佇列
        s_last_pong = millis();
        if (s_ping_sent) {
          s_last_rtt = s_last_pong - s_ping_sent;
          s_ping_sent = 0;
          netlog("[hub] <- pong（RTT %lu ms）\n", (unsigned long)s_last_rtt);
        }
        break;
      }
      if (!rxPush(false, payload, length)) { ++s_rx_dropped; netlog("[hub] 收件佇列滿，丟掉一則文字！\n"); }
      break;
    case WStype_BIN:
      ++s_bins_total;
      s_rx_link += length;
      if (!rxPush(true, payload, length)) { ++s_rx_dropped; netlog("[hub] 收件佇列滿，丟掉一則音訊！\n"); }
      break;
    default:
      break;
  }
}

void handleCmd(const CmdMsg& m) {
  switch (m.c) {
    case Cmd::UploadBegin: {
      s_job = Job();
      s_job.active = true;
      s_job.id = m.id;
      s_job.gen = audio_io::recGen();
      s_job.mulaw = strcmp(m.up, "mulaw") == 0;
      s_job.pos = audio_io::recFrom();
      snprintf(s_job.start_json, sizeof(s_job.start_json),
               "{\"t\":\"start\",\"id\":%lu,\"up\":\"%s\",\"down\":\"%s\",\"pace\":\"%s\"}",
               (unsigned long)m.id, m.up, m.down, m.pace);
      s_job_sent.store(0);
      s_job_done.store(false);
      s_job_active.store(true);
      break;
    }
    case Cmd::UploadFinish:
      s_job.finishing = true;
      break;
    case Cmd::UploadAbort:
      s_job.active = false;
      s_job_active.store(false);
      break;
    case Cmd::Ping:
      if (!s_link.load()) { netlog("[diag] 中控未連線，沒送 ping\n"); break; }
      s_ping_sent = millis();
      s_ws.sendTXT("{\"t\":\"ping\"}");
      netlog("[diag] 已送 ping（等 pong 算 RTT）\n");
      break;
    case Cmd::DropLink:
      if (s_ws_started) s_ws.disconnect();
      netlog("[diag] 已斷開 WebSocket（模擬連線重置），等自動重連\n");
      break;
    case Cmd::WifiOn:
      s_wifi_on = true;
      WiFi.mode(WIFI_STA);
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      netlog("[diag] Wi-Fi → 開（等 [hub] WS connected）\n");
      break;
    case Cmd::WifiOff:
      s_wifi_on = false;
      if (s_ws_started) s_ws.disconnect();
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      netlog("[diag] Wi-Fi → 關\n");
      break;
  }
}

// 上傳：每圈最多送一則（讓 ws.loop 與指令有機會跑）
void uploadStep() {
  Job& j = s_job;
  if (!j.active || !s_link.load()) return;
  if (audio_io::recGen() != j.gen) {   // 已經按下新的一輪，這份錄音不再有效
    j.active = false;
    s_job_active.store(false);
    return;
  }
  if (!j.start_sent) {
    if (!audio_io::recArmed()) return;   // B3b-5 補（Miles 選 A）：按滿最短時間才送 start；誤觸時中控完全不知道
    if (!s_ws.sendTXT(j.start_json)) return;
    j.start_sent = true;
    j.pos = audio_io::recFrom();
    s_job_sent.store(0);
    if (j.resend) {
      s_resends.fetch_add(1);
      netlog("[up] 已重送 start（id %lu），從頭重送錄音\n", (unsigned long)j.id);
    }
  }
  const bool ended = audio_io::recEnded();   // 先讀「放開了沒」，再讀上限（audio_io 依此順序寫入）
  const uint32_t limit = audio_io::recLimit();
  const uint32_t oldest = audio_io::recOldest();
  if (j.pos < oldest) {   // 網路停擺太久，最舊的錄音已被覆蓋
    s_lost.fetch_add(oldest - j.pos);
    netlog("[up] ⚠ %lu 個取樣沒送出就被錄音覆蓋（網路停擺過久）\n", (unsigned long)(oldest - j.pos));
    j.pos = oldest;
  }
  const uint32_t avail = limit > j.pos ? limit - j.pos : 0;
  const uint32_t per = j.mulaw ? MSG_BYTES : MSG_BYTES / 2;   // 一則幾個取樣
  if (avail >= per || (ended && avail > 0)) {
    const uint32_t n = avail < per ? avail : per;
    uint32_t k = 0;
    while (k < n) {
      size_t contig = 0;
      const int16_t* p = audio_io::recPtr(j.pos + k, &contig);
      uint32_t m = n - k;
      if (contig < m) m = (uint32_t)contig;
      if (j.mulaw) {
        for (uint32_t i = 0; i < m; ++i) s_tx[k + i] = adpcm::pcmToMulaw(p[i]);
      } else {
        memcpy(s_tx + k * 2, p, m * 2);
      }
      k += m;
    }
    const size_t bytes = j.mulaw ? n : n * 2;
    const uint32_t t0 = micros();
    const bool ok = s_ws.sendBIN(s_tx, bytes);
    s_p_send.add(micros() - t0);
    if (!ok) return;   // 送失敗通常接著是斷線事件，那裡會安排重送
    j.pos += n;
    s_tx_link += bytes;
    s_tx_audio.fetch_add(bytes);
    s_job_sent.store(j.pos - audio_io::recFrom());
    return;
  }
  if (ended && j.finishing && j.pos >= limit && !j.stop_sent) {
    if (s_ws.sendTXT("{\"t\":\"stop\"}")) {
      j.stop_sent = true;
      j.active = false;
      s_job_active.store(false);
      s_job_done.store(true);
    }
  }
}

void netTask(void*) {
  for (;;) {
    if (s_perf_reset.exchange(false)) {
      s_p_loop = Perf(); s_p_send = Perf();
      s_bins_total = 0; s_rx_paused = 0; s_rx_peak = 0;
    }
    CmdMsg m;
    while (s_cmdq && xQueueReceive(s_cmdq, &m, 0) == pdTRUE) handleCmd(m);

    if (s_wifi_drop_new.exchange(false)) {
      netlog("[wifi] 斷線，原因碼 %u（RSSI %d dBm）\n", (unsigned)s_wifi_reason.load(), (int)WiFi.RSSI());
      s_wifi_logged = false;
    }
    const bool wifi = s_wifi_on && WiFi.status() == WL_CONNECTED;
    if (wifi) {
      if (!s_wifi_logged) {
        s_wifi_logged = true;
        netlog("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
      }
      if (!s_ws_started) {
        // wss://HUB_HOST:443/ws?t=<token>（B3.5）。不要把這個網址印出來
        s_ws.beginSSL(HUB_HOST, HUB_PORT, HUB_PATH "?t=" HUB_TOKEN);
        s_ws.onEvent(onWsEvent);
        s_ws.setReconnectInterval(2000);   // B3b-5：重連在背景做，不卡主迴圈 → 縮短到 2 秒
        s_ws_started = true;
      }
    }
    // 鐵則七第 3 點：Wi-Fi 沒連上就不跑 ws.loop()（DNS 查詢會卡好幾秒）；Wi-Fi 掉線時直接視為中斷
    if (s_ws_started && !wifi) {
      if (s_link.load()) {
        logLinkDown("Wi-Fi 未連線", strlen("Wi-Fi 未連線"));
        markDown();
        netlog("[hub] Wi-Fi 未連線，WS 視為中斷（Wi-Fi 回來後自動重連）\n");
      }
    } else if (s_ws_started) {
      const uint32_t used = RX_CAP - rxFree();
      if (used > s_rx_peak) s_rx_peak = used;
      if (rxFree() >= RX_MIN_FREE) {
        const uint32_t t0 = micros();
        s_ws.loop();
        s_p_loop.add(micros() - t0);
      } else {
        ++s_rx_paused;   // 主迴圈還沒消化完（播放緩衝滿）→ 先不收，TCP 會讓中控等
      }
    }

    uploadStep();

    const uint32_t now = millis();
    const uint32_t pi = s_ping_ms.load();
    if (s_link.load() && pi && now - s_last_ping > pi) {   // 預設每 20 秒 ping 一次保活
      s_last_ping = now;
      s_ping_sent = now;
      s_ws.sendTXT("{\"t\":\"ping\"}");
      s_tx_link += 12;
    }
    vTaskDelay(1);   // 讓出 CPU：core 0 的 idle task 要跑得到，否則看門狗會重開機
  }
}

// ── 主迴圈：處理收到的文字訊息（極簡 JSON 解析，不引函式庫）──
bool getStr(const char* json, const char* key, char* out, size_t cap) {
  char pat[24];
  snprintf(pat, sizeof(pat), "\"%s\":\"", key);
  const char* p = strstr(json, pat);
  if (!p || cap == 0) return false;
  p += strlen(pat);
  size_t i = 0;
  while (*p && *p != '"' && i < cap - 1) out[i++] = *p++;
  out[i] = '\0';
  return true;
}
uint32_t getNum(const char* json, const char* key) {
  char pat[24];
  snprintf(pat, sizeof(pat), "\"%s\":", key);
  const char* p = strstr(json, pat);
  return p ? (uint32_t)strtoul(p + strlen(pat), nullptr, 10) : 0;
}

void handleText(const char* js) {
  if (strstr(js, "\"t\":\"audio_end\"")) {
    float g = 1.0f;
    const char* gp = strstr(js, "\"g\":");
    if (gp) g = strtof(gp + 4, nullptr);
    char fmt[12] = "";
    getStr(js, "fmt", fmt, sizeof(fmt));
    if (s_end_cb) s_end_cb(getNum(js, "n"), getNum(js, "pcm"), g, fmt);
    return;
  }
  if (strstr(js, "\"t\":\"format\"")) {
    char up[12] = "", down[12] = "";
    getStr(js, "up", up, sizeof(up));
    getStr(js, "down", down, sizeof(down));
    if (s_fmt_cb) s_fmt_cb(up, down);
    return;
  }
  char v[16];
  if (s_state_cb && getStr(js, "v", v, sizeof(v))) s_state_cb(v);
}

void drainLog() {
  uint32_t r = s_log_r.load(std::memory_order_relaxed);
  const uint32_t w = s_log_w.load(std::memory_order_acquire);
  while (r != w) {
    const uint32_t pos = r % LOG_CAP;
    uint32_t n = w - r;
    if (n > LOG_CAP - pos) n = LOG_CAP - pos;
    Serial.write((const uint8_t*)s_log + pos, n);
    r += n;
  }
  s_log_r.store(r, std::memory_order_release);
  const uint32_t d = s_log_drop.exchange(0);
  if (d) Serial.printf("[net] log 佇列滿，少印 %lu 行\n", (unsigned long)d);
}

}  // namespace

namespace net_hub {

void begin() {
  WiFi.onEvent(onWifiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);   // B3b-4：記 Wi-Fi 層斷線原因
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[wifi] connecting to \"%s\" ...\n", WIFI_SSID);
  s_rx = (uint8_t*)ps_malloc(RX_CAP);
  if (!s_rx) Serial.println("[net] ⚠ 收件佇列（PSRAM 1MB）配置失敗");
  s_cmdq = xQueueCreate(16, sizeof(CmdMsg));
  xTaskCreatePinnedToCore(netTask, "net", cfg::NET_TASK_STACK, nullptr, cfg::NET_TASK_PRIO, &s_task,
                          cfg::NET_TASK_CORE);
  Serial.printf("[net] 網路 task 啟動（core %d、優先權 %lu、堆疊 %lu bytes）\n", cfg::NET_TASK_CORE,
                (unsigned long)cfg::NET_TASK_PRIO, (unsigned long)cfg::NET_TASK_STACK);
}

void loop() {
  drainLog();
  // 依序處理收到的訊息。音訊收不下（播放緩衝滿）就停在這則，保持順序，下一圈再試。
  const uint32_t t0 = millis();
  for (int i = 0; i < 16 && millis() - t0 < 15; ++i) {
    bool bin = false;
    const uint8_t* d = nullptr;
    size_t len = 0;
    if (!rxPeek(&bin, &d, &len)) break;
    if (bin) {
      if (s_gate && !s_gate(len)) break;
      if (s_bin_cb) s_bin_cb(d, len);
    } else {
      char js[512];
      const size_t n = len < sizeof(js) - 1 ? len : sizeof(js) - 1;
      memcpy(js, d, n);
      js[n] = '\0';
      Serial.printf("[hub] <- %s\n", js);
      handleText(js);
    }
    rxPop(len);
  }
}

bool wifiUp() { return WiFi.status() == WL_CONNECTED; }
bool linkUp() { return s_link.load(); }
void onState(StateCb cb) { s_state_cb = cb; }
void onBinary(BinCb cb) { s_bin_cb = cb; }
void onAudioEnd(AudioEndCb cb) { s_end_cb = cb; }
void onFormat(FormatCb cb) { s_fmt_cb = cb; }
void onBinGate(BinGate g) { s_gate = g; }

void uploadBegin(uint32_t id, const char* up, const char* down, const char* pace) {
  CmdMsg m{};
  m.c = Cmd::UploadBegin;
  m.id = id;
  strncpy(m.up, up, sizeof(m.up) - 1);
  strncpy(m.down, down, sizeof(m.down) - 1);
  strncpy(m.pace, pace, sizeof(m.pace) - 1);
  s_job_done.store(false);   // 主迴圈馬上讀得到正確值，不用等網路 task 處理指令
  s_job_sent.store(0);
  post(m);
}
void uploadFinish() { postSimple(Cmd::UploadFinish); }
void uploadAbort() { postSimple(Cmd::UploadAbort); }
bool uploadDone() { return s_job_done.load(); }
uint32_t uploadSentSamples() { return s_job_sent.load(); }
uint32_t uploadResends() { return s_resends.load(); }
uint32_t uploadLostSamples() { return s_lost.load(); }
uint32_t txBytesTotal() { return s_tx_audio.load(); }

void perfReset() { s_perf_reset.store(true); }

const char* perfLine() {
  static char buf[224];
  auto avg = [](const Perf& p) { return p.n ? (float)(p.sum / p.n) / 1000.0f : 0.0f; };
  const unsigned long stack_free = s_task ? (unsigned long)uxTaskGetStackHighWaterMark(s_task) : 0UL;
  snprintf(buf, sizeof(buf),
           "ws.loop 平均 %.1f／最長 %lu ms（%lu 次）｜送幀 平均 %.1f／最長 %lu ms（%lu 次）｜收幀 %lu｜"
           "收件佇列最高 %lu KB、暫停收 %lu 次｜網路 task 堆疊剩 %lu bytes",
           avg(s_p_loop), (unsigned long)(s_p_loop.max / 1000), (unsigned long)s_p_loop.n,
           avg(s_p_send), (unsigned long)(s_p_send.max / 1000), (unsigned long)s_p_send.n,
           (unsigned long)s_bins_total, (unsigned long)(s_rx_peak / 1024), (unsigned long)s_rx_paused,
           stack_free);
  return buf;
}

uint32_t linkDownMs() {
  if (s_link.load()) return 0;
  return millis() - s_down_since.load();   // 0（開機後還沒連上過）→ 從開機起算
}

void setPingInterval(uint32_t sec) {
  s_ping_ms.store(sec * 1000);
  if (sec) Serial.printf("[diag] ping 間隔 → %lu 秒\n", (unsigned long)sec);
  else     Serial.println("[diag] ping 間隔 → 不 ping（量測閒置斷線用）");
}

// ── B3.6 Wi-Fi 干擾量測 ───────────────────────────────────────────────────
void setWifiEnabled(bool on) { postSimple(on ? Cmd::WifiOn : Cmd::WifiOff); }
void pingNow() { postSimple(Cmd::Ping); }
void dropLink() { postSimple(Cmd::DropLink); }

// 省電與發射功率直接呼叫 esp_wifi（本身可跨 task 呼叫），不經網路 task
void setPowerSave(bool on) {
  s_ps_manual.store(true);
  WiFi.setSleep(on);   // on → WIFI_PS_MIN_MODEM（預設）；off → WIFI_PS_NONE
  Serial.printf("[diag] Wi-Fi 省電 → %s（手動，自動切換暫停；輸入 sa 恢復）\n", on ? "開" : "關");
}

void setPowerSaveAuto() {
  s_ps_manual.store(false);
  WiFi.setSleep(true);
  Serial.println("[diag] Wi-Fi 省電 → 自動（錄放音期間關、平常開）");
}

void voiceActive(bool active) {
  if (s_ps_manual.load()) return;
  if (WiFi.getSleep() == !active) return;   // 已經是要的狀態，不重複設定
  WiFi.setSleep(!active);
  Serial.printf("[wifi] 省電 → %s（%s）\n", active ? "關" : "開",
                active ? "錄放音中，避免滋滋聲" : "錄放音結束");
}

void setTxPowerDbm(int dbm) {
  wifi_power_t p;
  if      (dbm >= 18) p = WIFI_POWER_19_5dBm;
  else if (dbm >= 14) p = WIFI_POWER_15dBm;
  else if (dbm >= 10) p = WIFI_POWER_11dBm;
  else if (dbm >= 7)  p = WIFI_POWER_8_5dBm;
  else if (dbm >= 4)  p = WIFI_POWER_5dBm;
  else                p = WIFI_POWER_2dBm;
  bool ok = WiFi.setTxPower(p);
  Serial.printf("[diag] 發射功率設定%s → %s\n", ok ? "" : "失敗", configLabel());
}

const char* configLabel() {
  static char buf[64];
  const char* link = s_link.load() ? "WS" : (WiFi.status() == WL_CONNECTED ? "up" : "down");
  wifi_ps_type_t ps = WIFI_PS_NONE;
  int8_t q = 0;   // 單位 0.25 dBm
  const bool ps_ok = (esp_wifi_get_ps(&ps) == ESP_OK);
  const bool tx_ok = (esp_wifi_get_max_tx_power(&q) == ESP_OK);
  char tx[12];
  if (tx_ok) snprintf(tx, sizeof(tx), "%.1fdBm", q / 4.0f);
  else       snprintf(tx, sizeof(tx), "?");
  snprintf(buf, sizeof(buf), "wifi=%s ps=%s tx=%s", link,
           !ps_ok ? "?" : (ps == WIFI_PS_NONE ? "off" : "on"), tx);
  return buf;
}

}  // namespace net_hub
