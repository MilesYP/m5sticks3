#include "net_hub.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <stdio.h>
#include <WebSocketsClient.h>
#include <string.h>
#include <stdlib.h>

#include "secrets.h"

// B3.5：中控要求連線帶 token（?t=HUB_TOKEN），沒有就連不上 → 編譯時直接擋下
#ifndef HUB_TOKEN
#error "secrets.h 缺少 HUB_TOKEN：照 secrets.h.example 加一行 #define HUB_TOKEN \"...\"（值與 wrangler secret 的 HUB_TOKEN 相同）"
#endif

namespace {

WebSocketsClient s_ws;
bool s_link = false;
bool s_ws_started = false;
uint32_t s_last_ping = 0;
uint32_t s_ping_sent = 0;     // B3b-2：送出 ping 的時刻，收到 pong 時算往返時間（RTT）
net_hub::StateCb s_state_cb = nullptr;
net_hub::BinCb s_bin_cb = nullptr;
net_hub::AudioEndCb s_end_cb = nullptr;
net_hub::FormatCb s_fmt_cb = nullptr;

// ── B3b-4 斷線量測：每次斷線印一行 [link]，找連線重置（ssl -80）的規律 ──
uint32_t s_ping_ms = 20000;        // ping 間隔（序列埠 i<秒> 可改；0＝不 ping）
uint32_t s_link_since = 0;         // 這次連上的時刻
uint32_t s_down_since = 0;         // 這次斷線的時刻（開機時＝0，代表從開機起就沒連上）
uint32_t s_last_pong = 0;          // 最後一次收到 pong 的時刻
uint32_t s_last_rtt = 0;
uint32_t s_rx_link = 0, s_tx_link = 0;   // 這次連線期間收／送的 bytes
uint32_t s_drops = 0;              // 開機以來斷線次數
// Wi-Fi 層事件（在 Wi-Fi 事件 task 裡只記錄，主迴圈再印，避免在別的 task 印序列埠）
volatile uint8_t  s_wifi_reason = 0;     // 最後一次 Wi-Fi 斷線原因碼（ESP-IDF wifi_err_reason_t）
volatile uint32_t s_wifi_drop_ms = 0;    // 最後一次 Wi-Fi 斷線的時刻
volatile bool     s_wifi_drop_new = false;

void onWifiDisconnected(arduino_event_id_t, arduino_event_info_t info) {
  s_wifi_reason = info.wifi_sta_disconnected.reason;
  s_wifi_drop_ms = millis();
  s_wifi_drop_new = true;
}

void logLinkDown(const uint8_t* payload, size_t length) {
  const uint32_t now = millis();
  ++s_drops;
  char wifi[48];
  if (WiFi.status() == WL_CONNECTED) snprintf(wifi, sizeof(wifi), "Wi-Fi 層仍連著");
  else snprintf(wifi, sizeof(wifi), "Wi-Fi 層也斷（原因 %u）", (unsigned)s_wifi_reason);
  Serial.printf("[link] 斷線 #%lu｜連線維持 %.1f s｜距上次 pong %.1f s（最後 RTT %lu ms）｜這次連線收 %.1f KB／送 %.1f KB｜"
                "RSSI %d dBm｜省電 %s｜ping 間隔 %lu s｜%s｜原因：%.*s\n",
                (unsigned long)s_drops, (now - s_link_since) / 1000.0f,
                s_last_pong ? (now - s_last_pong) / 1000.0f : -1.0f, (unsigned long)s_last_rtt,
                s_rx_link / 1024.0f, s_tx_link / 1024.0f,
                (int)WiFi.RSSI(), WiFi.getSleep() ? "開" : "關", (unsigned long)(s_ping_ms / 1000), wifi,
                (int)length, length ? (const char*)payload : "-");
}

// 從 JSON 文字取出 "key":"value" 的 value（極簡，不引 JSON 函式庫）
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

// B3b-2 量測：累計耗時（微秒）
struct Perf { uint32_t n = 0; uint64_t sum = 0; uint32_t max = 0;
  void add(uint32_t us) { ++n; sum += us; if (us > max) max = us; } };
Perf s_p_loop, s_p_send;
uint32_t s_bins_this_call = 0, s_bins_total = 0, s_bins_max_per_call = 0;

// 極簡解析中控訊息裡的 "v":"xxx"（避免引入 JSON 函式庫）
void handleText(const uint8_t* payload, size_t len) {
  // B3b-2：pong → 印往返時間
  if (strstr((const char*)payload, "\"t\":\"pong\"")) {
    s_last_pong = millis();
    if (s_ping_sent) {
      s_last_rtt = millis() - s_ping_sent;
      Serial.printf("[hub] <- pong（RTT %lu ms）\n", (unsigned long)s_last_rtt);
      s_ping_sent = 0;
    } else {
      Serial.println("[hub] <- pong");
    }
    return;
  }
  // B3b-2：{"t":"audio_end","n":<bytes>}
  const char* js = (const char*)payload;
  if (strstr(js, "\"t\":\"audio_end\"")) {
    float g = 1.0f;
    const char* gp = strstr(js, "\"g\":");
    if (gp) g = strtof(gp + 4, nullptr);
    char fmt[12] = "";
    getStr(js, "fmt", fmt, sizeof(fmt));
    if (s_end_cb) s_end_cb(getNum(js, "n"), getNum(js, "pcm"), g, fmt);
    return;
  }
  if (strstr(js, "\"t\":\"format\"")) {   // B3b-3：中控確認的音訊格式
    char up[12] = "", down[12] = "";
    getStr(js, "up", up, sizeof(up));
    getStr(js, "down", down, sizeof(down));
    if (s_fmt_cb) s_fmt_cb(up, down);
    return;
  }
  const char* p = strstr((const char*)payload, "\"v\":\"");
  if (!p || !s_state_cb) return;
  p += 5;
  char buf[16];
  size_t i = 0;
  while (*p && *p != '"' && i < sizeof(buf) - 1) buf[i++] = *p++;
  buf[i] = '\0';
  s_state_cb(buf);
}

void onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      const uint32_t now = millis();
      if (s_down_since || s_drops) {
        Serial.printf("[link] 已連線（斷線 %.1f s 後恢復）\n", (now - s_down_since) / 1000.0f);
      }
      s_link = true;
      s_link_since = now;
      s_last_pong = 0; s_last_rtt = 0;
      s_rx_link = 0; s_tx_link = 0;
      s_down_since = 0;
      Serial.println("[hub] WS connected");
      break;
    }
    case WStype_DISCONNECTED:
      if (s_link) {                 // 連著才算一次斷線（重連失敗也會進來，不重複記）
        logLinkDown(payload, length);
        s_down_since = millis();
        if (s_down_since == 0) s_down_since = 1;
      }
      s_link = false;
      // payload 是斷線原因；token 錯誤時為 "WebSocket handshake failed - HTTP 401"
      if (length > 0) {
        Serial.printf("[hub] WS disconnected: %.*s\n", (int)length, (const char*)payload);
      } else {
        Serial.println("[hub] WS disconnected");
      }
      break;
    case WStype_TEXT:
      s_rx_link += length;
      if (!strstr((const char*)payload, "\"t\":\"pong\""))   // pong 由 handleText 連同 RTT 一起印
        Serial.printf("[hub] <- %.*s\n", (int)length, (const char*)payload);
      handleText(payload, length);
      break;
    case WStype_BIN:
      ++s_bins_this_call; ++s_bins_total;
      s_rx_link += length;
      if (s_bin_cb) s_bin_cb(payload, length);   // 音訊幀很多，不逐幀印
      break;
    default:
      break;
  }
}

}  // namespace

namespace net_hub {

void begin() {
  WiFi.onEvent(onWifiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);   // B3b-4：記 Wi-Fi 層斷線原因
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[wifi] connecting to \"%s\" ...\n", WIFI_SSID);
  // WebSocket 等 Wi-Fi 拿到 IP 後才開（見 loop），避免開頭 DNS 失敗
}

void loop() {
  static bool wifi_logged = false;
  if (s_wifi_drop_new) {   // B3b-4：Wi-Fi 層斷線（事件 task 記下，這裡印）
    s_wifi_drop_new = false;
    Serial.printf("[wifi] 斷線，原因碼 %u（RSSI %d dBm）\n", (unsigned)s_wifi_reason, (int)WiFi.RSSI());
    wifi_logged = false;   // 重新連上時再印一次 IP
  }
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifi_logged) {
      wifi_logged = true;
      Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
    }
    if (!s_ws_started) {
      // wss://HUB_HOST:443/ws?t=<token>（B3.5）。token 是 hex，不需網址編碼；不要把這個網址印出來
      s_ws.beginSSL(HUB_HOST, HUB_PORT, HUB_PATH "?t=" HUB_TOKEN);
      s_ws.onEvent(onWsEvent);
      s_ws.setReconnectInterval(5000);              // 斷線每 5 秒重連
      s_ws_started = true;
    }
  }
  // B3b-2：Wi-Fi 沒連上就不跑 WebSocket（否則它每 5 秒重連，DNS 查詢可能卡住主迴圈好幾秒）。
  // Wi-Fi 掉線時 WebSocket 自己不會馬上發現 → 這裡直接視為中斷，Wi-Fi 回來後 loop 會偵測並重連。
  if (s_ws_started && WiFi.status() != WL_CONNECTED) {
    if (s_link) {
      logLinkDown((const uint8_t*)"Wi-Fi 未連線", strlen("Wi-Fi 未連線"));
      s_down_since = millis();
      if (s_down_since == 0) s_down_since = 1;
      s_link = false;
      Serial.println("[hub] Wi-Fi 未連線，WS 視為中斷（Wi-Fi 回來後自動重連）");
    }
  } else if (s_ws_started) {
    s_bins_this_call = 0;
    const uint32_t t0 = micros();
    s_ws.loop();
    s_p_loop.add(micros() - t0);
    if (s_bins_this_call > s_bins_max_per_call) s_bins_max_per_call = s_bins_this_call;
  }

  const uint32_t now = millis();
  if (s_link && s_ping_ms && now - s_last_ping > s_ping_ms) {   // 預設每 20 秒 ping 一次保活（i<秒> 可改）
    s_last_ping = now;
    s_ping_sent = millis();
    s_ws.sendTXT("{\"t\":\"ping\"}");
    s_tx_link += 12;
  }
}

bool wifiUp() { return WiFi.status() == WL_CONNECTED; }
bool linkUp() { return s_link; }
void sendStart() { if (s_link) s_ws.sendTXT("{\"t\":\"start\"}"); }
void sendStop()  { if (s_link) s_ws.sendTXT("{\"t\":\"stop\"}"); }
void onState(StateCb cb) { s_state_cb = cb; }

bool sendBinary(const uint8_t* data, size_t len) {
  if (!s_link) return false;
  const uint32_t t0 = micros();
  const bool ok = s_ws.sendBIN(data, len);
  if (ok) s_tx_link += len;
  s_p_send.add(micros() - t0);
  return ok;
}

void perfReset() {
  s_p_loop = Perf(); s_p_send = Perf();
  s_bins_total = 0; s_bins_max_per_call = 0;
}

const char* perfLine() {
  static char buf[160];
  auto avg = [](const Perf& p) { return p.n ? (float)(p.sum / p.n) / 1000.0f : 0.0f; };
  snprintf(buf, sizeof(buf),
           "ws.loop 平均 %.1f／最長 %lu ms（%lu 次）｜送幀 平均 %.1f／最長 %lu ms（%lu 次）｜收幀 %lu（單次 loop 最多 %lu）",
           avg(s_p_loop), (unsigned long)(s_p_loop.max / 1000), (unsigned long)s_p_loop.n,
           avg(s_p_send), (unsigned long)(s_p_send.max / 1000), (unsigned long)s_p_send.n,
           (unsigned long)s_bins_total, (unsigned long)s_bins_max_per_call);
  return buf;
}
void onBinary(BinCb cb) { s_bin_cb = cb; }
void onAudioEnd(AudioEndCb cb) { s_end_cb = cb; }
void onFormat(FormatCb cb) { s_fmt_cb = cb; }
void sendStartFmt(const char* up, const char* down, const char* pace) {
  if (!s_link) return;
  char buf[96];
  if (pace) snprintf(buf, sizeof(buf), "{\"t\":\"start\",\"up\":\"%s\",\"down\":\"%s\",\"pace\":\"%s\"}", up, down, pace);
  else      snprintf(buf, sizeof(buf), "{\"t\":\"start\",\"up\":\"%s\",\"down\":\"%s\"}", up, down);
  s_ws.sendTXT(buf);
}

uint32_t linkDownMs() {
  if (s_link) return 0;
  return millis() - s_down_since;   // s_down_since=0（開機後還沒連上過）→ 從開機起算
}

void setPingInterval(uint32_t sec) {
  s_ping_ms = sec * 1000;
  if (sec) Serial.printf("[diag] ping 間隔 → %lu 秒\n", (unsigned long)sec);
  else     Serial.println("[diag] ping 間隔 → 不 ping（量測閒置斷線用）");
}

// ── B3.6 Wi-Fi 干擾量測 ───────────────────────────────────────────────────
void setWifiEnabled(bool on) {
  if (on) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);   // WebSocket 由函式庫每 5 秒自動重連
    Serial.println("[diag] Wi-Fi → 開（等 [hub] WS connected）");
  } else {
    s_ws.disconnect();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("[diag] Wi-Fi → 關");
  }
}

// 手動切換（diag 的 s0/s1）期間不讓自動切換蓋掉，量測才準
bool s_ps_manual = false;

void setPowerSave(bool on) {
  s_ps_manual = true;
  WiFi.setSleep(on);   // on → WIFI_PS_MIN_MODEM（預設）；off → WIFI_PS_NONE
  Serial.printf("[diag] Wi-Fi 省電 → %s（手動，自動切換暫停；輸入 sa 恢復）\n", on ? "開" : "關");
}

void setPowerSaveAuto() {
  s_ps_manual = false;
  WiFi.setSleep(true);
  Serial.println("[diag] Wi-Fi 省電 → 自動（錄放音期間關、平常開）");
}

void voiceActive(bool active) {
  if (s_ps_manual) return;
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

void pingNow() {
  if (!s_link) { Serial.println("[diag] 中控未連線，沒送 ping"); return; }
  s_ping_sent = millis();
  s_ws.sendTXT("{\"t\":\"ping\"}");
  Serial.println("[diag] 已送 ping（等 pong 算 RTT）");
}

const char* configLabel() {
  static char buf[64];
  const char* link = s_link ? "WS" : (WiFi.status() == WL_CONNECTED ? "up" : "down");
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
