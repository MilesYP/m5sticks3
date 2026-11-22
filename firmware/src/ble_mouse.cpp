#include "ble_mouse.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <atomic>
#include <math.h>
#include <WiFi.h>

#include "config.h"
#include "killswitch.h"

namespace {

// 標準 3 鍵滑鼠＋滾輪，Report ID 1：[按鍵, X, Y, 滾輪]（各 1 byte，X/Y/滾輪為 −127～127 相對量）
const uint8_t kReportMap[] = {
  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x02,        // Usage (Mouse)
  0xA1, 0x01,        // Collection (Application)
  0x85, 0x01,        //   Report ID (1)
  0x09, 0x01,        //   Usage (Pointer)
  0xA1, 0x00,        //   Collection (Physical)
  0x05, 0x09,        //     Usage Page (Buttons)
  0x19, 0x01,        //     Usage Minimum (1)
  0x29, 0x03,        //     Usage Maximum (3)
  0x15, 0x00,        //     Logical Minimum (0)
  0x25, 0x01,        //     Logical Maximum (1)
  0x95, 0x03,        //     Report Count (3)
  0x75, 0x01,        //     Report Size (1)
  0x81, 0x02,        //     Input (Data, Variable, Absolute)：3 個按鍵位元
  0x95, 0x01,        //     Report Count (1)
  0x75, 0x05,        //     Report Size (5)
  0x81, 0x03,        //     Input (Constant)：補滿 1 byte
  0x05, 0x01,        //     Usage Page (Generic Desktop)
  0x09, 0x30,        //     Usage (X)
  0x09, 0x31,        //     Usage (Y)
  0x09, 0x38,        //     Usage (Wheel)
  0x15, 0x81,        //     Logical Minimum (−127)
  0x25, 0x7F,        //     Logical Maximum (127)
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x03,        //     Report Count (3)
  0x81, 0x06,        //     Input (Data, Variable, Relative)
  0xC0,              //   End Collection
  0xC0,              // End Collection
};
constexpr uint8_t kReportId = 1;

NimBLEServer*         s_server = nullptr;
NimBLEHIDDevice*      s_hid    = nullptr;
NimBLECharacteristic* s_input  = nullptr;

bool                  s_built = false;   // 堆疊與 HID 服務已建好
std::atomic<bool>     s_on{false};
std::atomic<bool>     s_connected{false};
std::atomic<bool>     s_secured{false};
std::atomic<uint16_t> s_conn{0xFFFF};
// 回呼只設旗標，主迴圈代印（鐵則：急停／高優先 task 不印序列埠；NimBLE task 也一樣小心）
std::atomic<uint8_t>  s_ev_conn{0}, s_ev_disc{0}, s_ev_auth{0}, s_ev_auth_fail{0};
std::atomic<int>      s_disc_reason{0};
char                  s_peer[20] = "";
std::atomic<bool>     s_peer_ready{false};
std::atomic<bool>     s_conn_bonded{false};
std::atomic<bool>     s_need_sec{false};
std::atomic<uint32_t> s_conn_t{0};
std::atomic<uint16_t> s_itvl{0};
std::atomic<uint8_t>  s_ev_param{0};
bool                  s_pending = false;   // 進 AirMouse 了、等 Wi-Fi 關掉才真的開藍牙
uint32_t              s_pending_t = 0;
uint32_t              s_unsec_log_t = 0;

bool     s_circle = false;
uint32_t s_circle_t0 = 0, s_last_tx = 0;
float    s_acc_x = 0, s_acc_y = 0;
uint8_t  s_buttons = 0;
uint32_t s_tx_ok = 0, s_tx_fail = 0;

class ServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo& ci) override {
    s_conn.store(ci.getConnHandle());
    s_connected.store(true);
    s_secured.store(ci.isEncrypted());
    s_conn_bonded.store(ci.isBonded());
    s_peer_ready.store(false);
    const NimBLEAddress addr = ci.getAddress();
    const uint8_t* a = addr.getVal();   // 不用 std::string（回呼在 NimBLE task，少配記憶體）
    snprintf(s_peer, sizeof(s_peer), "%02X:%02X:%02X:%02X:%02X:%02X", a[5], a[4], a[3], a[2], a[1], a[0]);
    s_peer_ready.store(true);
    s_conn_t.store(millis() | 1);
    s_ev_conn.fetch_add(1);
    // 31-m4b：由我們主動要求加密（有些主機要等對方先開口）；已加密（重連）就不用
    if (!ci.isEncrypted()) s_need_sec.store(true);
  }
  void onConnParamsUpdate(NimBLEConnInfo& ci) override {
    s_itvl.store(ci.getConnInterval());
    s_ev_param.fetch_add(1);
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override {
    s_connected.store(false);
    s_secured.store(false);
    s_conn.store(0xFFFF);
    s_disc_reason.store(reason);
    s_ev_disc.fetch_add(1);
    // advertiseOnDisconnect(true) 讓函式庫自己重新廣播
  }
  void onAuthenticationComplete(NimBLEConnInfo& ci) override {
    s_conn_bonded.store(ci.isBonded());
    if (ci.isEncrypted()) { s_secured.store(true); s_ev_auth.fetch_add(1); }
    else s_ev_auth_fail.fetch_add(1);
  }
};
ServerCb s_cb;

int8_t clamp127(int v) { return (int8_t)(v > 127 ? 127 : v < -127 ? -127 : v); }

bool sendRaw(int dx, int dy, int wheel, uint8_t buttons) {
  if (!s_on.load() || !s_connected.load() || !s_input) return false;
  if (!killswitch::outputAllowed()) return false;   // 鐵則三：閘門關了一個封包都不送
  uint8_t r[4] = {(uint8_t)(buttons & 0x07), (uint8_t)clamp127(dx), (uint8_t)clamp127(dy), (uint8_t)clamp127(wheel)};
  const bool ok = s_input->notify((const uint8_t*)r, sizeof(r));
  if (ok) ++s_tx_ok; else ++s_tx_fail;
  return ok;
}

}  // namespace

namespace ble_mouse {

bool startNow();

bool begin() {
  if (s_on.load() || s_pending) return true;
  s_pending = true;                      // 31-m4b：先等 Wi-Fi 關掉（網路 task 非同步在關），loop() 再開藍牙
  s_pending_t = millis();
  Serial.println("[ble] 等 Wi-Fi 關閉後開藍牙…");
  return true;
}

bool startNow() {
  if (s_on.load()) return true;
  const uint32_t t0 = millis();
  if (!s_built) {   // 第一次（或上次 exit 有 deinit）：初始化藍牙堆疊並建 HID 服務
    if (!NimBLEDevice::init(cfg::BLE_DEVICE_NAME)) {
      Serial.println("[ble] NimBLE 初始化失敗");
      return false;
    }
    // 配對：要綁定（bonding，下次自動重連）、不需輸入密碼（裝置沒有螢幕鍵盤可比對）、用 LE Secure Connections
    NimBLEDevice::setSecurityAuth(true, false, cfg::BLE_SECURE_CONN);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setPower(cfg::BLE_TX_DBM);

    s_server = NimBLEDevice::createServer();
    s_server->setCallbacks(&s_cb, false);
    s_server->advertiseOnDisconnect(true);

    s_hid = new NimBLEHIDDevice(s_server);
    s_input = s_hid->getInputReport(kReportId);
    s_hid->setManufacturer("M5S3Pack");
    s_hid->setPnp(0x02, 0x303A, 0x4D41, 0x0100);   // 0x02＝USB VID 來源；0x303A＝Espressif
    s_hid->setHidInfo(0x00, 0x02);                 // 國碼 0、可被喚醒
    s_hid->setReportMap((uint8_t*)kReportMap, sizeof(kReportMap));
    s_hid->setBatteryLevel(100);
    s_server->start();

    // 31-m4e：自己組廣播內容。函式庫預設的 flags 只有「一般可發現」，少了「不支援傳統藍牙（BR/EDR）」——
    // Mac 這種同時有傳統藍牙的主機可能因此把它當雙模裝置、在「附近裝置」不給連線按鈕。
    NimBLEAdvertisementData ad;
    ad.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);   // 3 bytes
    ad.setAppearance(HID_MOUSE);                   // 4：iOS／iPadOS／macOS 靠這個認成滑鼠
    ad.setCompleteServices(s_hid->getHidService()->getUUID());   // 4：HID 服務 0x1812
    ad.setPreferredParams(6, 12);                  // 6：連線間隔偏好 7.5～15ms（單位 1.25ms），游標才順
    ad.setName(cfg::BLE_DEVICE_NAME);              // 10：共 27 bytes（上限 31）
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    if (!adv->setAdvertisementData(ad)) Serial.println("[ble] 自訂廣播內容設定失敗（改用函式庫預設）");
    s_built = true;
  }
  NimBLEDevice::getAdvertising()->start();

  s_on.store(true);
  s_tx_ok = s_tx_fail = 0;
  Serial.printf("[ble] 開始廣播「%s」（%lu ms）｜已配對 %d 台｜heap %u\n", cfg::BLE_DEVICE_NAME,
                (unsigned long)(millis() - t0), NimBLEDevice::getNumBonds(), (unsigned)ESP.getFreeHeap());
  return true;
}

void end() {
  s_pending = false;
  if (!s_on.load()) return;
  const uint32_t t0 = millis();
  s_circle = false;
  s_on.store(false);                     // 先關：之後 report() 一律不送
  if (s_server && s_connected.load()) {
    if (killswitch::outputAllowed()) sendRaw(0, 0, 0, 0);   // 放開所有按鍵（急停時不送）
    s_server->disconnect(s_conn.load());
  }
  NimBLEDevice::getAdvertising()->stop();
  if (cfg::BLE_DEINIT_ON_EXIT) {         // 關藍牙控制器並釋放記憶體（鐵則二）；下次 begin 重建
    delete s_hid;                        // 只是包裝物件；服務本身由 deinit(true) 一起清
    s_hid = nullptr; s_input = nullptr; s_server = nullptr;
    NimBLEDevice::deinit(true);
    s_built = false;
  }                                      // 否則保留堆疊，只停廣播（BLE_DEINIT_ON_EXIT＝false 對照用）
  s_connected.store(false); s_secured.store(false); s_conn.store(0xFFFF);
  Serial.printf("[ble] 藍牙關閉（%lu ms）｜heap %u\n", (unsigned long)(millis() - t0), (unsigned)ESP.getFreeHeap());
}

void loop() {
  if (s_pending) {
    const bool wifi_off = WiFi.getMode() == WIFI_OFF;
    if (wifi_off || millis() - s_pending_t >= cfg::BLE_START_WAIT_MS) {
      s_pending = false;
      Serial.printf("[ble] Wi-Fi %s（等了 %lu ms）→ 開藍牙\n", wifi_off ? "已關" : "還沒關（逾時，照開）",
                    (unsigned long)(millis() - s_pending_t));
      if (!startNow()) Serial.println("[ble] 開藍牙失敗");
    }
    return;
  }
  if (s_ev_conn.exchange(0))
    Serial.printf("[ble] 已連線：%s%s%s\n", s_peer_ready.load() ? s_peer : "?",
                  s_secured.load() ? "（已加密）" : "（等配對加密…）", s_conn_bonded.load() ? "（有配對紀錄）" : "");
  if (s_need_sec.exchange(false) && s_connected.load()) {
    int rc = 0;
    const bool ok = NimBLEDevice::startSecurity(s_conn.load(), &rc);
    Serial.printf("[ble] 要求加密 %s（rc=%d）\n", ok ? "已送出" : "失敗", rc);
  }
  if (s_ev_param.exchange(0)) Serial.printf("[ble] 連線間隔 %.2f ms\n", s_itvl.load() * 1.25f);
  // 連上但一直沒加密：每 3 秒提醒一次（卡在 CONN.. 時看這行）
  if (s_connected.load() && !s_secured.load() && millis() - s_unsec_log_t >= 3000 && s_conn_t.load() &&
      millis() - s_conn_t.load() >= 3000) {
    s_unsec_log_t = millis();
    Serial.printf("[ble] 連上 %lu 秒仍未加密（電腦端可能要先「忘記此裝置」，這邊打 @f 清配對再重配）\n",
                  (unsigned long)((millis() - s_conn_t.load()) / 1000));
  }
  if (s_ev_auth.exchange(0)) Serial.println("[ble] 配對完成（已加密），可以送滑鼠資料");
  if (s_ev_auth_fail.exchange(0)) Serial.println("[ble] 配對失敗（沒加密）— 對方可能要先「忘記此裝置」再重配");
  if (s_ev_disc.exchange(0))
    Serial.printf("[ble] 斷線（原因 0x%X）→ 重新廣播\n", s_disc_reason.load());
  if (!s_on.load()) return;

  // 鐵則三：急停閘門關了 → 停測試、斷線（mode_manager 隨後會 exit → end()）
  if (!killswitch::outputAllowed()) {
    if (s_circle) s_circle = false;
    if (s_connected.load() && s_server) s_server->disconnect(s_conn.load());
    return;
  }

  // @1 畫圓測試：每 BLE_REPORT_MS 送一筆，3 秒一圈，半徑約 BLE_CIRCLE_R 個單位
  const uint32_t now = millis();
  if (s_circle && s_secured.load() && now - s_last_tx >= cfg::BLE_REPORT_MS) {
    const float dt = (now - s_last_tx) / 1000.0f;
    s_last_tx = now;
    const float w = 2.0f * (float)M_PI / 3.0f;   // 每秒 1/3 圈
    const float th = (now - s_circle_t0) / 1000.0f * w;
    s_acc_x += -cfg::BLE_CIRCLE_R * w * sinf(th) * dt;
    s_acc_y +=  cfg::BLE_CIRCLE_R * w * cosf(th) * dt;
    const int dx = (int)s_acc_x, dy = (int)s_acc_y;
    s_acc_x -= dx; s_acc_y -= dy;
    if (dx || dy) sendRaw(dx, dy, 0, s_buttons);
  }
}

State state() {
  if (s_pending) return State::Advertising;
  if (!s_on.load()) return State::Off;
  return s_connected.load() ? State::Connected : State::Advertising;
}
const char* stateName() {
  switch (state()) {
    case State::Off: return "OFF";
    case State::Advertising: return "ADV";
    default: return "CONN";
  }
}
bool secured() { return s_secured.load(); }
int bondCount() { return NimBLEDevice::isInitialized() ? NimBLEDevice::getNumBonds() : -1; }

bool report(int dx, int dy, int wheel, uint8_t buttons) {
  s_buttons = buttons;
  if (!s_secured.load()) return false;
  return sendRaw(dx, dy, wheel, buttons);
}

void setCircleTest(bool on) {
  s_circle = on;
  s_circle_t0 = s_last_tx = millis();
  s_acc_x = s_acc_y = 0;
  Serial.printf("[ble] 畫圓測試 %s%s\n", on ? "開" : "關",
                on && !s_secured.load() ? "（還沒連線／配對，連上後才會動）" : "");
}
bool circleTest() { return s_circle; }

void forgetBonds() {
  if (!NimBLEDevice::isInitialized()) {
    Serial.println("[ble] 藍牙沒開（切到 AirMouse 模式再打 @f）");
    return;
  }
  if (s_connected.load() && s_server) s_server->disconnect(s_conn.load());
  const bool ok = NimBLEDevice::deleteAllBonds();
  Serial.printf("[ble] 清除所有配對紀錄%s（電腦／平板那邊也要「忘記此裝置」）\n", ok ? "" : "失敗");
}

void printStatus() {
  Serial.printf("[ble] 狀態 %s%s｜對方 %s｜已配對 %d 台｜送出 %lu 筆、失敗 %lu｜畫圓 %s｜閘門 %s｜heap %u\n",
                stateName(), s_secured.load() ? "（已加密）" : "", s_connected.load() ? s_peer : "-", bondCount(),
                (unsigned long)s_tx_ok, (unsigned long)s_tx_fail, s_circle ? "開" : "關",
                killswitch::outputAllowed() ? "開" : "關", (unsigned)ESP.getFreeHeap());
}

}  // namespace ble_mouse
