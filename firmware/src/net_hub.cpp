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
net_hub::StateCb s_state_cb = nullptr;
net_hub::BinCb s_bin_cb = nullptr;
net_hub::AudioEndCb s_end_cb = nullptr;

// 極簡解析中控訊息裡的 "v":"xxx"（避免引入 JSON 函式庫）
void handleText(const uint8_t* payload, size_t len) {
  // B3b-2：{"t":"audio_end","n":<bytes>}
  if (strstr((const char*)payload, "\"t\":\"audio_end\"")) {
    const char* n = strstr((const char*)payload, "\"n\":");
    if (s_end_cb) s_end_cb(n ? (uint32_t)strtoul(n + 4, nullptr, 10) : 0);
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
    case WStype_CONNECTED:
      s_link = true;
      Serial.println("[hub] WS connected");
      break;
    case WStype_DISCONNECTED:
      s_link = false;
      // payload 是斷線原因；token 錯誤時為 "WebSocket handshake failed - HTTP 401"
      if (length > 0) {
        Serial.printf("[hub] WS disconnected: %.*s\n", (int)length, (const char*)payload);
      } else {
        Serial.println("[hub] WS disconnected");
      }
      break;
    case WStype_TEXT:
      Serial.printf("[hub] <- %.*s\n", (int)length, (const char*)payload);
      handleText(payload, length);
      break;
    case WStype_BIN:
      if (s_bin_cb) s_bin_cb(payload, length);   // 音訊幀很多，不逐幀印
      break;
    default:
      break;
  }
}

}  // namespace

namespace net_hub {

void begin() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[wifi] connecting to \"%s\" ...\n", WIFI_SSID);
  // WebSocket 等 Wi-Fi 拿到 IP 後才開（見 loop），避免開頭 DNS 失敗
}

void loop() {
  static bool wifi_logged = false;
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
  if (s_ws_started) s_ws.loop();

  const uint32_t now = millis();
  if (s_link && now - s_last_ping > 20000) {     // 每 20 秒 ping 一次保活
    s_last_ping = now;
    s_ws.sendTXT("{\"t\":\"ping\"}");
  }
}

bool wifiUp() { return WiFi.status() == WL_CONNECTED; }
bool linkUp() { return s_link; }
void sendStart() { if (s_link) s_ws.sendTXT("{\"t\":\"start\"}"); }
void sendStop()  { if (s_link) s_ws.sendTXT("{\"t\":\"stop\"}"); }
void onState(StateCb cb) { s_state_cb = cb; }

bool sendBinary(const uint8_t* data, size_t len) {
  if (!s_link) return false;
  return s_ws.sendBIN(data, len);
}
void onBinary(BinCb cb) { s_bin_cb = cb; }
void onAudioEnd(AudioEndCb cb) { s_end_cb = cb; }

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
  s_ws.sendTXT("{\"t\":\"ping\"}");
  Serial.println("[diag] 已送 ping");
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
