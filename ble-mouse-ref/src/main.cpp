// ble-mouse-ref — M4-0 對照用最小藍牙滑鼠（Bluedroid，Arduino 核心內建 BLE 函式庫）
//
// 用途：判斷 Mac「配對完約 0.1～2 秒就以 unused 斷線、HID 驅動沒接手」是
//       firmware/ 的 NimBLE 實作問題，還是 macOS 26.6.2 這一側的問題。
// 寫法照最常見的 ESP32 藍牙滑鼠做法（BLEHIDDevice＋BLESecurity 綁定），
// 不沿用 firmware/ 的任何設定（不加密 Report Map、不送 security request、
// 不要求改連線參數、位址用晶片預設），變因越少越好。
//
// 操作：A 短按＝開始／停止畫圈；B 短按或序列埠 s＝主動要求加密。序列埠 115200 印連線、加密、訂閱狀態。
// 藍牙回呼在 BT task 裡只改旗標，log 一律由 loop() 印（不在回呼裡碰 Serial）。

#include <Arduino.h>
#include <M5Unified.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEHIDDevice.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include <esp_gap_ble_api.h>

static const char*    kName       = "M5Ref";
static const uint16_t kAppearance = 0x03C2;   // GAP 外觀：滑鼠
static const uint8_t  kReportId   = 1;

// 3 鍵＋X／Y／滾輪（相對值 -127～127），Report ID 1
static const uint8_t kReportMap[] = {
  0x05, 0x01,       // Usage Page (Generic Desktop)
  0x09, 0x02,       // Usage (Mouse)
  0xA1, 0x01,       // Collection (Application)
  0x85, kReportId,  //   Report ID (1)
  0x09, 0x01,       //   Usage (Pointer)
  0xA1, 0x00,       //   Collection (Physical)
  0x05, 0x09,       //     Usage Page (Button)
  0x19, 0x01,       //     Usage Minimum (1)
  0x29, 0x03,       //     Usage Maximum (3)
  0x15, 0x00,       //     Logical Minimum (0)
  0x25, 0x01,       //     Logical Maximum (1)
  0x95, 0x03,       //     Report Count (3)
  0x75, 0x01,       //     Report Size (1)
  0x81, 0x02,       //     Input (Data, Var, Abs)
  0x95, 0x01,       //     Report Count (1)
  0x75, 0x05,       //     Report Size (5)
  0x81, 0x03,       //     Input (Const) 補齊 1 byte
  0x05, 0x01,       //     Usage Page (Generic Desktop)
  0x09, 0x30,       //     Usage (X)
  0x09, 0x31,       //     Usage (Y)
  0x09, 0x38,       //     Usage (Wheel)
  0x15, 0x81,       //     Logical Minimum (-127)
  0x25, 0x7F,       //     Logical Maximum (127)
  0x75, 0x08,       //     Report Size (8)
  0x95, 0x03,       //     Report Count (3)
  0x81, 0x06,       //     Input (Data, Var, Rel)
  0xC0,             //   End Collection
  0xC0              // End Collection
};

static BLEHIDDevice*      s_hid   = nullptr;
static BLECharacteristic* s_input = nullptr;

// ── 回呼 → 主迴圈的旗標 ──
static volatile bool    s_connected = false;
static volatile bool    s_evConnect = false;
static volatile bool    s_evDisconnect = false;
static volatile bool    s_evAuth = false;
static volatile bool    s_authOk = false;
static volatile uint8_t s_authReason = 0;
static volatile bool    s_evSubscribe = false;
static volatile bool    s_subscribed = false;
static uint8_t          s_peer[6] = {0};

class ServerCb : public BLEServerCallbacks {
  void onConnect(BLEServer*, esp_ble_gatts_cb_param_t* param) override {
    memcpy(s_peer, param->connect.remote_bda, 6);
    s_connected = true;
    s_subscribed = false;
    s_evConnect = true;
  }
  void onDisconnect(BLEServer*) override {
    s_connected = false;
    s_subscribed = false;
    s_evDisconnect = true;
  }
};

class SecCb : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override { return 0; }
  void onPassKeyNotify(uint32_t) override {}
  bool onSecurityRequest() override { return true; }
  bool onConfirmPIN(uint32_t) override { return true; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override {
    s_authOk = cmpl.success;
    s_authReason = cmpl.fail_reason;
    s_evAuth = true;
  }
};

// 主機寫 CCCD（0x2902）＝訂閱／取消訂閱滑鼠報告
class CccdCb : public BLEDescriptorCallbacks {
  void onWrite(BLEDescriptor* d) override {
    uint8_t* v = d->getValue();
    s_subscribed = (d->getLength() >= 1) && (v[0] & 0x01);
    s_evSubscribe = true;
  }
};

static bool     s_circle = false;
static float    s_angle = 0;
static uint32_t s_lastSend = 0;
static uint32_t s_connectAt = 0;
static bool     s_aliveLogged = false;

static void drawStatus() {
  auto& d = M5.Display;
  d.fillScreen(TFT_BLACK);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setTextSize(2);
  d.setCursor(4, 4);
  d.println("M5Ref");
  d.setTextSize(1);
  d.println("Bluedroid BLE mouse");
  d.println();
  d.printf("conn : %s\n", s_connected ? "YES" : "no");
  d.printf("auth : %s\n", s_evAuth || s_authOk ? (s_authOk ? "OK" : "FAIL") : "-");
  d.printf("sub  : %s\n", s_subscribed ? "YES" : "no");
  d.printf("circle(A): %s\n", s_circle ? "ON" : "off");
}

static void sendMove(int8_t dx, int8_t dy) {
  if (!s_connected || !s_input) return;
  uint8_t r[4] = {0, (uint8_t)dx, (uint8_t)dy, 0};
  s_input->setValue(r, sizeof(r));
  s_input->notify();
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  M5.Display.setRotation(0);

  BLEDevice::init(kName);
  BLEDevice::setSecurityCallbacks(new SecCb());

  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCb());

  s_hid = new BLEHIDDevice(server);
  s_input = s_hid->inputReport(kReportId);
  if (auto* cccd = (BLE2902*)s_input->getDescriptorByUUID(BLEUUID((uint16_t)0x2902))) {
    cccd->setCallbacks(new CccdCb());
  }
  s_hid->manufacturer()->setValue(kName);
  s_hid->pnp(0x02, 0x303A, 0x4D42, 0x0100);   // USB-IF 來源、Espressif VID、自訂 PID（跟主韌體 0x4D41 不同）
  s_hid->hidInfo(0x00, 0x02);                  // 國碼 0、NormallyConnectable

  BLESecurity* sec = new BLESecurity();
  sec->setAuthenticationMode(ESP_LE_AUTH_BOND);   // 綁定、Just Works

  s_hid->reportMap((uint8_t*)kReportMap, sizeof(kReportMap));
  s_hid->startServices();
  s_hid->setBatteryLevel(100);

  BLEAdvertising* adv = server->getAdvertising();
  adv->setAppearance(kAppearance);
  adv->addServiceUUID(s_hid->hidService()->getUUID());
  adv->setScanResponse(true);
  adv->start();

  delay(300);
  Serial.printf("\n[ref] M5Ref 開始廣播｜位址 %s｜A 短按＝畫圈開關\n",
                BLEDevice::getAddress().toString().c_str());
  drawStatus();
}

void loop() {
  M5.update();
  uint32_t now = millis();
  bool redraw = false;

  if (s_evConnect) {
    s_evConnect = false;
    s_connectAt = now;
    s_aliveLogged = false;
    Serial.printf("[ref] 已連線：%02X:%02X:%02X:%02X:%02X:%02X\n",
                  s_peer[0], s_peer[1], s_peer[2], s_peer[3], s_peer[4], s_peer[5]);
    redraw = true;
  }
  if (s_evAuth) {
    s_evAuth = false;
    if (s_authOk) Serial.println("[ref] 配對／加密完成");
    else          Serial.printf("[ref] 配對失敗（原因 0x%02X）\n", s_authReason);
    redraw = true;
  }
  if (s_evSubscribe) {
    s_evSubscribe = false;
    Serial.printf("[ref] 主機%s滑鼠報告%s\n", s_subscribed ? "訂閱" : "取消訂閱",
                  s_subscribed ? "＝HID 接上了" : "");
    redraw = true;
  }
  if (s_evDisconnect) {
    s_evDisconnect = false;
    Serial.printf("[ref] 斷線（連了 %lu ms）→ 重新廣播\n", (unsigned long)(now - s_connectAt));
    delay(100);
    BLEDevice::startAdvertising();
    redraw = true;
  }
  if (s_connected && !s_aliveLogged && now - s_connectAt > 10000) {
    s_aliveLogged = true;
    Serial.printf("[ref] 已連線超過 10 秒（訂閱：%s）\n", s_subscribed ? "是" : "否");
  }

  // B 短按（或序列埠打 s）＝裝置主動要求加密（SMP Security Request）
  int ch = Serial.available() ? Serial.read() : -1;
  if ((M5.BtnB.wasClicked() || ch == 's') && s_connected) {
    esp_err_t rc = esp_ble_set_encryption(s_peer, ESP_BLE_SEC_ENCRYPT_NO_MITM);
    Serial.printf("[ref] 要求加密 已送出（rc=%d）\n", (int)rc);
  }

  if (M5.BtnA.wasClicked()) {
    s_circle = !s_circle;
    Serial.printf("[ref] 畫圈 %s\n", s_circle ? "開" : "關");
    redraw = true;
  }

  if (s_circle && now - s_lastSend >= 20) {
    s_lastSend = now;
    s_angle += 0.08f;
    sendMove((int8_t)lroundf(6 * cosf(s_angle)), (int8_t)lroundf(6 * sinf(s_angle)));
  }

  if (redraw) drawStatus();
  delay(2);
}
