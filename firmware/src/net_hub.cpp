#include "net_hub.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <string.h>

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

// 極簡解析中控訊息裡的 "v":"xxx"（避免引入 JSON 函式庫）
void handleText(const uint8_t* payload, size_t len) {
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

}  // namespace net_hub
