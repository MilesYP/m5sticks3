// ─────────────────────────────────────────────────────────────────────────
//  M5StickS3 + RF Pack S3 — 階段四 RF 探針 v5（用 SmartRC 函式庫驗收）
//
//  為什麼改用函式庫（v1~v4 的結論）：
//    自己手寫的 CC1101 SPI 時序在這塊板子上會出現「每隔一次交易出錯」或晶片不回應，
//    結果隨寫法飄動，無法用來判斷硬體好壞。Bruce 使用的 SmartRC-CC1101-Driver-Lib
//    （bmorcelli 分支）已在階段三證實能驅動這塊 RF Pack，而專案規範（CLAUDE.md 第 3 節）
//    本來就指定正式韌體用 ELECHOUSE/SmartRC 驅動 CC1101。
//    → 驗收標準改為：「我們的工具鏈 + 我們的腳位 + 正式韌體要用的同一個函式庫」能正確讀寫。
//
//  驗收項目：
//    (1) Init()：函式庫初始化成功（含晶片重置與寫入預設設定）
//    (2) 身分：PARTNUM=0x00、VERSION=0x14（或舊批次 0x04）
//    (3) 寫入→讀回：4 個暫存器 × 4 種位元圖樣 = 16 次全部一致
//    (4) 穩定度：寫入已知值後連續讀 100 輪 × 4 個暫存器 = 400 次，錯誤 0 次
//    (5) nRF24：RF_CH 寫入→讀回→還原（對照組）
//
//  全程不發射：CC1101 只讀寫設定暫存器，不呼叫 SetTx / SendData；nRF24 的 CE 保持 LOW。
//
//  腳位（Bruce boards/m5stack-sticks3，已實機確認）：
//    共用 SPI：SCK=G5  MISO=G4  MOSI=G6；CC1101 CS=G2 GDO0=G3；nRF24 CS=G8 CE=G1；SD CS=G7
//  SPI 分配：M5GFX 螢幕用 SPI3_HOST，RF 用 SPI2（FSPI），交給函式庫使用（同 Bruce 做法）。
//
//  操作：開機自動測一輪；按正面藍鍵 A 重測並重印序列埠報告。
//  註：測試碼，用 delay() 阻塞可接受；正式韌體會改非阻塞。
// ─────────────────────────────────────────────────────────────────────────
#include <M5Unified.h>
#include <SPI.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>

namespace rfpin {
constexpr int kSck    = 5;
constexpr int kMiso   = 4;
constexpr int kMosi   = 6;
constexpr int kCcCs   = 2;
constexpr int kCcGdo0 = 3;
constexpr int kNrfCs  = 8;
constexpr int kNrfCe  = 1;
constexpr int kSdCs   = 7;
}  // namespace rfpin

namespace cc {
constexpr uint8_t kStPartnum = 0x30;
constexpr uint8_t kStVersion = 0x31;
}  // namespace cc

// 寫入→讀回：全 8 位元可讀寫、不影響發射的暫存器（SYNC1/SYNC0/PKTLEN/ADDR）
static const uint8_t kRwRegs[]     = {0x04, 0x05, 0x06, 0x09};
static const uint8_t kRwPatterns[] = {0x55, 0xAA, 0x00, 0xFF};
constexpr size_t kRwRegCount = sizeof(kRwRegs);
constexpr size_t kRwTotal    = sizeof(kRwRegs) * sizeof(kRwPatterns);
static const uint8_t kStressValues[kRwRegCount] = {0x5A, 0xA5, 0x3C, 0xC3};  // 穩定度測試用的已知值
constexpr uint32_t kStressRounds = 100;
constexpr uint32_t kStressTotal  = kStressRounds * kRwRegCount;

namespace nrf {
constexpr uint8_t kCmdRead  = 0x00;
constexpr uint8_t kCmdWrite = 0x20;
constexpr uint8_t kNop      = 0xFF;
constexpr uint8_t kRegSetupAw = 0x03;
constexpr uint8_t kRegRfCh    = 0x05;
}  // namespace nrf

static SPIClass g_rfSpi(FSPI);
static const SPISettings kNrfSpi(1000000, MSBFIRST, SPI_MODE0);

struct Report {
  bool     initOk = false;
  bool     found = false;
  uint8_t  partnum = 0xFF, version = 0xFF;
  uint8_t  rwPass = 0;
  uint8_t  rwErrReg = 0, rwErrWrote = 0, rwErrRead = 0;
  uint32_t stressErrors = 0;
  uint32_t stressMs = 0;
  uint8_t  nrfChTest = 0, nrfChBack = 0, nrfChRestored = 0;
  bool     nrfOk = false;

  bool idOk() const { return found && partnum == 0x00 && (version == 0x14 || version == 0x04); }
  bool rwOk() const { return rwPass == kRwTotal; }
  bool stressOk() const { return stressErrors == 0; }
  bool allOk() const { return initOk && idOk() && rwOk() && stressOk() && nrfOk; }
};

static uint32_t g_runCount = 0;
static bool     g_libInitDone = false;
static bool     g_libInitOk = false;
static M5Canvas g_canvas(&M5.Display);

static void deselectAll() {
  digitalWrite(rfpin::kCcCs, HIGH);
  digitalWrite(rfpin::kNrfCs, HIGH);
  digitalWrite(rfpin::kSdCs, HIGH);
}

// SmartRC 註明 Init() 整個程式只能呼叫一次，所以只在第一次量測時初始化
static bool initCc1101Once() {
  if (g_libInitDone) return g_libInitOk;
  ELECHOUSE_cc1101.setSPIinstance(&g_rfSpi);  // 用我們指定的 SPI2（同 Bruce 的 acquireSPIBus 做法）
  ELECHOUSE_cc1101.setBeginEndLogic(false);
  ELECHOUSE_cc1101.setSpiPin(rfpin::kSck, rfpin::kMiso, rfpin::kMosi, rfpin::kCcCs);
  ELECHOUSE_cc1101.setGDO0(rfpin::kCcGdo0);
  g_libInitOk = ELECHOUSE_cc1101.Init();
  g_libInitDone = true;
  return g_libInitOk;
}

// ───────────────────────── nRF24（對照組，維持原寫法；v1~v4 皆 PASS）─────────────────────────
static uint8_t nrfReadReg(uint8_t reg) {
  g_rfSpi.beginTransaction(kNrfSpi);
  digitalWrite(rfpin::kNrfCs, LOW);
  g_rfSpi.transfer(nrf::kCmdRead | (reg & 0x1F));
  const uint8_t v = g_rfSpi.transfer(nrf::kNop);
  digitalWrite(rfpin::kNrfCs, HIGH);
  g_rfSpi.endTransaction();
  return v;
}

static void nrfWriteReg(uint8_t reg, uint8_t value) {
  g_rfSpi.beginTransaction(kNrfSpi);
  digitalWrite(rfpin::kNrfCs, LOW);
  g_rfSpi.transfer(nrf::kCmdWrite | (reg & 0x1F));
  g_rfSpi.transfer(value);
  digitalWrite(rfpin::kNrfCs, HIGH);
  g_rfSpi.endTransaction();
}

// ───────────────────────── 驗收流程 ─────────────────────────
static Report runProbe() {
  Report r;
  deselectAll();

  // (1)(2) 初始化與身分
  r.initOk  = initCc1101Once();
  r.found   = ELECHOUSE_cc1101.getCC1101();
  r.partnum = ELECHOUSE_cc1101.SpiReadStatus(cc::kStPartnum);
  r.version = ELECHOUSE_cc1101.SpiReadStatus(cc::kStVersion);

  // 先記下 4 個測試暫存器的原值，測完還原
  uint8_t saved[kRwRegCount];
  for (size_t i = 0; i < kRwRegCount; ++i) saved[i] = ELECHOUSE_cc1101.SpiReadReg(kRwRegs[i]);

  // (3) 寫入→讀回
  bool recorded = false;
  for (uint8_t reg : kRwRegs) {
    for (uint8_t pattern : kRwPatterns) {
      ELECHOUSE_cc1101.SpiWriteReg(reg, pattern);
      const uint8_t back = ELECHOUSE_cc1101.SpiReadReg(reg);
      if (back == pattern) {
        ++r.rwPass;
      } else if (!recorded) {
        r.rwErrReg = reg; r.rwErrWrote = pattern; r.rwErrRead = back;
        recorded = true;
      }
    }
  }

  // (4) 穩定度：寫入已知值，連續讀 100 輪
  for (size_t i = 0; i < kRwRegCount; ++i) ELECHOUSE_cc1101.SpiWriteReg(kRwRegs[i], kStressValues[i]);
  const uint32_t t0 = millis();
  for (uint32_t round = 0; round < kStressRounds; ++round) {
    for (size_t i = 0; i < kRwRegCount; ++i) {
      if (ELECHOUSE_cc1101.SpiReadReg(kRwRegs[i]) != kStressValues[i]) ++r.stressErrors;
    }
  }
  r.stressMs = millis() - t0;

  for (size_t i = 0; i < kRwRegCount; ++i) ELECHOUSE_cc1101.SpiWriteReg(kRwRegs[i], saved[i]);  // 還原

  // (5) nRF24
  deselectAll();
  digitalWrite(rfpin::kNrfCe, LOW);
  const uint8_t orig = nrfReadReg(nrf::kRegRfCh) & 0x7F;
  r.nrfChTest = (orig ^ 0x2A) & 0x7F;
  if (r.nrfChTest == orig) r.nrfChTest = 0x4C;
  nrfWriteReg(nrf::kRegRfCh, r.nrfChTest);
  r.nrfChBack = nrfReadReg(nrf::kRegRfCh);
  nrfWriteReg(nrf::kRegRfCh, orig);
  r.nrfChRestored = nrfReadReg(nrf::kRegRfCh);
  r.nrfOk = (r.nrfChBack == r.nrfChTest) && (r.nrfChRestored == orig);

  return r;
}

static const char* pf(bool ok) { return ok ? "PASS" : "FAIL"; }

static void printReport(const Report& r) {
  Serial.printf("\n===== RF Probe v5 (SmartRC lib) run #%lu =====\n", (unsigned long)g_runCount);
  Serial.printf("(1) Init       : %s\n", pf(r.initOk));
  Serial.printf("(2) ID         : found=%c PARTNUM=0x%02X VERSION=0x%02X (TI 0x00 / 0x14)  -> %s\n",
                r.found ? 'Y' : 'N', r.partnum, r.version, pf(r.idOk()));
  Serial.printf("(3) Write/Read : %u/%u correct", r.rwPass, (unsigned)kRwTotal);
  if (!r.rwOk()) {
    Serial.printf("  first err reg 0x%02X wrote 0x%02X read 0x%02X", r.rwErrReg, r.rwErrWrote, r.rwErrRead);
  }
  Serial.printf("  -> %s\n", pf(r.rwOk()));
  Serial.printf("(4) Stability  : %lu errors in %lu reads (%lu ms)  -> %s\n",
                (unsigned long)r.stressErrors, (unsigned long)kStressTotal,
                (unsigned long)r.stressMs, pf(r.stressOk()));
  Serial.printf("(5) nRF24      : RF_CH write %02X read %02X restored %02X  -> %s\n",
                r.nrfChTest, r.nrfChBack, r.nrfChRestored, pf(r.nrfOk));
  Serial.printf("OVERALL        : %s\n", r.allOk() ? "PASS - Stage 4 complete" : "FAIL");
  Serial.println("===== press A to rerun =====");
}

static void drawLine(const char* label, bool ok, const char* detail) {
  auto& c = g_canvas;
  c.setTextColor(ok ? TFT_GREEN : TFT_RED, TFT_BLACK);
  c.printf("%-6s %s\n", label, ok ? "PASS" : "FAIL");
  c.setTextColor(TFT_WHITE, TFT_BLACK);
  c.printf(" %s\n", detail);
}

static void drawScreen(const Report& r) {
  auto& c = g_canvas;
  char buf[32];
  c.fillScreen(TFT_BLACK);
  c.setCursor(2, 2);
  c.setTextColor(TFT_CYAN, TFT_BLACK);
  c.printf("RF PROBE v5 #%lu\n\n", (unsigned long)g_runCount);

  drawLine("INIT", r.initOk, "SmartRC lib");
  snprintf(buf, sizeof(buf), "PN%02X VER%02X", r.partnum, r.version);
  drawLine("ID", r.idOk(), buf);
  snprintf(buf, sizeof(buf), "%u/%u write/read", r.rwPass, (unsigned)kRwTotal);
  drawLine("R/W", r.rwOk(), buf);
  snprintf(buf, sizeof(buf), "%lu err / %lu", (unsigned long)r.stressErrors, (unsigned long)kStressTotal);
  drawLine("STRESS", r.stressOk(), buf);
  snprintf(buf, sizeof(buf), "CH %02X>%02X>%02X", r.nrfChTest, r.nrfChBack, r.nrfChRestored);
  drawLine("nRF24", r.nrfOk, buf);

  c.setTextColor(r.allOk() ? TFT_GREEN : TFT_RED, TFT_BLACK);
  c.printf("\n%s\n", r.allOk() ? "STAGE 4 PASS" : "STAGE 4 FAIL");
  c.setTextColor(0x7BEF, TFT_BLACK);
  c.println("\nA = rerun");
  c.pushSprite(0, 0);
}

static void drawBusy() {
  auto& c = g_canvas;
  c.fillScreen(TFT_BLACK);
  c.setCursor(2, 2);
  c.setTextColor(TFT_CYAN, TFT_BLACK);
  c.println("RF PROBE v5");
  c.setTextColor(TFT_WHITE, TFT_BLACK);
  c.println("\nprobing...");
  c.pushSprite(0, 0);
}

static void runAll() {
  drawBusy();
  ++g_runCount;
  const Report r = runProbe();
  printReport(r);
  drawScreen(r);
}

// ───────────────────────── setup / loop ─────────────────────────
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Power.setExtOutput(false);

  M5.Display.setRotation(0);
  g_canvas.createSprite(M5.Display.width(), M5.Display.height());
  g_canvas.setTextSize(1);

  Serial.begin(115200);

  pinMode(rfpin::kCcCs, OUTPUT);
  pinMode(rfpin::kNrfCs, OUTPUT);
  pinMode(rfpin::kSdCs, OUTPUT);
  deselectAll();
  pinMode(rfpin::kNrfCe, OUTPUT);
  digitalWrite(rfpin::kNrfCe, LOW);

  g_rfSpi.begin(rfpin::kSck, rfpin::kMiso, rfpin::kMosi, -1);

  delay(300);
  runAll();
}

void loop() {
  M5.update();
  if (M5.BtnA.wasPressed()) runAll();
  delay(10);
}
