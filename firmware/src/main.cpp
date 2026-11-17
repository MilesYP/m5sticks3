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
#include "input_hal.h"
#include "killswitch.h"
#include "mode_manager.h"
#include "audio_io.h"
#include "net_hub.h"
#include "ui_expression.h"
#include "ui_status.h"
#include "voice_turn.h"

// B3b-2：表情改由本機（voice_turn）決定，中控回的 state 只印在序列埠（net_hub 已印）。

// Desktop 模式畫表情，其餘模式（含 Safe 的 KILLED 畫面）畫狀態畫面。
static void renderActive(bool force) {
  if (mode_manager::current() == Mode::Desktop) ui_expression::render();
  else ui_status::render(force);
}

void setup() {
  auto m5cfg = M5.config();
  M5.begin(m5cfg);
  Serial.begin(115200);
  Serial.println("\n===== M5StickS3 firmware M1-A =====");

  input_hal::begin();
  killswitch::begin();
  mode_manager::begin(Mode::Desktop);
  ui_status::begin();        // 設定螢幕方向並建立狀態畫布
  ui_expression::begin();    // 需在 ui_status::begin 之後
  net_hub::begin();          // 連 Wi-Fi + 中控（B2a）
  audio_io::begin();             // 音訊緩衝（B3b-1）
  voice_turn::begin();           // 一輪語音：錄→傳中控→收回→播（B3b-2）
  audio_io::setDiagLabel(net_hub::configLabel);  // 錄音那一行附 Wi-Fi 設定（B3.6）
  diag_cmd::begin();             // 序列埠診斷指令（B3.6）
  ui_status::printReport();
}

void loop() {
  net_hub::loop();   // 驅動 Wi-Fi / WebSocket（非阻塞）
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
      // Desktop：按住 A 講話（B3b-2：錄音 → 上傳中控 → 原樣收回 → 播放，見 voice_turn）
      if (mode_manager::current() == Mode::Desktop) {
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
      if (mode_manager::current() == Mode::Desktop) voice_turn::onRelease();
      break;

    case InputEvent::None:
      break;
  }

  // 錄音餵資料、播完切回麥克風（B3b-1）；一輪語音的上傳／等待／收尾（B3b-2）
  audio_io::loop();
  voice_turn::loop();

  // (3) 重畫
  renderActive(false);
  delay(cfg::LOOP_YIELD_MS);
}
