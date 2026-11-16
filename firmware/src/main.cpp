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
#include "input_hal.h"
#include "killswitch.h"
#include "mode_manager.h"
#include "audio_io.h"
#include "net_hub.h"
#include "ui_expression.h"
#include "ui_status.h"

// 中控回的狀態 → 切表情（B2b）
static void onHubState(const char* v) {
  if      (strcmp(v, "listening") == 0) ui_expression::setMood(Mood::Listening);
  else if (strcmp(v, "thinking")  == 0) ui_expression::setMood(Mood::Thinking);
  else if (strcmp(v, "speaking")  == 0) ui_expression::setMood(Mood::Speaking);
  else                                  ui_expression::setMood(Mood::Idle);
}

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
  net_hub::onState(onHubState);  // 中控狀態 → 表情（B2b）
  audio_io::begin();             // 音訊緩衝（B3b-1）
  ui_status::printReport();
}

void loop() {
  net_hub::loop();   // 驅動 Wi-Fi / WebSocket（非阻塞）

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
      // Desktop：長按 A = 對中控「開始講話」（B2b；B3 才串真音訊）
      if (mode_manager::current() == Mode::Desktop) {
        // B3b-1：本機回放測試 — 按住 A 錄音（B3b-2 會改回串給中控）
        audio_io::startRecording();
        ui_expression::setMood(Mood::Listening);
        break;
      }
      // 模式 2 的 A 急停已由硬體中斷處理；其他模式的主動作留給後續里程碑
      if (mode_manager::current() != Mode::Safe && !killswitch::armedByBtnA()) {
        Serial.printf("[input] A pressed in %s (no action yet)\n",
                      mode_manager::currentName());
      }
      break;

    case InputEvent::BtnA_Release:
      // Desktop：放開 A = 停止錄音並把剛錄的播出來（B3b-1）
      if (mode_manager::current() == Mode::Desktop) {
        audio_io::stopRecordingAndPlay();
        ui_expression::setMood(Mood::Speaking);
      }
      break;

    case InputEvent::None:
      break;
  }

  // 錄音餵資料；放音結束就回待機（B3b-1）
  audio_io::feedRecording();
  if (mode_manager::current() == Mode::Desktop &&
      ui_expression::mood() == Mood::Speaking && !audio_io::isPlaying()) {
    ui_expression::setMood(Mood::Idle);
  }

  // (3) 重畫
  renderActive(false);
  delay(cfg::LOOP_YIELD_MS);
}
