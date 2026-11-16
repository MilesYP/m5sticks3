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

#include "config.h"
#include "input_hal.h"
#include "killswitch.h"
#include "mode_manager.h"
#include "ui_expression.h"
#include "ui_status.h"

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
  ui_status::printReport();
}

void loop() {
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
      // 模式 2 的 A 急停已由硬體中斷處理；其他模式的主動作留給後續里程碑
      if (mode_manager::current() != Mode::Safe && !killswitch::armedByBtnA()) {
        Serial.printf("[input] A pressed in %s (no action yet)\n",
                      mode_manager::currentName());
      }
      break;

    case InputEvent::None:
      break;
  }

  // (3) 重畫
  renderActive(false);
  delay(cfg::LOOP_YIELD_MS);
}
