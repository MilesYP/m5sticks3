#include <M5Unified.h>
#include <atomic>
#include <driver/gpio.h>
#include <esp_system.h>
#ifdef IR_TEST_RMT5
#include <driver/rmt_rx.h>
#else
#include <driver/rmt.h>
#include <freertos/ringbuf.h>
#endif

#include "config.h"
#include "screen.h"
#include "ui_fx.h"

namespace {

constexpr gpio_num_t kRxPin = GPIO_NUM_42;
constexpr uint32_t kCounterHz = 1000000;
constexpr uint16_t kIdleUs = 20000;
#ifdef IR_TEST_RMT5
constexpr size_t kRmtMemorySymbols = 128;
constexpr size_t kRxSymbols = 64;
constexpr uint32_t kMinPulseNs = 1000;
using RxItem = rmt_symbol_word_t;
rmt_channel_handle_t rxChannel = nullptr;
RxItem rxItems[kRxSymbols];
std::atomic<size_t> rxDone{0};
#else
constexpr rmt_channel_t kRxChannel = RMT_CHANNEL_4;
constexpr uint8_t kClockDivider = 80;  // APB 80 MHz → 每 tick 1 µs
constexpr size_t kRxBufferBytes = 1024;
using RxItem = rmt_item32_t;
RingbufHandle_t rxBuffer = nullptr;
#endif
constexpr uint32_t kUnknownLogMs = 500;
constexpr uint32_t kBacklightSampleMs = 10000;
constexpr uint8_t kPmGpioFunc0 = 0x16;
constexpr uint8_t kPmGpioMode = 0x10;
constexpr uint8_t kPmGpioDrive = 0x13;
constexpr uint8_t kPmGpioOut = 0x11;
constexpr uint8_t kPmPowerSource = 0x04;
constexpr uint8_t kPm5vOut = 0x26;
constexpr uint8_t kPyg3Bit = 1 << 3;
constexpr uint8_t kVinPresent = 1 << 0;
constexpr uint32_t kPowerPollMs = 250;
constexpr uint32_t kPowerSettleMs = 1000;
constexpr uint32_t kPowerSampleMs = 10000;
constexpr uint32_t kPowerStageTimeoutMs = 180000;

M5Canvas canvas(&M5.Display);
bool canvasReady = false;
bool previousExtPower = false;
bool powerChanged = false;
bool receiverActive = false;
ir_test::State state = ir_test::State::Waiting;
nec::Frame lastFrame;
const char* errorText = "";
int battery = -1;
uint32_t decoded = 0;
uint32_t repeats = 0;
uint32_t unknown = 0;
uint32_t lastUnknownLog = 0;
bool redraw = true;
std::atomic<uint32_t> rxEdges{0};
esp_err_t edgeProbe = ESP_ERR_INVALID_STATE;
const char* edgeProbeStatus() {
#ifdef IR_TEST_RMT5
  if (edgeProbe == ESP_ERR_INVALID_STATE) return "OFF";
#endif
  return esp_err_to_name(edgeProbe);
}
uint32_t bootId = 0;

struct RxCounts {
  uint32_t edges;
  uint32_t decoded;
  uint32_t repeats;
  uint32_t unknown;
};

struct BacklightTest {
  uint8_t step = 0;
  uint8_t originalBrightness = 0;
  uint32_t startedAt = 0;
  RxCounts baseline{};
} backlightTest;

struct PowerSample {
  RxCounts baseline{};
  RxCounts delta{};
  uint32_t startedAt = 0;
  uint32_t elapsed = 0;
  uint32_t mvSum = 0;
  unsigned mvCount = 0;
  unsigned mvMin = 0;
  unsigned mvMax = 0;
};

struct PowerTest {
  PowerSample samples[3]{};
  unsigned step = 0;
  uint8_t brightness = 0;
  bool settling = false;
  bool sampling = false;
  uint32_t stageAt = 0;
  uint32_t stableSince = 0;
  uint32_t lastPoll = 0;
  bool active = false;
  const char* status = "NONE";
  const char* probeStatus = "NONE";
} powerTest;

RxCounts readCounts() {
  return {rxEdges.load(std::memory_order_relaxed), decoded, repeats, unknown};
}

bool readPm(uint8_t reg, uint8_t* data, size_t size = 1) {
  return M5.In_I2C.readRegister(cfg::PM1_ADDR, reg, data, size, cfg::PM1_I2C_HZ);
}

void printPyg3() {
  uint8_t func, mode, drive, out;
  if (!readPm(kPmGpioFunc0, &func) || !readPm(kPmGpioMode, &mode) ||
      !readPm(kPmGpioDrive, &drive) || !readPm(kPmGpioOut, &out)) {
    Serial.println("[ir1] PYG3 ERROR I2C");
    return;
  }
  const unsigned function = func >> 6;
  const bool output = mode & kPyg3Bit;
  const bool openDrain = drive & kPyg3Bit;
  const bool high = out & kPyg3Bit;
  Serial.printf("[ir1] PYG3 FUNC0=0x%02X MODE=0x%02X DRV=0x%02X OUT=0x%02X "
                "func=%u output=%u open_drain=%u high=%u amp_off_config=%u\n",
                func, mode, drive, out, function, output, openDrain, high,
                function == 0 && output && !openDrain && !high);
}

void printPowerTest() {
  if (!Serial) return;
  Serial.printf("[ir1] PWR boot=%08lX status=%s brightness=%u completed=%u probe=%s\n",
                static_cast<unsigned long>(bootId), powerTest.status, powerTest.brightness, powerTest.step,
                powerTest.probeStatus);
  for (unsigned i = 0; i < powerTest.step; ++i) {
    const auto& s = powerTest.samples[i];
    Serial.printf("[ir1] PWR step=%u source=%s ms=%lu edges=%lu count=%lu repeat=%lu unknown=%lu "
                  "ext_mv_avg=%lu ext_mv_min=%u ext_mv_max=%u n=%u\n",
                  i + 1, i == 1 ? "BAT" : "USB", static_cast<unsigned long>(s.elapsed),
                  static_cast<unsigned long>(s.delta.edges), static_cast<unsigned long>(s.delta.decoded),
                  static_cast<unsigned long>(s.delta.repeats), static_cast<unsigned long>(s.delta.unknown),
                  static_cast<unsigned long>(s.mvSum / s.mvCount), s.mvMin, s.mvMax, s.mvCount);
  }
}

void finishPowerTest(const char* status) {
  powerTest.active = false;
  powerTest.status = status;
  printPowerTest();
}

void startPowerTest() {
  if (powerTest.active) {
    printPowerTest();
    return;
  }
  uint8_t source;
  if (!receiverActive || backlightTest.step ||
      !readPm(kPmPowerSource, &source) || !(source & kVinPresent)) {
    Serial.println("[ir1] PWR ERROR need active RX, VIN and fixed backlight");
    return;
  }
  powerTest = {};
  powerTest.brightness = M5.Display.getBrightness();
  powerTest.probeStatus = edgeProbeStatus();
  powerTest.stageAt = millis();
  powerTest.active = true;
  powerTest.status = "RUNNING";
  printPowerTest();
}

void pollPowerTest() {
  if (!powerTest.active) return;
  const uint32_t now = millis();
  if (now - powerTest.stageAt >= kPowerStageTimeoutMs ||
      M5.Display.getBrightness() != powerTest.brightness) {
    finishPowerTest("INVALID_TIMEOUT_OR_BRIGHTNESS");
    return;
  }
  if (now - powerTest.lastPoll < kPowerPollMs) return;
  powerTest.lastPoll = now;
  uint8_t source;
  const bool wantVin = powerTest.step != 1;
  if (!readPm(kPmPowerSource, &source) || bool(source & kVinPresent) != wantVin) {
    powerTest.settling = powerTest.sampling = false;
    return;
  }
  if (!powerTest.settling) {
    powerTest.settling = true;
    powerTest.stableSince = now;
    return;
  }
  if (now - powerTest.stableSince < kPowerSettleMs) return;

  auto& s = powerTest.samples[powerTest.step];
  if (!powerTest.sampling) {
    powerTest.sampling = true;
    s = {};
    s.baseline = readCounts();
    s.startedAt = now;
  }
  uint8_t raw[2];
  if (readPm(kPm5vOut, raw, sizeof(raw))) {
    const unsigned mv = (raw[1] << 8) | raw[0];
    if (!s.mvCount || mv < s.mvMin) s.mvMin = mv;
    if (!s.mvCount || mv > s.mvMax) s.mvMax = mv;
    s.mvSum += mv;
    ++s.mvCount;
  }
  if (now - s.startedAt < kPowerSampleMs) return;
  if (!s.mvCount) {
    finishPowerTest("INVALID_5V_READ");
    return;
  }

  const auto current = readCounts();
  s.elapsed = now - s.startedAt;
  s.delta = {current.edges - s.baseline.edges, current.decoded - s.baseline.decoded,
             current.repeats - s.baseline.repeats, current.unknown - s.baseline.unknown};
  ++powerTest.step;
  powerTest.stageAt = now;
  powerTest.settling = powerTest.sampling = false;
  if (powerTest.step == 3) finishPowerTest("DONE");
  else printPowerTest();
}

void startBacklightTest() {
  if (backlightTest.step || powerTest.active) return;
  const uint8_t brightness = M5.Display.getBrightness();
  if (!receiverActive || !brightness) {
    Serial.println("[ir1] BL ERROR 要先開始接收並打開背光");
    return;
  }
  backlightTest.step = 1;
  backlightTest.originalBrightness = brightness;
  Serial.printf("[ir1] BL START brightness=%u probe=%s\n", static_cast<unsigned>(brightness), edgeProbeStatus());
  backlightTest.baseline = readCounts();
  backlightTest.startedAt = millis();
}

void pollBacklightTest() {
  if (!backlightTest.step) return;
  const uint32_t elapsed = millis() - backlightTest.startedAt;
  if (elapsed < kBacklightSampleMs) return;

  const RxCounts current = readCounts();
  const RxCounts before = backlightTest.baseline;
  Serial.printf("[ir1] BL step=%u brightness=%u ms=%lu edges=%lu count=%lu repeat=%lu unknown=%lu\n",
                static_cast<unsigned>(backlightTest.step), static_cast<unsigned>(M5.Display.getBrightness()),
                static_cast<unsigned long>(elapsed), static_cast<unsigned long>(current.edges - before.edges),
                static_cast<unsigned long>(current.decoded - before.decoded),
                static_cast<unsigned long>(current.repeats - before.repeats),
                static_cast<unsigned long>(current.unknown - before.unknown));

  if (backlightTest.step == 3) {
    backlightTest.step = 0;
    Serial.println("[ir1] BL DONE");
    return;
  }
  ++backlightTest.step;
  M5.Display.setBrightness(backlightTest.step == 2 ? 0 : backlightTest.originalBrightness);
  backlightTest.baseline = readCounts();
  backlightTest.startedAt = millis();
}

void IRAM_ATTR countEdge(void*) {
  rxEdges.fetch_add(1, std::memory_order_relaxed);
}

esp_err_t startEdgeProbe() {
  // 只計算 G42 有幾次跳變；RMT 繼續負責收碼和計時。
  esp_err_t err = gpio_install_isr_service(0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
  err = gpio_set_intr_type(kRxPin, GPIO_INTR_ANYEDGE);
  if (err != ESP_OK) return err;
  return gpio_isr_handler_add(kRxPin, countEdge, nullptr);
}

#ifdef IR_TEST_RMT5
bool IRAM_ATTR receiveDone(rmt_channel_handle_t, const rmt_rx_done_event_data_t* event, void*) {
  rxDone.store(event->num_symbols + 1, std::memory_order_release);
  return false;
}

esp_err_t armReceiver() {
  rmt_receive_config_t config{};
  config.signal_range_min_ns = kMinPulseNs;
  config.signal_range_max_ns = kIdleUs * 1000;
  return rmt_receive(rxChannel, rxItems, sizeof(rxItems), &config);
}

esp_err_t startReceiver() {
  rmt_rx_channel_config_t config{};
  config.gpio_num = kRxPin;
  config.clk_src = RMT_CLK_SRC_DEFAULT;
  config.resolution_hz = kCounterHz;
  config.mem_block_symbols = kRmtMemorySymbols;
  esp_err_t err = rmt_new_rx_channel(&config, &rxChannel);
  if (err != ESP_OK) return err;
  rmt_rx_event_callbacks_t callbacks{};
  callbacks.on_recv_done = receiveDone;
  err = rmt_rx_register_event_callbacks(rxChannel, &callbacks, nullptr);
  if (err == ESP_OK) err = rmt_enable(rxChannel);
  if (err == ESP_OK) {
    err = armReceiver();
    if (err != ESP_OK) rmt_disable(rxChannel);
  }
  if (err != ESP_OK) {
    rmt_del_channel(rxChannel);
    rxChannel = nullptr;
  }
  return err;
}

void releaseReceiver() {
  rmt_disable(rxChannel);
  rmt_del_channel(rxChannel);
  rxChannel = nullptr;
  rxDone.store(0, std::memory_order_relaxed);
}
#else
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

void releaseReceiver() {
  rmt_rx_stop(kRxChannel);
  rmt_driver_uninstall(kRxChannel);
  rxBuffer = nullptr;
}
#endif

void restorePower() {
  if (!powerChanged) return;
  M5.Power.setExtOutput(previousExtPower);
  powerChanged = false;
}

void stopReceiver() {
  if (edgeProbe == ESP_OK) gpio_isr_handler_remove(kRxPin);
  releaseReceiver();
  receiverActive = false;
  if (powerTest.active) finishPowerTest("INVALID_STOPPED");
  restorePower();
  if (backlightTest.step) {
    M5.Display.setBrightness(backlightTest.originalBrightness);
    backlightTest.step = 0;
  }
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
  else {
#ifdef IR_TEST_RMT5
    Serial.printf("[ir1] READY RX=G%d RMT=IDF5 EXT_5V=ON\n", kRxPin);
#else
    Serial.printf("[ir1] READY RX=G%d RMT=%d EXT_5V=ON\n", kRxPin, kRxChannel);
#endif
  }
  bool ampLatch = false;
  const bool ampRead = M5.Power.M5pm1.getGPIOOutputLatch(m5::M5PM1_Class::gpio3, &ampLatch);
  const uint16_t outMv = M5.Power.M5pm1.get5VoutVoltage();
  Serial.printf("[ir1] DIAG ext_mv=%u amp_latch=%s gpio42=%d edges=%lu probe=%s\n",
                static_cast<unsigned>(outMv), ampRead ? (ampLatch ? "1" : "0") : "?",
                gpio_get_level(kRxPin), static_cast<unsigned long>(rxEdges.load(std::memory_order_relaxed)),
                edgeProbeStatus());
  if (decoded) printFrame();
  Serial.printf("[ir1] count=%lu repeat=%lu unknown=%lu\n",
                static_cast<unsigned long>(decoded), static_cast<unsigned long>(repeats),
                static_cast<unsigned long>(unknown));
  printPyg3();
  Serial.printf("[ir1] BOOT id=%08lX uptime_ms=%lu reset=%u\n",
                static_cast<unsigned long>(bootId), millis(), static_cast<unsigned>(esp_reset_reason()));
}

void pollReceiver() {
  if (!receiverActive) return;
#ifdef IR_TEST_RMT5
  const size_t result = rxDone.exchange(0, std::memory_order_acquire);
  if (!result) return;
  const size_t count = result - 1;
  const RxItem* items = rxItems;
#else
  size_t bytes = 0;
  auto* items = static_cast<RxItem*>(xRingbufferReceive(rxBuffer, &bytes, 0));
  if (!items) return;
  if (bytes % sizeof(*items) != 0) {
    vRingbufferReturnItem(rxBuffer, items);
    return;
  }
  const size_t count = bytes / sizeof(*items);
#endif

  nec::Frame frame;
  if (count <= nec::kFullFrameSymbols) {
    nec::Symbol symbols[nec::kFullFrameSymbols];
    for (size_t i = 0; i < count; ++i) {
      symbols[i] = {static_cast<uint16_t>(items[i].duration0), static_cast<uint16_t>(items[i].duration1),
                    items[i].level0 != 0, items[i].level1 != 0};
    }
    frame = nec::decode(symbols, count);
  }
  const RxItem lead = count ? items[0] : RxItem{};
#ifdef IR_TEST_RMT5
  const esp_err_t err = armReceiver();
  if (err != ESP_OK) {
    stopReceiver();
    state = ir_test::State::Error;
    errorText = esp_err_to_name(err);
    printStatus();
    return;
  }
#else
  vRingbufferReturnItem(rxBuffer, items);
#endif

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
  bootId = esp_random();
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
    } else {
      receiverActive = true;
#ifndef IR_TEST_RMT5
      edgeProbe = startEdgeProbe();
#endif
    }
  }
  printStatus();
}

void loop() {
  M5.update();
  if (receiverActive && M5.BtnB.pressedFor(cfg::BTN_B_KILL_MS)) stopReceiver();
  if (M5.BtnA.wasPressed()) {
    battery = M5.Power.getBatteryLevel();
    redraw = true;
    printStatus();
  }
  if (Serial.available()) {
    const int command = Serial.read();
    if (command == '?') printStatus();
    else if (command == 'b') startBacklightTest();
    else if (command == 'p') startPowerTest();
    else if (command == 'r') printPowerTest();
#ifdef IR_TEST_RMT5
    else if (command == 'g' && receiverActive && !powerTest.active && !backlightTest.step) {
      if (edgeProbe == ESP_OK) {
        gpio_isr_handler_remove(kRxPin);
        edgeProbe = ESP_ERR_INVALID_STATE;
      } else {
        edgeProbe = startEdgeProbe();
      }
      printStatus();
    }
#endif
  }
  pollReceiver();
  pollBacklightTest();
  pollPowerTest();
  if (redraw && canvasReady) {
    ir_test::drawScreen(canvas, state, lastFrame, errorText, battery);
    ui_fx::present(&canvas);
    redraw = false;
  }
  vTaskDelay(1);
}
