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
#include "audio_io.h"
#include "battery_ui.h"
#include "net_hub.h"
#include "ui_expression.h"
#include "ui_fx.h"
#include "ui_info.h"
#include "ui_status.h"
#include "voice_turn.h"

// B3b-2：表情改由本機（voice_turn）決定，中控回的 state 只印在序列埠（net_hub 已印）。

// B3b-4 斷線提示（Miles 選定）：中控斷線超過 OFFLINE_SHOW_MS → 表情定格、眼睛變灰、右上角 Wi-Fi 斜線；
// 恢復連線後回待機臉（這一輪語音若還在播已收到的部分，播完自己會回 Idle）。
static void updateOffline() {
  static bool shown = false;
  const bool off = net_hub::linkDownMs() > cfg::OFFLINE_SHOW_MS;
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
  if (mode_manager::current() == Mode::Desktop) {
    if (ui_info::shown()) ui_info::render();
    else ui_expression::render();
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

  input_hal::begin();
  killswitch::begin();
  mode_manager::begin(Mode::Desktop);
  ui_status::begin();        // 設定螢幕方向並建立狀態畫布
  ui_expression::begin();    // 需在 ui_status::begin 之後
  ui_info::begin();          // A5-1：資訊頁畫布（PSRAM 約 64KB）
  ui_fx::begin();            // A5-1：換整頁時的交叉淡化（PSRAM 約 128KB）
  net_hub::begin();          // 連 Wi-Fi + 建網路 task（B2a／B3b-5）
  audio_io::begin();             // 音訊緩衝（B3b-1）
  voice_turn::begin();           // 一輪語音：錄→傳中控→收回→播（B3b-2）
  audio_io::setDiagLabel(net_hub::configLabel);  // 錄音那一行附 Wi-Fi 設定（B3.6）
  diag_cmd::begin();             // 序列埠診斷指令（B3.6）
  imu_pet::begin();              // A3：IMU 桌寵互動（序列埠 j／v 量測）
  battery_ui::begin();           // A3：休息臉顏色跟電量（序列埠 c 假裝電量）
  ui_status::printReport();
}

void loop() {
  net_hub::loop();   // B3b-5：代印網路 task 的 log、依序處理收到的訊息（WebSocket 本身在網路 task 跑）
  diag_cmd::loop();  // 序列埠診斷指令（B3.6，非阻塞）

  // (1) 急停第二段
  if (killswitch::isTriggered() && mode_manager::current() != Mode::Safe) {
    mode_manager::emergencyStop();
    renderActive(true);
  }

  // (2) 輸入事件
  switch (input_hal::poll()) {
    case InputEvent::BtnB_Click:
      if (mode_manager::current() == Mode::Safe) mode_manager::resumeFromSafe();
      else mode_manager::next();
      renderActive(true);
      break;

    case InputEvent::BtnB_Hold1s:
      ui_status::printReport();
      break;

    case InputEvent::BtnB_Hold3s:
      killswitch::triggerSoftware();
      mode_manager::emergencyStop();
      renderActive(true);
      break;

    case InputEvent::BtnA_Press:
      // Desktop：按住 A 講話（錄音 → 上傳中控 → 邊收邊播，見 voice_turn）
      if (mode_manager::current() == Mode::Desktop) {
        s_a_was_busy = voice_turn::busy();
        voice_turn::onPress();
        break;
      }
      // 模式 2 的 A 急停已由硬體中斷處理；其他模式的主動作留給後續里程碑
      if (mode_manager::current() != Mode::Safe && !killswitch::armedByBtnA()) {
        Serial.printf("[input] A pressed in %s (no action yet)\n",
                      mode_manager::currentName());
      }
      break;

    case InputEvent::BtnA_Release:
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

  // 錄音餵資料、播完切回麥克風（B3b-1）；一輪語音的上傳／等待／收尾（B3b-2）
  audio_io::loop();
  voice_turn::loop();

  imu_pet::loop();      // A3：讀 IMU、搖／拍／靜置／睡著聽聲音 → 表情（非阻塞）
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
  if (mode_manager::current() != Mode::Desktop) ui_fx::cancel();
  renderActive(false);
  ui_fx::loop();
  delay(cfg::LOOP_YIELD_MS);
}
