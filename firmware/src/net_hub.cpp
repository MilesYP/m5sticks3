#include "net_hub.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_bt.h>
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
#include "battery_ui.h"
#include "config.h"
#include "info_data.h"
#include "secrets.h"

// B3.5：中控要求連線帶 token（?t=HUB_TOKEN），沒有就連不上 → 編譯時直接擋下
#ifndef HUB_TOKEN
#error "secrets.h 缺少 HUB_TOKEN：照 secrets.h.example 加一行 #define HUB_TOKEN \"...\"（值與 wrangler secret 的 HUB_TOKEN 相同）"
#endif

namespace net_hub { extern std::atomic<bool> s_voice_on; void applyIdlePs(); }   // 定義在下面（錄放音中、省電深度）

namespace {

// ═══════════════════════════════════════════════════════════════════════
//  跨 task 交接（B3b-5）
// ═══════════════════════════════════════════════════════════════════════

// ── (1) 指令佇列：主迴圈 → 網路 task ──
enum class Cmd : uint8_t { UploadBegin, UploadFinish, UploadAbort, Ping, DropLink, WifiOn, WifiOff, InfoReq, DevStatus };
struct CmdMsg {
  Cmd c;
  uint32_t id;
  char up[8];
  char down[10];
  char pace[6];
  char mode[6];   // B3c："gpt"／"echo"
  char txt[96];   // B5：DevStatus 的 JSON 欄位（主迴圈填，網路 task 補 Wi-Fi 後送出）
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
net_hub::ErrorCb    s_err_cb = nullptr;
net_hub::TextCb     s_text_cb = nullptr;
net_hub::BinGate    s_gate = nullptr;

// ═══════════════════════════════════════════════════════════════════════
//  以下只在網路 task 裡用
// ═══════════════════════════════════════════════════════════════════════
WebSocketsClient s_ws;
bool     s_ws_started = false;
bool     s_wifi_on = true;
bool     s_need_pick = true;   // 11-21：網路 task 要重新掃描挑 Wi-Fi（家裡優先，見 wifiPickStep）
bool     s_wifi_logged = false;
uint32_t s_last_ping = 0, s_ping_sent = 0;
uint32_t s_link_since = 0, s_last_pong = 0;
std::atomic<uint32_t> s_last_rtt{0};   // 網路 task 寫入，資訊頁讀取
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
  char start_json[160] = "";
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
         s_last_pong ? (now - s_last_pong) / 1000.0f : -1.0f, (unsigned long)s_last_rtt.load(),
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
          netlog("[hub] <- pong（RTT %lu ms）\n", (unsigned long)s_last_rtt.load());
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

const char* netTag();   // 目前連的是哪一組 Wi-Fi（「家裡」／「熱點」），定義在 Wi-Fi 清單後面

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
               "{\"t\":\"start\",\"id\":%lu,\"up\":\"%s\",\"down\":\"%s\",\"pace\":\"%s\",\"mode\":\"%s\"}",
               (unsigned long)m.id, m.up, m.down, m.pace, m.mode);
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
    case Cmd::InfoReq:   // A5-2：序列埠 a → 中控重送資訊頁資料並強制更新
      if (!s_link.load()) { netlog("[info] 中控未連線，沒送 info_req\n"); break; }
      s_ws.sendTXT("{\"t\":\"info_req\"}");
      netlog("[info] 已要求中控重送資料（info_req）\n");
      break;
    case Cmd::DevStatus: {   // B5：機器狀態給中控（Friday 回答「還剩多少額度／電量」等用；不印序列埠，避免洗版）
      if (!s_link.load()) break;
      char js[200];
      const bool up = WiFi.status() == WL_CONNECTED;
      snprintf(js, sizeof js, "{\"t\":\"dev\",%s,\"net\":\"%s\",\"rssi\":%d}", m.txt,
               up ? netTag() : "-", up ? (int)WiFi.RSSI() : 0);
      s_ws.sendTXT(js);
      break;
    }
    case Cmd::DropLink:
      if (s_ws_started) s_ws.disconnect();
      netlog("[diag] 已斷開 WebSocket（模擬連線重置），等自動重連\n");
      break;
    case Cmd::WifiOn:
      s_wifi_on = true;
      WiFi.mode(WIFI_STA);
      s_need_pick = true;   // 11-21：交給 wifiPickStep 掃描挑網路（家裡優先）
      // 31-m4e：重開 Wi-Fi 先試「關之前連著的那一組」（原本會輪到下一組，家裡→先試熱點、白等 10 秒）
      s_net_idx = s_net_idx < 0 ? -1 : s_net_idx - 1;
      net_hub::applyIdlePs();   // P2：重開 Wi-Fi 後重套省電深度（錄放音中由 voiceActive 管）
      netlog("[wifi] Wi-Fi → 開（等 [hub] WS connected）\n");
      break;
    case Cmd::WifiOff:
      s_wifi_on = false;
      if (s_ws_started) s_ws.disconnect();
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      netlog("[wifi] Wi-Fi → 關\n");
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

// ── 11-21 Miles：多組 Wi-Fi，家裡優先 ──
// secrets.h 有定義 WIFI_SSID_HOME／WIFI_PASS_HOME 時，家裡排第一、手機熱點（WIFI_SSID）第二。
// 沒連上：照順序輪流試（每組 10 秒）；連在熱點上：每 60 秒背景掃描，掃到家裡就換過去（錄放音中、上傳還沒送完都不換）。
struct WifiNet { const char* ssid; const char* pass; const char* tag; };
const WifiNet kNets[] = {
#if defined(WIFI_SSID_HOME) && defined(WIFI_PASS_HOME)
  {WIFI_SSID_HOME, WIFI_PASS_HOME, "家裡"},
#endif
  {WIFI_SSID, WIFI_PASS, "熱點"},
};
constexpr int kNetCount = sizeof(kNets) / sizeof(kNets[0]);
bool     s_scanning = false;
int      s_net_idx = -1;
uint32_t s_wifi_try_ms = 0, s_last_pref_scan = 0;
const char* netTag() { return (s_net_idx >= 0 && s_net_idx < kNetCount) ? kNets[s_net_idx].tag : "-"; }

void wifiConnect(int i) {
  s_net_idx = i;
  s_wifi_try_ms = millis();
  WiFi.disconnect(false);
  WiFi.begin(kNets[i].ssid, kNets[i].pass);
  netlog("[wifi] connecting to \"%s\"（%s）...\n", kNets[i].ssid, kNets[i].tag);
}

// 11-21（27-a7o）實機：沒連上時掃描一律失敗或掃到 0 個（ESP32 正在試連時掃描會被打斷），
// 所以「沒連上」改成照優先順序輪流直接試：家裡 → 熱點 → 家裡…，每組最多等 WIFI_TRY_MS；
// 掃描只在「已經連在熱點上」時做（這時掃描正常，11-21 實測掃到家裡 -45 dBm），掃到家裡就換過去。
constexpr uint32_t WIFI_TRY_MS = 10000;
bool s_was_up = false;

void wifiPickStep(bool wifi) {
  if (!s_wifi_on) { s_scanning = false; s_was_up = false; return; }
  const uint32_t now = millis();
  if (s_was_up && !wifi) {   // 剛斷線：從最優先的那組重新輪
    s_net_idx = -1;
    s_wifi_try_ms = 0;
    s_scanning = false;
  }
  s_was_up = wifi;
  if (!wifi) {
    if (s_need_pick || s_net_idx < 0 || now - s_wifi_try_ms > WIFI_TRY_MS) {
      s_need_pick = false;
      wifiConnect(s_net_idx < 0 ? 0 : (s_net_idx + 1) % kNetCount);
    }
    return;
  }
  // 已連上：在熱點（不是第一優先）上才定期掃描看家裡回來了沒
  if (s_scanning) {
    const int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    s_scanning = false;
    int best = -1, rssi = 0;
    for (int i = 0; i < kNetCount && best < 0; ++i)
      for (int j = 0; j < n; ++j)
        if (WiFi.SSID(j) == kNets[i].ssid) { best = i; rssi = WiFi.RSSI(j); break; }
    WiFi.scanDelete();
    if (best >= 0 && best < s_net_idx) {
      netlog("[wifi] 掃到「%s」%d dBm，優先順序較高 → 換過去\n", kNets[best].ssid, rssi);
      wifiConnect(best);
    }
    return;
  }
  if (kNetCount > 1 && s_net_idx > 0 && !net_hub::s_voice_on.load() && !s_job.active &&
      now - s_last_pref_scan > 60000) {
    s_last_pref_scan = now;
    if (WiFi.scanNetworks(true) != WIFI_SCAN_FAILED) s_scanning = true;
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
      const unsigned rc = s_wifi_reason.load();
      const char* hint = (rc == 15 || rc == 2 || rc == 202 || rc == 204) ? "，可能是密碼錯"
                       : (rc == 201) ? "，找不到這個網路（不在範圍或不是 2.4GHz）" : "";
      netlog("[wifi] 斷線，原因碼 %u%s（RSSI %d dBm）\n", rc, hint, (int)WiFi.RSSI());
      s_wifi_logged = false;
    }
    const bool wifi = s_wifi_on && WiFi.status() == WL_CONNECTED;
    wifiPickStep(wifi);
    if (wifi) {
      if (!s_wifi_logged) {
        s_wifi_logged = true;
        netlog("[wifi] connected「%s」, IP %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
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
// B4：取 JSON 字串值並解開跳脫（\" \\ \/ \n \t \r \b \f \uXXXX→UTF-8，含代理對）。中文本身是 UTF-8 原字。
bool getStrJson(const char* json, const char* key, char* out, size_t cap) {
  char pat[24];
  snprintf(pat, sizeof(pat), "\"%s\":\"", key);
  const char* p = strstr(json, pat);
  if (!p || cap < 5) return false;
  p += strlen(pat);
  size_t i = 0;
  auto put = [&](uint32_t cp) {
    if (cp < 0x80) { if (i + 1 < cap) out[i++] = (char)cp; }
    else if (cp < 0x800) { if (i + 2 < cap) { out[i++] = 0xC0 | (cp >> 6); out[i++] = 0x80 | (cp & 0x3F); } }
    else if (cp < 0x10000) { if (i + 3 < cap) { out[i++] = 0xE0 | (cp >> 12); out[i++] = 0x80 | ((cp >> 6) & 0x3F); out[i++] = 0x80 | (cp & 0x3F); } }
    else { if (i + 4 < cap) { out[i++] = 0xF0 | (cp >> 18); out[i++] = 0x80 | ((cp >> 12) & 0x3F); out[i++] = 0x80 | ((cp >> 6) & 0x3F); out[i++] = 0x80 | (cp & 0x3F); } }
  };
  auto hex4 = [](const char* h, uint32_t* v) {
    uint32_t r = 0;
    for (int k = 0; k < 4; ++k) {
      const char c = h[k];
      r <<= 4;
      if (c >= '0' && c <= '9') r |= c - '0';
      else if (c >= 'a' && c <= 'f') r |= c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') r |= c - 'A' + 10;
      else return false;
    }
    *v = r;
    return true;
  };
  while (*p && *p != '"') {
    if (*p != '\\') { if (i + 1 < cap) out[i++] = *p; ++p; continue; }
    ++p;
    switch (*p) {
      case 'n': put('\n'); ++p; break;
      case 't': put(' '); ++p; break;
      case 'r': case 'b': case 'f': ++p; break;
      case 'u': {
        uint32_t cp;
        if (!hex4(p + 1, &cp)) { ++p; break; }
        p += 5;
        uint32_t lo;
        if (cp >= 0xD800 && cp < 0xDC00 && p[0] == '\\' && p[1] == 'u' && hex4(p + 2, &lo) && lo >= 0xDC00 && lo < 0xE000) {
          cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          p += 6;
        }
        put(cp);
        break;
      }
      case '\0': break;
      default: put((uint8_t)*p); ++p; break;   // \" \\ \/
    }
  }
  // 截斷時不要留下半個 UTF-8 字元
  if (i > 0) {
    size_t lead = i - 1;
    while (lead > 0 && ((uint8_t)out[lead] & 0xC0) == 0x80) --lead;   // 最後一個字的開頭
    const uint8_t c = (uint8_t)out[lead];
    const size_t need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    if (i - lead < need) i = lead;                                      // 不完整就整字拿掉
  }
  out[i] = '\0';
  return true;
}

uint32_t getNum(const char* json, const char* key);
// A5-2：數值（可有小數、負號）；沒有這個鍵回 dflt
float getFloat(const char* json, const char* key, float dflt) {
  char pat[24];
  snprintf(pat, sizeof(pat), "\"%s\":", key);
  const char* p = strstr(json, pat);
  return p ? strtof(p + strlen(pat), nullptr) : dflt;
}

// A5-2：資訊頁雲端資料 {"t":"info","k":"usd|wx|nw",…}（cloud/src/info.js）→ info_data；只存資料，不重畫
void handleInfo(const char* js) {
  char k[8] = "";
  getStr(js, "k", k, sizeof(k));
  const time_t at = (time_t)getNum(js, "at");
  if (!strcmp(k, "usd")) {
    const float today = getFloat(js, "today", 0), cap = getFloat(js, "cap", 0), left = getFloat(js, "left", -1);
    info_data::onUsd(today, cap, left, at);
    if (left >= 0) Serial.printf("[info] 額度 今日 $%.3f／上限 $%.2f｜餘額 $%.2f\n", today, cap, left);
    else Serial.printf("[info] 額度 今日 $%.3f／上限 $%.2f｜餘額（沒設定 CREDIT_USD）\n", today, cap);
  } else if (!strcmp(k, "wx")) {
    char ic[8] = "", loc[24] = "", d[64] = "";
    getStr(js, "ic", ic, sizeof(ic));
    getStrJson(js, "loc", loc, sizeof(loc));
    getStrJson(js, "s", d, sizeof(d));
    const int temp = (int)getFloat(js, "temp", -999), rh = (int)getFloat(js, "rh", -1), pop = (int)getFloat(js, "pop", -1);
    info_data::onWeather(ic, temp, rh, pop, loc, d, at);
    Serial.printf("[info] 天氣 %s %s %d°C 濕度 %d%% 降雨 %d%%（%s）\n", loc, d, temp, rh, pop, ic);
  } else if (!strcmp(k, "nw")) {
    char t[128] = "";
    getStrJson(js, "s", t, sizeof(t));
    const int i = (int)getNum(js, "i"), n = (int)getNum(js, "n");
    info_data::onNewsItem(i, n, t, at);
    if (i == 0) Serial.printf("[info] 快訊 %d 則：%s…\n", n, t);
  }
}

uint32_t getNum(const char* json, const char* key) {
  char pat[24];
  snprintf(pat, sizeof(pat), "\"%s\":", key);
  const char* p = strstr(json, pat);
  return p ? (uint32_t)strtoul(p + strlen(pat), nullptr, 10) : 0;
}

void handleText(const char* js) {
  if (strstr(js, "\"t\":\"info\"")) { handleInfo(js); return; }   // A5-2（要在下面「有 v 就當 state」之前）
  if (strstr(js, "\"t\":\"audio_end\"")) {
    float g = 1.0f;
    const char* gp = strstr(js, "\"g\":");
    if (gp) g = strtof(gp + 4, nullptr);
    char fmt[12] = "";
    getStr(js, "fmt", fmt, sizeof(fmt));
    if (s_end_cb) s_end_cb(getNum(js, "n"), getNum(js, "pcm"), g, fmt);
    return;
  }
  if (strstr(js, "\"t\":\"text\"")) {   // B4：逐字稿
    char role[12] = "", s[400];
    getStr(js, "role", role, sizeof(role));
    if (getStrJson(js, "s", s, sizeof(s)) && s_text_cb) s_text_cb(role, s, getNum(js, "ms"));
    return;
  }
  if (strstr(js, "\"t\":\"error\"")) {  // B3c：quota／openai…
    char code[16] = "", s[200] = "";
    getStr(js, "code", code, sizeof(code));
    getStrJson(js, "s", s, sizeof(s));
    if (s_err_cb) s_err_cb(code, s);
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
  WiFi.setAutoReconnect(false);   // 11-21：重連由 wifiPickStep 照優先順序自己做（函式庫的自動重連只會一直試同一組）
  Serial.printf("[wifi] 依優先順序連線（共 %d 組，前面的優先）...\n", kNetCount);   // 網路 task 掃完才連
  s_rx = (uint8_t*)ps_malloc(RX_CAP);
  if (!s_rx) Serial.println("[net] ⚠ 收件佇列（PSRAM 1MB）配置失敗");
  s_cmdq = xQueueCreate(16, sizeof(CmdMsg));
  xTaskCreatePinnedToCore(netTask, "net", cfg::NET_TASK_STACK, nullptr, cfg::NET_TASK_PRIO, &s_task,
                          cfg::NET_TASK_CORE);
  Serial.printf("[net] 網路 task 啟動（core %d、優先權 %lu、堆疊 %lu bytes）\n", cfg::NET_TASK_CORE,
                (unsigned long)cfg::NET_TASK_PRIO, (unsigned long)cfg::NET_TASK_STACK);
}

// B5：連上中控、每 60 秒、充電狀態變了，就送一則機器狀態（電量、充電、電壓、版本、開機多久）
void devStatusStep() {
  static bool was_up = false;
  static uint32_t last = 0;
  static int last_chg = -1;
  const bool up = s_link.load();
  const uint32_t now = millis();
  const int chg = battery_ui::chargeState();
  if (up && (!was_up || now - last >= 60000 || chg != last_chg)) {
    CmdMsg m{};
    m.c = Cmd::DevStatus;
    snprintf(m.txt, sizeof m.txt, "\"bat\":%d,\"chg\":%d,\"mv\":%d,\"ver\":\"%s\",\"up\":%lu",
             battery_ui::level(), chg, battery_ui::voltageMv(), audio_io::buildTag(), (unsigned long)(now / 1000));
    post(m);
    last = now;
    last_chg = chg;
  }
  was_up = up;
}

void loop() {
  drainLog();
  devStatusStep();
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
      if (!strstr(js, "\"t\":\"text\"") && !strstr(js, "\"t\":\"info\""))   // 逐字稿由 voice_turn、資訊由 handleInfo 整理後印
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
void onError(ErrorCb cb) { s_err_cb = cb; }
void onText(TextCb cb) { s_text_cb = cb; }
void onBinGate(BinGate g) { s_gate = g; }
void requestInfo() { postSimple(Cmd::InfoReq); }

void uploadBegin(uint32_t id, const char* up, const char* down, const char* pace, const char* mode) {
  CmdMsg m{};
  m.c = Cmd::UploadBegin;
  m.id = id;
  strncpy(m.up, up, sizeof(m.up) - 1);
  strncpy(m.down, down, sizeof(m.down) - 1);
  strncpy(m.pace, pace, sizeof(m.pace) - 1);
  strncpy(m.mode, mode, sizeof(m.mode) - 1);
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

uint32_t lastRttMs() { return s_last_rtt.load(); }
uint32_t pingIntervalSeconds() { return s_ping_ms.load() / 1000; }

void setPingInterval(uint32_t sec) {
  s_ping_ms.store(sec * 1000);
  if (sec) Serial.printf("[diag] ping 間隔 → %lu 秒\n", (unsigned long)sec);
  else     Serial.println("[diag] ping 間隔 → 不 ping（量測閒置斷線用）");
}

// ── B3.6 Wi-Fi 干擾量測 ───────────────────────────────────────────────────
void setWifiEnabled(bool on) { postSimple(on ? Cmd::WifiOn : Cmd::WifiOff); }

// P2：睡著／關螢幕滿 3 分鐘斷 Wi-Fi（power_save 決定）；叫醒時重連。斷著的時候不算「斷線」（不顯示斷線提示）
std::atomic<bool>     s_radio_off{false};
std::atomic<uint32_t> s_radio_on_t{0};
void setRadioOff(bool off, const char* why) {
  if (s_radio_off.load() == off) return;
  s_radio_off.store(off);
  if (!off) {
    const uint32_t now = millis();
    s_radio_on_t.store(now | 1);
    if (!s_link.load()) s_down_since.store(now | 1);   // 斷線提示從叫醒那一刻起算（見 OFFLINE_WAKE_GRACE_MS）
  }
  postSimple(off ? Cmd::WifiOff : Cmd::WifiOn);
  Serial.printf("[pwr] Wi-Fi %s（%s）\n", off ? "斷開省電" : "重連", why);
}
bool radioOff() { return s_radio_off.load(); }
uint32_t radioOnForMs() {
  const uint32_t t = s_radio_on_t.load();
  return t ? millis() - t : UINT32_MAX;
}
void pingNow() { postSimple(Cmd::Ping); }
void dropLink() { postSimple(Cmd::DropLink); }

// 省電與發射功率直接呼叫 esp_wifi（本身可跨 task 呼叫），不經網路 task
// P1：待機省電深度（power_save 決定）。錄放音中一律 NONE；平常 MIN_MODEM；想睡／睡著 MAX_MODEM。
std::atomic<bool> s_voice_on{false};
std::atomic<bool> s_idle_deep{false};

// M4（31-m4d）：藍牙控制器開著時 Wi-Fi 不能關省電（WIFI_PS_NONE），否則 ESP-IDF 直接 abort 重開
// （"Should enable WiFi modem sleep when both WiFi and Bluetooth are enabled"）。保險：藍牙還開著就退回 MIN_MODEM。
void setPsNone() {
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
    WiFi.setSleep(WIFI_PS_MIN_MODEM);
    Serial.println("[wifi] 藍牙還開著 → 不能關省電，維持 MIN_MODEM（錄音可能有滋滋聲）");
    return;
  }
  WiFi.setSleep(WIFI_PS_NONE);
}

void applyIdlePs() {
  WiFi.setSleep(s_idle_deep.load() ? WIFI_PS_MAX_MODEM : WIFI_PS_MIN_MODEM);
}

void setPowerSave(bool on) {
  s_ps_manual.store(true);
  if (on) WiFi.setSleep(true); else setPsNone();   // on → WIFI_PS_MIN_MODEM（預設）；off → WIFI_PS_NONE
  Serial.printf("[diag] Wi-Fi 省電 → %s（手動，自動切換暫停；輸入 sa 恢復）\n", on ? "開" : "關");
}

void setPowerSaveAuto() {
  s_ps_manual.store(false);
  if (s_voice_on.load()) setPsNone(); else applyIdlePs();
  Serial.println("[diag] Wi-Fi 省電 → 自動（錄放音期間關、平常開）");
}

void voiceActive(bool active) {
  if (s_ps_manual.load()) return;
  if (s_voice_on.load() == active) return;   // 已經是要的狀態，不重複設定
  s_voice_on.store(active);
  if (active) setPsNone();
  else applyIdlePs();
  Serial.printf("[wifi] 省電 → %s（%s）\n", active ? "關" : s_idle_deep.load() ? "深度" : "開",
                active ? "錄放音中，避免滋滋聲" : "錄放音結束");
}

void setIdleDeepSleep(bool deep) {
  s_idle_deep.store(deep);
  if (s_ps_manual.load() || s_voice_on.load()) return;   // 手動覆寫／錄放音中：等結束時再套用
  applyIdlePs();
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
