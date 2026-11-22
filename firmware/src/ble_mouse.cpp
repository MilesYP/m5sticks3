#include "ble_mouse.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <atomic>
#include <math.h>
#include <WiFi.h>

#include "config.h"

extern "C" int ble_store_clear(void);   // NimBLE host：清空配對資料庫（NimBLEDevice::deleteAllBonds 逐筆刪會失敗）
#include "killswitch.h"
#include "net_hub.h"

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
constexpr size_t  kReportIdPos = 6;   // kReportMap 裡「Report ID (1)」那 2 bytes 的位置（不用 Report ID 時拿掉）

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
std::atomic<uint16_t> s_lat{0}, s_tmo{0};   // 31-m4k：延遲（可跳過幾次）、監督逾時（單位 10ms）
std::atomic<uint8_t>  s_ev_param{0};
// 31-m4k：連線間隔預設組（單位 1.25ms）；Apple 規範見 config.h BLE_ITVL_PRESET
const uint16_t        kItvl[3][2] = {{6, 12}, {9, 12}, {12, 24}};
uint8_t               s_itvl_preset = cfg::BLE_ITVL_PRESET < 3 ? cfg::BLE_ITVL_PRESET : 2;
bool                  s_opt_upd = true;                      // @u：配對完成後主動要求連線參數
std::atomic<uint32_t> s_auth_t{0};                           // 配對完成時間（|1，0＝沒有待送的參數要求）
// 31-m4h：配對選項可用序列埠即時切（Mac 連上秒斷的對照測試，不用重燒）
// 31-m4l：預設改成對照 HijelHID_BLEMouse（Apache-2.0，README 標 macOS Ventura 實測可用）的 HID 設定，
// 每一項都可用序列埠切回舊設定，逐項找出 Mac 不接手的原因。改了會自動重建藍牙（要重新配對）。
bool                  s_opt_rid    = false;                  // @r：報告帶 Report ID 1（舊）；關＝不帶 Report ID（對照組）
bool                  s_opt_vid    = true;                   // @v：PnP 用 Apple VID 0x05AC／PID 0（對照組）；關＝Espressif 0x303A（舊）
bool                  s_opt_wake   = true;                   // @h：HID Information flags 0x01 遠端喚醒（對照組）；關＝0x02 一般可連線（舊）
bool                  s_opt_enc    = true;                   // @e：HID 服務特徵值全部要加密（31-m4m，規範要求）；關＝NimBLE 預設（Report Map 等不加密）
bool                  s_opt_reqsec = false;                  // @s：連上後由裝置主動要求加密（舊＝開；對照組不主動要求）
bool                  s_opt_sc     = cfg::BLE_SECURE_CONN;   // @c：LE Secure Connections（關＝舊式配對）
bool                  s_opt_irk    = true;                   // @k：配對交換身分金鑰（IRK）
std::atomic<uint8_t>  s_auth_flags{0};                       // bit0 加密 bit1 綁定 bit2 驗證
std::atomic<uint8_t>  s_auth_key{0};
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
    s_itvl.store(ci.getConnInterval()); s_lat.store(ci.getConnLatency()); s_tmo.store(ci.getConnTimeout());
    s_auth_t.store(0);
    s_ev_conn.fetch_add(1);
    // 31-m4b：由我們主動要求加密（有些主機要等對方先開口）；已加密（重連）就不用
    if (!ci.isEncrypted() && s_opt_reqsec) s_need_sec.store(true);
  }
  void onConnParamsUpdate(NimBLEConnInfo& ci) override {
    s_itvl.store(ci.getConnInterval()); s_lat.store(ci.getConnLatency()); s_tmo.store(ci.getConnTimeout());
    s_ev_param.fetch_add(1);
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override {
    s_connected.store(false);
    s_secured.store(false);
    s_conn.store(0xFFFF);
    s_conn_t.store(0);   // 31-m4k：歸零，否則重連前會印錯的「連上 N 秒仍未加密」
    s_auth_t.store(0);
    s_disc_reason.store(reason);
    s_ev_disc.fetch_add(1);
    // advertiseOnDisconnect(true) 讓函式庫自己重新廣播
  }
  void onAuthenticationComplete(NimBLEConnInfo& ci) override {
    s_conn_bonded.store(ci.isBonded());
    s_auth_flags.store((ci.isEncrypted() ? 1 : 0) | (ci.isBonded() ? 2 : 0) | (ci.isAuthenticated() ? 4 : 0));
    s_auth_key.store(ci.getSecKeySize());
    if (ci.isEncrypted()) { s_secured.store(true); s_auth_t.store(millis() | 1); s_ev_auth.fetch_add(1); }
    else s_ev_auth_fail.fetch_add(1);
  }
};
ServerCb s_cb;

// 31-m4g：主機有沒有訂閱滑鼠報告（訂閱了＝HID 真的接上，Mac 連上秒斷時看有沒有走到這步）
std::atomic<uint8_t>  s_ev_sub{0};
std::atomic<uint16_t> s_sub_val{0};
// 31-m4m：記錄主機讀／寫了哪個 HID 相關特徵值（Mac 的滑鼠驅動有沒有開始讀 Report Map，看這裡）
enum ChrTag : uint8_t { T_MAP, T_INFO, T_PROTO, T_CTRL, T_INPUT, T_PNP, T_MANU, T_BATT, T_COUNT };
const char* const kTagName[T_COUNT] = {"Report Map（滑鼠格式）", "HID Information", "Protocol Mode",
                                       "HID Control Point", "Input Report（滑鼠資料）", "PnP ID", "製造商名稱", "電池電量"};
std::atomic<uint8_t> s_ev_rd[T_COUNT], s_ev_wr[T_COUNT];
class LogCb : public NimBLECharacteristicCallbacks {
 public:
  explicit LogCb(uint8_t t) : tag(t) {}
  uint8_t tag;
  void onRead(NimBLECharacteristic*, NimBLEConnInfo&) override { s_ev_rd[tag].fetch_add(1); }
  void onWrite(NimBLECharacteristic*, NimBLEConnInfo&) override { s_ev_wr[tag].fetch_add(1); }
  void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t subValue) override {
    if (tag != T_INPUT) return;
    s_sub_val.store(subValue);
    s_ev_sub.fetch_add(1);
  }
};
LogCb s_cb_tag[T_COUNT] = {LogCb(T_MAP), LogCb(T_INFO), LogCb(T_PROTO), LogCb(T_CTRL),
                           LogCb(T_INPUT), LogCb(T_PNP), LogCb(T_MANU), LogCb(T_BATT)};
// NimBLE 2.x 的 NimBLEHIDDevice 把 Report Map／HID Information／Protocol Mode 建成「不加密也能讀」；
// HID over GATT 規範要求 HID 服務要加密才能存取（主機讀到「需要驗證」才會自己發起配對）。
// setProperties 是 protected → 用同結構的子類別取用（沒有新增成員，只是開放這個函式）。
struct PropAccess : public NimBLECharacteristic {
  static void addEnc(NimBLECharacteristic* c) {
    if (!c) return;
    uint16_t p = c->getProperties();
    if (p & NIMBLE_PROPERTY::READ) p |= NIMBLE_PROPERTY::READ_ENC;
    if (p & (NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR)) p |= NIMBLE_PROPERTY::WRITE_ENC;
    static_cast<PropAccess*>(c)->setProperties(p);
  }
};

const char* discReason(int r) {
  switch (r) {
    case 0x213: return "對方主動斷線";
    case 0x208: return "連線逾時（訊號或對方沒回應）";
    case 0x216: return "這邊主動斷線";
    case 0x23D: return "加密驗證失敗（MIC）";
    case 0x206: return "對方說沒有配對金鑰（要重新配對）";
    case 0x205: return "驗證失敗";
    case 0x23E: return "連線建立失敗";
    default:    return "";
  }
}

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

// 31-m4e：自己組廣播內容。函式庫預設的 flags 只有「一般可發現」，少了「不支援傳統藍牙（BR/EDR）」——
// Mac 這種同時有傳統藍牙的主機可能因此把它當雙模裝置、在「附近裝置」不給連線按鈕。
// 31-m4k：偏好連線間隔改用預設組（@i），預設 15～30ms（Apple 規範）
void applyAdvData() {
  NimBLEAdvertisementData ad;
  ad.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);   // 3 bytes
  ad.setAppearance(HID_MOUSE);                   // 4：iOS／iPadOS／macOS 靠這個認成滑鼠
  ad.setCompleteServices(s_hid->getHidService()->getUUID());   // 4：HID 服務 0x1812
  ad.setPreferredParams(kItvl[s_itvl_preset][0], kItvl[s_itvl_preset][1]);   // 6：偏好連線間隔（單位 1.25ms）
  ad.setName(cfg::BLE_DEVICE_NAME);              // 10：共 27 bytes（上限 31）
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  const bool was = adv->isAdvertising();
  if (was) adv->stop();
  if (!adv->setAdvertisementData(ad)) Serial.println("[ble] 自訂廣播內容設定失敗（改用函式庫預設）");
  if (was) adv->start();
}

void setIntervalPreset(uint8_t p) {
  if (p > 2) return;
  s_itvl_preset = p;
  if (s_built && s_hid) applyAdvData();
  Serial.printf("[ble] 偏好連線間隔 %.2f～%.2f ms（@i%u；下一次連線生效，連著的話配對後主動要求也用這組）\n",
                kItvl[p][0] * 1.25f, kItvl[p][1] * 1.25f, (unsigned)p);
}

// 31-m4g：配對時交換「加密金鑰＋身分金鑰（IRK）」（Mac 用身分金鑰認出重連的裝置）；31-m4h 可用 @k 關掉對照
void applySecurity() {
  NimBLEDevice::setSecurityAuth(true, false, s_opt_sc);   // 綁定、不需密碼比對、SC 依選項
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  const uint8_t keys = BLE_SM_PAIR_KEY_DIST_ENC | (s_opt_irk ? BLE_SM_PAIR_KEY_DIST_ID : 0);
  NimBLEDevice::setSecurityInitKey(keys);
  NimBLEDevice::setSecurityRespKey(keys);
}

void setOption(char opt, bool on) {
  if (opt == 's') s_opt_reqsec = on;
  else if (opt == 'c') s_opt_sc = on;
  else if (opt == 'k') s_opt_irk = on;
  else if (opt == 'u') { s_opt_upd = on; Serial.printf("[ble] 配對後主動要求連線參數 %s\n", on ? "開" : "關"); return; }
  else if (opt == 'r' || opt == 'v' || opt == 'h' || opt == 'p' || opt == 'e') {   // 31-m4l：GATT 內容變了 → 重建藍牙
    if (opt == 'e') s_opt_enc = on;
    else if (opt == 'r') s_opt_rid = on;
    else if (opt == 'v') s_opt_vid = on;
    else if (opt == 'h') s_opt_wake = on;
    else { s_opt_rid = !on; s_opt_vid = on; s_opt_wake = on; s_opt_reqsec = !on; s_opt_enc = on; }   // @p1 對照組全套、@p0 舊設定全套
    Serial.printf("[ble] HID 設定：HID 特徵值加密 %s｜Report ID %s｜VID %s｜HID 旗標 %s｜主動要求加密 %s（電腦端先「忘記此裝置」，這邊會清配對並重建藍牙）\n",
                  s_opt_enc ? "要" : "不要", s_opt_rid ? "帶 1" : "不帶", s_opt_vid ? "Apple 0x05AC" : "Espressif 0x303A",
                  s_opt_wake ? "0x01" : "0x02", s_opt_reqsec ? "開" : "關");
    if (s_on.load() || s_pending) {
      if (NimBLEDevice::isInitialized()) ble_store_clear();   // 報告格式變了，舊配對的 GATT 快取不能用
      end();
      begin();
    }
    return;
  }
  else return;
  if (NimBLEDevice::isInitialized()) applySecurity();
  Serial.printf("[ble] 選項：主動要求加密 %s｜Secure Connections %s｜交換身分金鑰 %s（下一次配對生效；先在電腦「忘記此裝置」＋ @f）\n",
                s_opt_reqsec ? "開" : "關", s_opt_sc ? "開" : "關", s_opt_irk ? "開" : "關");
}

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
    applySecurity();
    NimBLEDevice::setPower(cfg::BLE_TX_DBM);

    s_server = NimBLEDevice::createServer();
    s_server->setCallbacks(&s_cb, false);
    s_hid = new NimBLEHIDDevice(s_server);
    s_input = s_hid->getInputReport(s_opt_rid ? kReportId : 0);
    s_input->setCallbacks(&s_cb_tag[T_INPUT]);
    s_hid->getReportMap()->setCallbacks(&s_cb_tag[T_MAP]);
    s_hid->getHidInfo()->setCallbacks(&s_cb_tag[T_INFO]);
    s_hid->getProtocolMode()->setCallbacks(&s_cb_tag[T_PROTO]);
    s_hid->getHidControl()->setCallbacks(&s_cb_tag[T_CTRL]);
    s_hid->getPnp()->setCallbacks(&s_cb_tag[T_PNP]);
    s_hid->getBatteryLevel()->setCallbacks(&s_cb_tag[T_BATT]);
    if (s_opt_enc) {   // 31-m4m：HID 服務的特徵值都要加密（要在 server->start() 前改）
      PropAccess::addEnc(s_hid->getReportMap());
      PropAccess::addEnc(s_hid->getHidInfo());
      PropAccess::addEnc(s_hid->getProtocolMode());
      PropAccess::addEnc(s_hid->getHidControl());
    }
    s_hid->setManufacturer("M5S3Pack");
    if (s_opt_vid) s_hid->setPnp(0x02, 0x05AC, 0x0000, 0x0001);   // 31-m4l 對照組：Apple VID（HijelHID 用法）
    else s_hid->setPnp(0x02, 0x303A, 0x4D41, 0x0100);              // 0x02＝USB VID 來源；0x303A＝Espressif
    s_hid->setHidInfo(0x00, s_opt_wake ? 0x01 : 0x02);             // 旗標 bit0 遠端喚醒、bit1 一般可連線
    static uint8_t rmap[sizeof(kReportMap)];
    size_t n = 0;
    for (size_t i = 0; i < sizeof(kReportMap); ++i) {
      if (!s_opt_rid && (i == kReportIdPos || i == kReportIdPos + 1)) continue;   // 不帶 Report ID
      rmap[n++] = kReportMap[i];
    }
    s_hid->setReportMap(rmap, (uint16_t)n);
    s_hid->setBatteryLevel(100);
    s_server->start();

    applyAdvData();
    s_built = true;
  }
  if (s_server) s_server->advertiseOnDisconnect(true);
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
  if (s_server) s_server->advertiseOnDisconnect(false);   // 31-m4g：斷線後不要自己又開廣播
  NimBLEDevice::getAdvertising()->stop();
  if (s_server && s_connected.load()) {
    if (killswitch::outputAllowed()) sendRaw(0, 0, 0, 0);   // 放開所有按鍵（急停時不送）
    s_server->disconnect(s_conn.load());
    // 31-m4g：連著的時候直接 deinit 會當機（host task 還在處理斷線事件）→ 等斷線回呼回來（最多 600ms）
    const uint32_t tw = millis();
    while (s_connected.load() && millis() - tw < 600) delay(5);
    Serial.printf("[ble] 等斷線 %lu ms%s\n", (unsigned long)(millis() - tw), s_connected.load() ? "（逾時）" : "");
    delay(50);                                 // 讓 host task 把斷線後的事情做完
  }
  if (cfg::BLE_DEINIT_ON_EXIT) {         // 關藍牙控制器並釋放記憶體（鐵則二）；下次 begin 重建
    delete s_hid;                        // 只是包裝物件；服務本身由 deinit(true) 一起清
    s_hid = nullptr; s_input = nullptr; s_server = nullptr;
    NimBLEDevice::deinit(true);
    s_built = false;
  }                                      // 否則保留堆疊，只停廣播（BLE_DEINIT_ON_EXIT＝false 對照用）
  s_connected.store(false); s_secured.store(false); s_conn.store(0xFFFF);
  s_conn_t.store(0); s_auth_t.store(0);
  Serial.printf("[ble] 藍牙關閉（%lu ms）｜heap %u\n", (unsigned long)(millis() - t0), (unsigned)ESP.getFreeHeap());
}

void loop() {
  if (s_pending) {
    // 31-m4j：要等「最後一個」關 Wi-Fi 指令做完（快速來回切模式時，舊的開 Wi-Fi 指令可能還排在後面）
    const bool wifi_off = net_hub::radioSettled() && WiFi.getMode() == WIFI_OFF;
    if (wifi_off || millis() - s_pending_t >= cfg::BLE_START_WAIT_MS) {
      s_pending = false;
      Serial.printf("[ble] Wi-Fi %s（等了 %lu ms）→ 開藍牙\n", wifi_off ? "已關" : "還沒關（逾時，照開）",
                    (unsigned long)(millis() - s_pending_t));
      if (!startNow()) Serial.println("[ble] 開藍牙失敗");
    }
    return;
  }
  if (s_ev_conn.exchange(0)) {
    Serial.printf("[ble] 已連線：%s%s%s\n", s_peer_ready.load() ? s_peer : "?",
                  s_secured.load() ? "（已加密）" : "（等配對加密…）", s_conn_bonded.load() ? "（有配對紀錄）" : "");
    Serial.printf("[ble] 主機選的連線參數：間隔 %.2f ms｜延遲 %u｜逾時 %u ms（我們偏好 %.2f～%.2f ms）\n",
                  s_itvl.load() * 1.25f, (unsigned)s_lat.load(), (unsigned)s_tmo.load() * 10,
                  kItvl[s_itvl_preset][0] * 1.25f, kItvl[s_itvl_preset][1] * 1.25f);
  }
  if (s_need_sec.exchange(false) && s_connected.load()) {
    int rc = 0;
    const bool ok = NimBLEDevice::startSecurity(s_conn.load(), &rc);
    Serial.printf("[ble] 要求加密 %s（rc=%d）\n", ok ? "已送出" : "失敗", rc);
  }
  if (s_ev_param.exchange(0))
    Serial.printf("[ble] 連線參數更新：間隔 %.2f ms｜延遲 %u｜逾時 %u ms\n", s_itvl.load() * 1.25f,
                  (unsigned)s_lat.load(), (unsigned)s_tmo.load() * 10);
  // 31-m4k：配對完成 BLE_PARAM_REQ_MS 後，若主機選的間隔不在我們的範圍內，主動要求一次（只要求一次）
  const uint32_t at = s_auth_t.load();
  if (at && millis() - at >= cfg::BLE_PARAM_REQ_MS) {
    s_auth_t.store(0);
    const uint16_t mn = kItvl[s_itvl_preset][0], mx = kItvl[s_itvl_preset][1], cur = s_itvl.load();
    if (s_opt_upd && s_connected.load() && s_server && (cur < mn || cur > mx)) {
      s_server->updateConnParams(s_conn.load(), mn, mx, 0, cfg::BLE_SUP_TIMEOUT);
      Serial.printf("[ble] 目前間隔 %.2f ms 不在偏好範圍 → 要求改 %.2f～%.2f ms、逾時 %u ms\n", cur * 1.25f,
                    mn * 1.25f, mx * 1.25f, (unsigned)cfg::BLE_SUP_TIMEOUT * 10);
    }
  }
  // 連上但一直沒加密：每 3 秒提醒一次（卡在 CONN.. 時看這行）
  if (s_connected.load() && !s_secured.load() && millis() - s_unsec_log_t >= 3000 && s_conn_t.load() &&
      millis() - s_conn_t.load() >= 3000) {
    s_unsec_log_t = millis();
    Serial.printf("[ble] 連上 %lu 秒仍未加密（電腦端可能要先「忘記此裝置」，這邊打 @f 清配對再重配）\n",
                  (unsigned long)((millis() - s_conn_t.load()) / 1000));
  }
  if (s_ev_auth.exchange(0)) {
    const uint8_t f = s_auth_flags.load();
    Serial.printf("[ble] 配對完成（已加密），可以送滑鼠資料｜綁定 %s｜驗證 %s｜金鑰 %u bytes\n",
                  (f & 2) ? "是" : "否", (f & 4) ? "是" : "否", (unsigned)s_auth_key.load());
  }
  if (s_ev_auth_fail.exchange(0)) Serial.println("[ble] 配對失敗（沒加密）— 對方可能要先「忘記此裝置」再重配");
  if (s_ev_disc.exchange(0))
    Serial.printf("[ble] 斷線（原因 0x%X %s）→ 重新廣播\n", s_disc_reason.load(), discReason(s_disc_reason.load()));
  for (uint8_t t = 0; t < T_COUNT; ++t) {   // 31-m4m：主機讀寫了哪些特徵值
    const uint8_t r = s_ev_rd[t].exchange(0), w = s_ev_wr[t].exchange(0);
    if (r) Serial.printf("[ble] 主機讀 %s（%u 次）%s\n", kTagName[t], (unsigned)r, t == T_MAP ? "＝滑鼠驅動開始接手" : "");
    if (w) Serial.printf("[ble] 主機寫 %s（%u 次）\n", kTagName[t], (unsigned)w);
  }
  if (s_ev_sub.exchange(0))
    Serial.printf("[ble] 主機%s訂閱滑鼠報告（0x%X）%s\n", s_sub_val.load() ? "" : "取消", s_sub_val.load(),
                  s_sub_val.load() ? "＝HID 接上了" : "");
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
bool starting() { return s_pending; }
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
  const int before = NimBLEDevice::getNumBonds();
  const bool ok = ble_store_clear() == 0;   // 31-m4i：deleteAllBonds() 實機回失敗，改直接清整個資料庫
  Serial.printf("[ble] 清除所有配對紀錄%s（%d → %d 台；電腦／平板那邊也要「忘記此裝置」）\n", ok ? "" : "失敗", before,
                NimBLEDevice::getNumBonds());
}

void printStatus() {
  Serial.printf("[ble] HID 設定 @e 特徵值加密 %s｜@r Report ID %s｜@v VID %s｜@h HID 旗標 %s（@p1 對照組全套／@p0 舊設定全套）\n",
                s_opt_enc ? "要" : "不要", s_opt_rid ? "帶 1" : "不帶", s_opt_vid ? "Apple" : "Espressif", s_opt_wake ? "0x01" : "0x02");
  Serial.printf("[ble] 選項 @s 主動要求加密 %s｜@c SC %s｜@k 身分金鑰 %s｜@i%u 偏好間隔 %.2f～%.2f ms｜@u 配對後要求參數 %s\n",
                s_opt_reqsec ? "開" : "關", s_opt_sc ? "開" : "關", s_opt_irk ? "開" : "關", (unsigned)s_itvl_preset,
                kItvl[s_itvl_preset][0] * 1.25f, kItvl[s_itvl_preset][1] * 1.25f, s_opt_upd ? "開" : "關");
  if (s_connected.load())
    Serial.printf("[ble] 目前連線：間隔 %.2f ms｜延遲 %u｜逾時 %u ms\n", s_itvl.load() * 1.25f, (unsigned)s_lat.load(),
                  (unsigned)s_tmo.load() * 10);
  Serial.printf("[ble] 狀態 %s%s｜對方 %s｜已配對 %d 台｜送出 %lu 筆、失敗 %lu｜畫圓 %s｜閘門 %s｜heap %u\n",
                stateName(), s_secured.load() ? "（已加密）" : "", s_connected.load() ? s_peer : "-", bondCount(),
                (unsigned long)s_tx_ok, (unsigned long)s_tx_fail, s_circle ? "開" : "關",
                killswitch::outputAllowed() ? "開" : "關", (unsigned)ESP.getFreeHeap());
}

}  // namespace ble_mouse
