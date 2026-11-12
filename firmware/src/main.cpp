// ─────────────────────────────────────────────────────────────────────────
//  M5StickS3 極客口袋終端 — 正式韌體進入點（M0 骨架）
//
//  loop 每一圈的順序：
//    (1) 急停第二段：killswitch 已觸發 → mode_manager 回收資源進 Safe
//    (2) 讀輸入事件 → 分派（按鍵配置見 CLAUDE.md 第 2 節）
//    (3) 依 millis() 重畫狀態畫面
//  全程不用長 delay()；急停第一段在 ISR + 最高優先 task，不依賴本迴圈。
// ─────────────────────────────────────────────────────────────────────────
#include <M5Unified.h>

#include "config.h"
#include "input_hal.h"
#include "killswitch.h"
#include "mode_manager.h"
#include "ui_status.h"

void setup() {
  auto m5cfg = M5.config();
  M5.begin(m5cfg);
  Serial.begin(115200);
  Serial.println("\n===== M5StickS3 firmware M0 =====");

  input_hal::begin();
  killswitch::begin();
  mode_manager::begin(Mode::Desktop);
  ui_status::begin();
  ui_status::printReport();
}

void loop() {
  // (1) 急停第二段
  if (killswitch::isTriggered() && mode_manager::current() != Mode::Safe) {
    mode_manager::emergencyStop();
    ui_status::render(true);
  }

  // (2) 輸入事件
  switch (input_hal::poll()) {
    case InputEvent::BtnB_Click:
      if (mode_manager::current() == Mode::Safe) mode_manager::resumeFromSafe();
      else mode_manager::next();
      ui_status::render(true);
      break;

    case InputEvent::BtnB_Hold1s:
      ui_status::printReport();
      break;

    case InputEvent::BtnB_Hold3s:
      killswitch::triggerSoftware();
      mode_manager::emergencyStop();
      ui_status::render(true);
      break;

    case InputEvent::BtnA_Press:
      // 模式 2 的 A 急停已由硬體中斷處理；其他模式的主動作留給後續里程碑
      if (mode_manager::current() != Mode::Safe && !killswitch::armedByBtnA()) {
        Serial.printf("[input] A pressed in %s (no action in M0)\n",
                      mode_manager::currentName());
      }
      break;

    case InputEvent::None:
      break;
  }

  // (3) 定期重畫
  ui_status::render(false);
  delay(cfg::LOOP_YIELD_MS);
}
