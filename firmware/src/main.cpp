// ─────────────────────────────────────────────────────────────────────────
//  M5StickS3 極客口袋終端 — 正式韌體進入點
//
//  loop 每一圈的順序：
//    (1) 急停第二段：killswitch 已觸發 → mode_manager 回收資源進 Safe
//    (2) 讀輸入事件 → 分派（按鍵配置見 CLAUDE.md 第 2 節）
//    (3) 重畫畫面：Desktop 模式畫表情（M1-A），其餘模式畫狀態畫面
//  全程不用長 delay()；急停第一段在 ISR + 最高優先 task，不依賴本迴圈。
// ─────────────────────────────────────────────────────────────────────────
#include <M5Unified.h>
#include <string.h>

#include "config.h"
#include "diag_cmd.h"
#include "imu_pet.h"
#include "input_hal.h"
#include "killswitch.h"
#include "mode_manager.h"
#include "air_mouse.h"
#include "audio_io.h"
#include "battery_ui.h"
#include "ble_mouse.h"
#include "net_hub.h"
#include "power_key.h"
#include "power_save.h"
#include "ui_expression.h"
#include "ui_fx.h"
#include "ui_info.h"
#include "ui_modeswitch.h"
#include "ui_status.h"
#include "voice_turn.h"

// B3b-2：表情改由本機（voice_turn）決定，中控回的 state 只印在序列埠（net_hub 已印）。

// B3b-4 斷線提示（Miles 選定）：中控斷線超過 OFFLINE_SHOW_MS → 表情定格、眼睛變灰、右上角 Wi-Fi 斜線；
// 恢復連線後回待機臉（這一輪語音若還在播已收到的部分，播完自己會回 Idle）。
static void updateOffline() {
  static bool shown = false;
  // P2：省電主動斷 Wi-Fi 時不算斷線；剛叫醒重連（含 TLS 約 3～5 秒）的那段也先不顯示
  const uint32_t thr = net_hub::radioOnForMs() < cfg::OFFLINE_WAKE_GRACE_MS ? cfg::OFFLINE_WAKE_GRACE_MS : cfg::OFFLINE_SHOW_MS;
  const bool off = !net_hub::radioOff() && net_hub::linkDownMs() > thr;
  if (off == shown) return;
  shown = off;
  ui_expression::setOffline(off);
  if (off) {
    Serial.println("[ui] 斷線提示：表情定格");
  } else {
    Serial.println("[ui] 連線恢復：回待機");
    if (!voice_turn::busy()) ui_expression::setMood(Mood::Idle);
  }
}

// Desktop 模式畫表情或資訊頁（A5-1），其餘模式（含 Safe 的 KILLED 畫面）畫狀態畫面。
static void renderActive(bool force) {
  if (ui_modeswitch::render()) return;   // 切模式過場中（結束那一圈會回 false，接著畫新模式）
  if (mode_manager::current() == Mode::Desktop) {
    if (ui_info::shown()) ui_info::render();
    else if (force || power_save::frameDue()) { ui_expression::render(); power_save::frameDrawn(); }   // P1：待機／想睡／睡著降畫面張數
  } else {
    ui_status::render(force);
  }
}

// 目前螢幕上那一頁的畫布（換整頁時 ui_fx 存舊畫面用）
static M5Canvas* showingCanvas() { return ui_info::shown() ? ui_info::canvas() : ui_expression::canvas(); }

// A5-1 資訊頁切換：
//  - Desktop 短按 A（< 0.6 秒）：沿用語音誤觸防護——一按就照常開錄，放開時 voice_turn 判為誤觸、取消這輪，
//    這裡再把它當成「切頁」。按下前 Friday 正在講話／處理中的話，短按＝打斷（原本行為），不切頁。
//  - 資訊頁上按住 A 滿 0.6 秒（或這輪還在忙）→ 自動回表情頁（對話區在表情頁）。
//  - 離開 Desktop → 回到表情頁（下次進 Desktop 從表情頁開始）。
static bool s_a_was_busy = false;
// P2：Desktop 關螢幕或睡著變暗（省電等級 Asleep）時 A 鍵：短按不理，按住滿 1 秒（PWR_WAKE_A_MS）＝醒來（亮螢幕＋連回中控，不錄音，要說話放開再按）
static uint32_t s_a_off_down = 0;    // 關螢幕時按下 A 的時刻（0＝沒有）
static bool     s_a_skip_release = false;
static void updatePage() {
  if (!ui_info::shown()) return;
  if (mode_manager::current() != Mode::Desktop) ui_info::setShown(false, "離開 Desktop");
  else if (voice_turn::faceWanted()) { ui_fx::start(showingCanvas()); ui_info::setShown(false, "說話"); }
}

void setup() {
  auto m5cfg = M5.config();
  M5.begin(m5cfg);
  Serial.begin(115200);
  Serial.println("\n===== M5S3Pack firmware M1-A / A5-1 =====");
  {   // 31-m4b：上次為什麼重開（當機 PANIC、看門狗 WDT、電壓不足 BROWNOUT…）——查「自己重開」用
    static const char* kRst[] = {"UNKNOWN", "POWERON", "EXT", "SW", "PANIC(當機)", "INT_WDT(中斷看門狗)",
                                 "TASK_WDT(任務看門狗)", "WDT", "DEEPSLEEP", "BROWNOUT(電壓不足)", "SDIO"};
    const int r = (int)esp_reset_reason();
    Serial.printf("[boot] 上次重開原因：%s（%d）\n", r >= 0 && r <= 10 ? kRst[r] : "?", r);
  }

  input_hal::begin();
  killswitch::begin();
  mode_manager::begin(Mode::Desktop);
  ui_status::begin();        // 設定螢幕方向並建立狀態畫布
  ui_expression::begin();    // 需在 ui_status::begin 之後
  ui_info::begin();          // A5-1：資訊頁畫布（PSRAM 約 64KB）
  ui_fx::begin();            // A5-1：換整頁時的交叉淡化（PSRAM 約 128KB）
  ui_modeswitch::begin();    // B 切模式的 EVA 風過場（PSRAM 約 64KB）
  net_hub::begin();          // 連 Wi-Fi + 建網路 task（B2a／B3b-5）
  audio_io::begin();             // 音訊緩衝（B3b-1）
  voice_turn::begin();           // 一輪語音：錄→傳中控→收回→播（B3b-2）
  audio_io::setDiagLabel(net_hub::configLabel);  // 錄音那一行附 Wi-Fi 設定（B3.6）
  diag_cmd::begin();             // 序列埠診斷指令（B3.6）
  imu_pet::begin();              // A3：IMU 桌寵互動（序列埠 j／v 量測）
  battery_ui::begin();           // A3：休息臉顏色跟電量（序列埠 c 假裝電量）
  power_save::begin();           // P1：背光／畫面張數／CPU／Wi-Fi 依桌寵狀態省電（序列埠 z）
  power_key::begin();            // P2：電源鍵新機制（PM1 設定、看門狗；序列埠 q）
  ui_status::printReport();
}

void loop() {
  net_hub::loop();   // B3b-5：代印網路 task 的 log、依序處理收到的訊息（WebSocket 本身在網路 task 跑）
  diag_cmd::loop();  // 序列埠診斷指令（B3.6，非阻塞）
  power_key::loop(); // P2：餵 PM1 看門狗、輪詢電源鍵（單按＝開關螢幕、長按＝關機／root、連按五下＝強制關機）

  // (1) 急停第二段
  if (killswitch::isTriggered() && mode_manager::current() != Mode::Safe) {
    power_save::setScreenOn(true, "急停");   // P2：急停一律亮螢幕
    ui_modeswitch::cancel();   // 急停不走過場
    mode_manager::emergencyStop();
    renderActive(true);
  }

  // (2) 輸入事件
  switch (input_hal::poll()) {
    case InputEvent::BtnB_Click:
      if (!power_save::screenOn()) break;   // P2（11-22 Miles）：關螢幕時 B 短按不理（只有長按 A、單按電源鍵會亮；B 按住 3 秒急停照樣亮）
      if (mode_manager::current() == Mode::Safe) {
        mode_manager::resumeFromSafe();
      } else {
        ui_modeswitch::start(mode_manager::peekNext());   // 先把過場推上螢幕，蓋住 exit()／enter() 的時間
        mode_manager::next();
        ui_modeswitch::ready();
      }
      renderActive(true);
      break;

    case InputEvent::BtnB_Hold1s:
      ui_status::printReport();
      break;

    case InputEvent::BtnB_Hold3s:
      power_save::setScreenOn(true, "急停");
      killswitch::triggerSoftware();
      ui_modeswitch::cancel();
      mode_manager::emergencyStop();
      renderActive(true);
      break;

    case InputEvent::BtnA_Press:
      if (ui_modeswitch::active()) { s_a_skip_release = true; break; }   // 31-m4f：過場（等連線）中按 A 不理
      // Desktop：按住 A 講話（錄音 → 上傳中控 → 邊收邊播，見 voice_turn）
      if (mode_manager::current() == Mode::Desktop && !voice_turn::busy() &&
          (!power_save::screenOn() || power_save::level() == power_save::Level::Asleep)) {
        s_a_off_down = millis() | 1;   // P2：關螢幕／睡著變暗：等按滿 1 秒才醒（見下面）
        break;
      }
      if (mode_manager::current() == Mode::Desktop) {
        s_a_was_busy = voice_turn::busy();
        voice_turn::onPress();
        break;
      }
      if (mode_manager::current() == Mode::AirMouse) {   // M4-1：A＝左鍵（按下）
        air_mouse::onButton(true);
        break;
      }
      // 模式 2 的 A 急停已由硬體中斷處理；其他模式的主動作留給後續里程碑
      if (mode_manager::current() != Mode::Safe && !killswitch::armedByBtnA()) {
        Serial.printf("[input] A pressed in %s (no action yet)\n",
                      mode_manager::currentName());
      }
      break;

    case InputEvent::BtnA_Release:
      if (s_a_off_down || s_a_skip_release) {   // P2：關螢幕／睡著時按的 A（短按不理；長按已在按住時叫醒）
        s_a_off_down = 0;
        s_a_skip_release = false;
        break;
      }
      if (mode_manager::current() == Mode::AirMouse) {   // M4-1：左鍵放開（短按＝點擊、按住移動＝拖曳）
        air_mouse::onButton(false);
        break;
      }
      if (mode_manager::current() == Mode::Desktop) {
        const uint32_t t0 = millis();
        const bool tap = voice_turn::onRelease();   // true＝按不到 0.6 秒，這輪已取消
        if (tap && !s_a_was_busy) {
          const uint32_t t1 = millis();
          ui_fx::start(showingCanvas());              // 先存舊畫面，新的一頁畫好後交叉淡化過去
          ui_info::setShown(!ui_info::shown(), "短按 A");
          renderActive(true);                         // 這一圈就畫出新的一頁
          Serial.printf("[ui] 切頁耗時：取消錄音 %lu ms＋畫面 %lu ms\n", (unsigned long)(t1 - t0),
                        (unsigned long)(millis() - t1));
        }
      }
      break;

    case InputEvent::None:
      break;
  }

  // 看實際按鍵狀態，不只看事件：快速點一下時「按下／放開」可能落在同一次 M5.update，放開事件被吃掉 → 以前會誤亮
  if (s_a_off_down && !M5.BtnA.isPressed()) s_a_off_down = 0;
  if (s_a_off_down && millis() - s_a_off_down >= cfg::PWR_WAKE_A_MS) {
    s_a_off_down = 0;
    s_a_skip_release = true;
    if (!power_save::screenOn()) power_save::setScreenOn(true, "長按 A");
    else imu_pet::wake("長按 A");   // 睡著變暗（螢幕還開著）：叫醒回待機，背光立刻亮
  }

  // 錄音餵資料、播完切回麥克風（B3b-1）；一輪語音的上傳／等待／收尾（B3b-2）
  audio_io::loop();
  voice_turn::loop();
  ble_mouse::loop();    // M4：藍牙滑鼠（代印連線 log、@1 畫圓測試、急停檢查）

  imu_pet::loop();      // A3：讀 IMU（M4-1：每筆也交給 air_mouse）—— 必須在 air_mouse::loop 之前
  air_mouse::loop();    // M4-1：空中滑鼠送游標／按鍵報告、搖／拍／靜置／睡著聽聲音 → 表情（非阻塞）
  battery_ui::loop();   // A3：每 5 秒讀電量 → 休息臉顏色
  ui_info::loop();      // A5-1：收負載／記憶體歷史、Wi-Fi 連上後開始對時（不畫圖）
  updatePage();

  // (3) 重畫
  updateOffline();
  {   // A5-1：Desktop 轉向（直式↔橫式）＝整頁換掉 → 背光過場
    static Orient s_last = Orient::Portrait;
    const Orient o = ui_expression::orientation();
    if (o != s_last) {
      s_last = o;
      if (mode_manager::current() == Mode::Desktop) ui_fx::start(showingCanvas());   // 畫布內容還是轉向前的
    }
  }
  if (mode_manager::current() != Mode::Desktop && !ui_modeswitch::active()) ui_fx::cancel();
  power_save::loop();   // P1：依這一圈的狀態決定省電等級（在畫圖之前）
  if (power_save::screenOn() && !power_key::overlayActive()) {   // P2：關螢幕／電源鍵長按畫面時不畫一般畫面
    renderActive(false);
    ui_fx::loop();
    // root 模式角落「ROOT」：表情頁已畫在狀態列（電量左邊），其他畫面才蓋小標
    if (!(mode_manager::current() == Mode::Desktop && !ui_info::shown())) power_key::drawBadge();
  }
  delay(power_save::yieldMs());   // P1：兩張畫面之間讓 CPU 休息（最多 10ms 一圈）
}
