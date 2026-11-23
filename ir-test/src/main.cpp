#include <M5Unified.h>
#include <driver/rmt.h>
#include <freertos/ringbuf.h>

#include "config.h"
#include "screen.h"
#include "ui_fx.h"

namespace {

constexpr gpio_num_t kRxPin = GPIO_NUM_42;
constexpr rmt_channel_t kRxChannel = RMT_CHANNEL_4;
constexpr uint8_t kClockDivider = 80;  // APB 80 MHz → 每 tick 1 µs
constexpr uint32_t kCounterHz = 1000000;
constexpr uint16_t kIdleUs = 20000;
constexpr size_t kRxBufferBytes = 1024;
constexpr uint32_t kUnknownLogMs = 500;

M5Canvas canvas(&M5.Display);
bool canvasReady = false;
bool previousExtPower = false;
bool powerChanged = false;
RingbufHandle_t rxBuffer = nullptr;
ir_test::State state = ir_test::State::Waiting;
nec::Frame lastFrame;
const char* errorText = "";
int battery = -1;
uint32_t decoded = 0;
uint32_t repeats = 0;
uint32_t unknown = 0;
uint32_t lastUnknownLog = 0;
bool redraw = true;

esp_err_t startReceiver() {
  rmt_config_t config = RMT_DEFAULT_CONFIG_RX(kRxPin, kRxChannel);
  config.clk_div = kClockDivider;
  config.rx_config.idle_threshold = kIdleUs;
  esp_err_t err = rmt_config(&config);
  if (err != ESP_OK) return err;
  uint32_t clockHz = 0;
  err = rmt_get_counter_clock(kRxChannel, &clockHz);
  if (err != ESP_OK) return err;
  if (clockHz != kCounterHz) return ESP_ERR_INVALID_STATE;
  err = rmt_driver_install(kRxChannel, kRxBufferBytes, 0);
  if (err != ESP_OK) return err;
  err = rmt_get_ringbuf_handle(kRxChannel, &rxBuffer);
  if (err == ESP_OK && !rxBuffer) err = ESP_ERR_INVALID_STATE;
  if (err == ESP_OK) err = rmt_rx_start(kRxChannel, true);
  if (err != ESP_OK) {
    rmt_driver_uninstall(kRxChannel);
    rxBuffer = nullptr;
  }
  return err;
}

void restorePower() {
  if (!powerChanged) return;
  M5.Power.setExtOutput(previousExtPower);
  powerChanged = false;
}

void stopReceiver() {
  rmt_rx_stop(kRxChannel);
  rmt_driver_uninstall(kRxChannel);
  rxBuffer = nullptr;
  restorePower();
  state = ir_test::State::Stopped;
  redraw = true;
  if (Serial) Serial.println("[ir1] STOPPED");
}

void printFrame() {
  Serial.printf("[ir1] %s ADDR=0x%04X CMD=0x%02X RAW=0x%08lX\n",
                nec::name(lastFrame.kind), static_cast<unsigned>(lastFrame.address),
                static_cast<unsigned>(lastFrame.command), static_cast<unsigned long>(lastFrame.raw));
}

void printStatus() {
  if (!Serial) return;
  if (state == ir_test::State::Error) Serial.printf("[ir1] ERROR %s\n", errorText);
  else if (state == ir_test::State::Stopped) Serial.println("[ir1] STOPPED");
  else Serial.printf("[ir1] READY RX=G%d RMT=%d EXT_5V=ON\n", kRxPin, kRxChannel);
  if (decoded) printFrame();
  Serial.printf("[ir1] count=%lu repeat=%lu unknown=%lu\n",
                static_cast<unsigned long>(decoded), static_cast<unsigned long>(repeats),
                static_cast<unsigned long>(unknown));
}

void pollReceiver() {
  if (!rxBuffer) return;
  size_t bytes = 0;
  auto* items = static_cast<rmt_item32_t*>(xRingbufferReceive(rxBuffer, &bytes, 0));
  if (!items) return;

  const size_t count = bytes / sizeof(*items);
  nec::Frame frame;
  if (count <= nec::kFullFrameSymbols && bytes % sizeof(*items) == 0) {
    nec::Symbol symbols[nec::kFullFrameSymbols];
    for (size_t i = 0; i < count; ++i) {
      symbols[i] = {static_cast<uint16_t>(items[i].duration0), static_cast<uint16_t>(items[i].duration1),
                    items[i].level0 != 0, items[i].level1 != 0};
    }
    frame = nec::decode(symbols, count);
  }
  const rmt_item32_t lead = count ? items[0] : rmt_item32_t{};
  vRingbufferReturnItem(rxBuffer, items);

  switch (frame.kind) {
    case nec::Kind::Standard:
    case nec::Kind::Extended:
      ++decoded;
      redraw |= state != ir_test::State::Received || lastFrame.raw != frame.raw;
      state = ir_test::State::Received;
      lastFrame = frame;
      if (Serial) printFrame();
      break;
    case nec::Kind::Repeat:
      ++repeats;
      break;
    case nec::Kind::Unknown:
      ++unknown;
      if (state == ir_test::State::Waiting) {
        state = ir_test::State::Unknown;
        redraw = true;
      }
      const uint32_t now = millis();
      if (Serial && (unknown == 1 || now - lastUnknownLog >= kUnknownLogMs)) {
        Serial.printf("[ir1] UNKNOWN symbols=%u lead=%u:%uus/%u:%uus\n",
                      static_cast<unsigned>(count), static_cast<unsigned>(lead.level0),
                      static_cast<unsigned>(lead.duration0), static_cast<unsigned>(lead.level1),
                      static_cast<unsigned>(lead.duration1));
        lastUnknownLog = now;
      }
      break;
  }
}

}  // namespace

void setup() {
  auto config = M5.config();
  config.internal_mic = false;
  M5.begin(config);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  M5.Speaker.end();
  M5.Display.setRotation(0);
  canvas.setPsram(true);
  canvas.setColorDepth(16);
  canvasReady = canvas.createSprite(M5.Display.width(), M5.Display.height()) != nullptr;
  if (canvasReady) ui_fx::begin();

  battery = M5.Power.getBatteryLevel();
  previousExtPower = M5.Power.getExtOutput();
  if (!previousExtPower) {
    powerChanged = M5.Power.setExtOutput(true);
  }
  if (!M5.Power.getExtOutput()) {
    errorText = "EXT_5V";
    state = ir_test::State::Error;
    restorePower();
  } else {
    const esp_err_t err = startReceiver();
    if (err != ESP_OK) {
      errorText = esp_err_to_name(err);
      state = ir_test::State::Error;
      restorePower();
    }
  }
  printStatus();
}

void loop() {
  M5.update();
  if (rxBuffer && M5.BtnB.pressedFor(cfg::BTN_B_KILL_MS)) stopReceiver();
  if (M5.BtnA.wasPressed()) {
    battery = M5.Power.getBatteryLevel();
    redraw = true;
    printStatus();
  }
  if (Serial.available() && Serial.read() == '?') printStatus();
  pollReceiver();
  if (redraw && canvasReady) {
    ir_test::drawScreen(canvas, state, lastFrame, errorText, battery);
    ui_fx::present(&canvas);
    redraw = false;
  }
  vTaskDelay(1);
}
