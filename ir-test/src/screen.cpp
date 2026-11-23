#include "screen.h"

#include "ir_test_labels.h"
#include "ui_kit.h"

namespace ir_test {

namespace ink = ui_kit::color;

void drawScreen(M5Canvas& cv, State state, const nec::Frame& frame, const char* error, int battery) {
  const int w = cv.width();
  cv.fillScreen(ink::BG);
  ui_kit::statusBar(cv, 0, w, "IR1", "紅外線", ink::AMBER, battery, ui_kit::batteryColor(battery));

  const auto& title = *ir_test_labels::find("title");
  ui_kit::glyphFit(cv, title.bits, title.w, title.h, 3, 23, title.w, title.h, ink::RED);
  ui_kit::textFit(cv, "RMT RX / G42", 5, 64, ink::DIM, &fonts::Font0);
  cv.drawFastHLine(4, 79, w - 8, ink::FRAME);

  const bool received = state == State::Received;
  ui_kit::hexShape(cv, 18, 103, 11, ink::AMBER, received);
  ui_kit::textFit(cv, "RX", 18, 103, received ? ink::BG : ink::AMBER, &fonts::Font0, 0, middle_center);

  const char* message = "等待訊號";
  uint16_t messageInk = ink::AMBER;
  if (received) {
    message = nec::name(frame.kind);
    messageInk = ink::GREEN;
  } else if (state == State::Unknown) {
    message = "收到未知訊號";
    messageInk = ink::YELLOW;
  } else if (state == State::Error || state == State::Stopped) {
    message = state == State::Error ? "接收初始化失敗" : "已停止接收";
    messageInk = ink::RED;
  }
  const lgfx::IFont* messageFont = &fonts::efontTW_12;
  if (received) messageFont = &fonts::Font0;
  ui_kit::textFit(cv, message, 36, 96, messageInk, messageFont, w - 39);

  if (received) {
    char line[22];
    snprintf(line, sizeof line, "ADDR 0x%04X", static_cast<unsigned>(frame.address));
    ui_kit::textFit(cv, line, 36, 119, ink::BRIGHT, &fonts::Font0);
    snprintf(line, sizeof line, "CMD  0x%02X", static_cast<unsigned>(frame.command));
    ui_kit::textFit(cv, line, 36, 135, ink::BRIGHT, &fonts::Font0);
    snprintf(line, sizeof line, "RAW  0x%08lX", static_cast<unsigned long>(frame.raw));
    ui_kit::textFit(cv, line, 36, 151, ink::BRIGHT, &fonts::Font0);
  } else if (state == State::Error) {
    ui_kit::textFit(cv, error, 5, 130, ink::BRIGHT, &fonts::Font0, w - 10);
  } else if (state == State::Waiting || state == State::Unknown) {
    ui_kit::textFit(cv, "對準頂端，按遙控器", 5, 130, ink::BRIGHT, &fonts::efontTW_12, w - 10);
    ui_kit::textFit(cv, "距離大於 30 公分", 5, 147, ink::BRIGHT, &fonts::efontTW_12, w - 10);
  }

  if (state != State::Stopped && state != State::Error) {
    ui_kit::textFit(cv, "B 按住 3 秒 停止", 5, 201, ink::DIM, &fonts::efontTW_12, w - 10);
  }
  const auto& reprint = *ir_test_labels::find("reprint");
  ui_kit::actionBand(cv, 222, w, 18, ink::YELLOW, ink::AMBER, reprint.bits, reprint.w, reprint.h);
}

}  // namespace ir_test
