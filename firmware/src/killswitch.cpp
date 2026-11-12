#include "killswitch.h"

#include <Arduino.h>
#include <atomic>
#include <esp_timer.h>

#include "config.h"

namespace {

std::atomic<bool> s_triggered{false};
std::atomic<bool> s_output_allowed{true};
std::atomic<bool> s_armed_a{false};
std::atomic<uint8_t> s_source{static_cast<uint8_t>(KillSource::None)};
std::atomic<int32_t> s_latency_us{-1};
volatile int64_t s_trigger_us = 0;   // 只在 s_triggered 由 false→true 時寫入一次

TaskHandle_t s_task = nullptr;

// ISR 與 triggerSoftware 共用的「第一段」動作：必須極短、不可阻塞
inline void IRAM_ATTR closeGate(KillSource src) {
  s_output_allowed.store(false);
  if (!s_triggered.exchange(true)) {
    s_trigger_us = esp_timer_get_time();
    s_source.store(static_cast<uint8_t>(src));
  }
}

void IRAM_ATTR onBtnA() {
  if (!s_armed_a.load()) return;
  closeGate(KillSource::BtnA);   // 按鍵彈跳造成的重複觸發是冪等的，不需去彈跳
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(s_task, &woken);
  portYIELD_FROM_ISR(woken);
}

// 最高優先權 task：放 ISR 裡不能做、但要比主迴圈先做的硬停動作。
// M0 沒有實際輸出資源，只記錄延遲；日後模式 2 在這裡呼叫 HID「放開所有鍵」。
// 這裡不印 Serial：USB CDC 沒接電腦時寫入可能卡住，會拖住最高優先 task。
void killTask(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    s_output_allowed.store(false);
    s_latency_us.store(static_cast<int32_t>(esp_timer_get_time() - s_trigger_us));
  }
}

}  // namespace

namespace killswitch {

void begin() {
  xTaskCreatePinnedToCore(killTask, "killswitch", cfg::KILL_TASK_STACK, nullptr,
                          configMAX_PRIORITIES - 1, &s_task, cfg::KILL_TASK_CORE);
  // M5Unified 已把 G11 設為 input（按下 = LOW），這裡只掛下降緣中斷
  attachInterrupt(digitalPinToInterrupt(cfg::PIN_BTN_A), onBtnA, FALLING);
}

bool isTriggered() { return s_triggered.load(); }
bool outputAllowed() { return s_output_allowed.load(); }

void triggerSoftware() {
  closeGate(KillSource::BtnBHold);
  xTaskNotifyGive(s_task);
}

void setArmedByBtnA(bool armed) { s_armed_a.store(armed); }
bool armedByBtnA() { return s_armed_a.load(); }

void clear() {
  s_source.store(static_cast<uint8_t>(KillSource::None));
  s_output_allowed.store(true);
  s_triggered.store(false);
}

KillSource source() { return static_cast<KillSource>(s_source.load()); }

const char* sourceName() {
  switch (source()) {
    case KillSource::BtnA:     return "BtnA";
    case KillSource::BtnBHold: return "BtnB-hold3s";
    default:                   return "-";
  }
}

int32_t lastLatencyUs() { return s_latency_us.load(); }

}  // namespace killswitch
