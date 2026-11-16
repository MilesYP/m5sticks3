// ─────────────────────────────────────────────────────────────────────────
//  M5StickS3 + RF Pack S3 — CC1101 接收驗收 v6.5（RSSI 測試＋頻率掃描＋SPI 可靠性測試，有自動中止）
//
//  目的：用自有韌體獨立確認 CC1101 的「接收路徑」是否正常。
//    v5 只驗過 SPI 讀寫；Bruce 上看到的「RSSI 恆定約 -50 dBm、拔天線不變」
//    PINGEQUA 解釋為 Bruce 的 AGC 設定（AGCCTRL2=0xC7）所致。
//
//  全程只接收：只下 SIDLE／SCAL／SRX，不呼叫 SetTx／SendData。
//
//  兩個模式（A 按住 1 秒切換）：
//    MONITOR：固定 433.92 MHz、頻寬 256 kHz，看即時 RSSI 與 10 秒擷取。
//    SCAN   ：頻寬 812 kHz、每 0.75 MHz 一點掃過 300–348／387–464／779–928 MHz，
//             每點保留看過的最大值（max hold），用來找出遙控器實際的發射頻率。
//
//  AGC 設定（MONITOR 下 B 短按切換；SCAN 沿用當下那一組）：
//    1 CHIP DEFAULT  : AGCCTRL2/1/0 = 03 40 91（CC1101 重置後的出廠值）
//    2 LIB 0xC7      : C7 00 B2（SmartRC Init() 寫入的值，也是 Bruce 一般模式的值）
//    3 BRUCE OOK C7  : C7 01 40（Bruce 固定頻率 OOK 預設組）
//
//  按鍵：
//    MONITOR  A 短按＝10 秒擷取      B 短按＝切 AGC      B 按住 1 秒＝SPI 可靠性測試
//    SCAN     A 短按＝清除 max hold  B 短按＝切頻段
//    兩者     A 按住 1 秒＝切換 MONITOR／SCAN
//
//  v6.1 相對 v6：
//    - MARCSTATE 連讀確認，單次讀錯不再算成「掉出接收」（另計 badread）
//    - 開機與每 10 秒印一次 [regs]；進 RX 前做一次手動校正（SCAL）並印 FSCAL3/2/1
//    - VER 與 RXdrop 分開上色
//    - 新增 SCAN 模式
//
//  v6.2 相對 v6.1（為了查「晶片間歇性不回應」）：
//    - 任何一次 SPI 操作超過 500 ms 就記為一次 hang（驅動等 MISO 逾時是 1000 ms），畫面與序列埠顯示次數
//    - 取消每 10 秒自動印 [regs]（v6.1 的卡住都緊接在這 20 多筆連續讀取之後），
//      改為只在開機、切設定、擷取結束時印；[regs] 內每筆讀取間隔 300 µs，FREQ2 連讀三次
//
//  v6.3／v6.4 相對 v6.2：
//    - MONITOR 下「B 按住 1 秒」改為執行 SPI 可靠性測試（原本的 EXT 5V 切換移除）。
//      v6.3 版的測試先寫入再讀回，但寫入本身沒生效（讀回的 D3 是原廠預設值），測試無效。
//      v6.4 改為讀唯讀且值已知的 VERSION，並比較 IDLE 與 RX 兩種晶片狀態。
//
//  腳位與 SPI 分配同 v5：SCK=G5 MISO=G4 MOSI=G6；CC1101 CS=G2 GDO0=G3；
//    nRF24 CS=G8 CE=G1；SD CS=G7。RF 用 SPI2（FSPI），螢幕佔 SPI3。
//  規則：Init() 整個程式只呼叫一次；切設定只改暫存器與下 strobe，不重置晶片。
//  註：測試碼，少量 delay() 可接受；正式韌體會改非阻塞。
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
constexpr uint8_t kStPartnum   = 0x30;  // 狀態暫存器
constexpr uint8_t kStVersion   = 0x31;
constexpr uint8_t kStRssi      = 0x34;
constexpr uint8_t kStMarcstate = 0x35;
constexpr uint8_t kStPktstatus = 0x38;
constexpr uint8_t kMarcRx      = 0x0D;  // 接收中
constexpr uint8_t kFscal1Unlocked = 0x3F;
constexpr float   kFreqMhz     = 433.92f;
constexpr float   kRxBwKhz     = 256.0f;
constexpr float   kScanBwKhz   = 812.0f;
}  // namespace cc

struct AgcProfile {
  const char* name;
  uint8_t agc2, agc1, agc0;
};
static const AgcProfile kProfiles[] = {
    {"1 CHIP DEFAULT", 0x03, 0x40, 0x91},
    {"2 LIB 0xC7",     0xC7, 0x00, 0xB2},
    {"3 BRUCE OOK C7", 0xC7, 0x01, 0x40},
};
constexpr size_t kProfileCount = sizeof(kProfiles) / sizeof(kProfiles[0]);

// ── 掃描頻段（CC1101 可用的三段）──
struct Range { float lo, hi; };
static const Range kRanges[] = {{300.0f, 348.0f}, {387.0f, 464.0f}, {779.0f, 928.0f}};
constexpr size_t kRangeCount = sizeof(kRanges) / sizeof(kRanges[0]);
static const char* const kBandNames[] = {"ALL", "300-348", "387-464", "779-928"};
constexpr size_t kBandCount = kRangeCount + 1;  // 0 = 全部
constexpr float  kScanStepMhz = 0.75f;
constexpr size_t kMaxScanPoints = 400;
constexpr uint32_t kScanSettleMs = 3;   // 換頻後等校正與 RSSI 穩定
constexpr uint32_t kScanDwellMs  = 12;  // 每點停留取樣
constexpr size_t   kTopPeaks     = 3;
constexpr float    kPeakSeparationMhz = 3.0f;

// ── 時間常數 ──
constexpr uint32_t kSampleIntervalMs = 2;      // RSSI 取樣間隔
constexpr uint32_t kBucketMs         = 100;    // 一格歷史 = 100 ms
constexpr size_t   kBucketCount      = 135;    // 135 格 = 螢幕寬度 = 13.5 秒
constexpr size_t   kWindowBuckets    = 50;     // 統計視窗 5 秒
constexpr uint32_t kDrawIntervalMs   = 100;
constexpr uint32_t kHealthIntervalMs = 1000;   // 每秒檢查 MARCSTATE 並印一行
constexpr uint32_t kHangMs           = 500;    // 單次 SPI 操作超過這個時間視為卡住
constexpr uint32_t kCaptureMs        = 10000;  // A 鍵擷取長度
constexpr uint32_t kHoldMs           = 1000;   // 長按門檻
constexpr float    kGraphTopDbm      = -20.0f;
constexpr float    kGraphBottomDbm   = -115.0f;
constexpr float    kNoValue          = -200.0f;

struct Bucket {
  float    mn = 0, mx = 0, sum = 0;
  uint32_t n = 0;
  void clear() { mn = mx = sum = 0; n = 0; }
  void add(float v) {
    if (n == 0) { mn = mx = v; } else { if (v < mn) mn = v; if (v > mx) mx = v; }
    sum += v; ++n;
  }
};

struct Result {
  uint8_t  profile = 0;
  bool     ext5v = false;
  float    mn = 0, avg = 0, mx = 0;
  uint32_t n = 0;
};
constexpr size_t kMaxResults = 16;

enum class Mode { Monitor, Scan };

static SPIClass g_rfSpi(FSPI);
static M5Canvas g_canvas(&M5.Display);

static Mode     g_mode = Mode::Monitor;
static bool     g_initOk = false;
static bool     g_found = false;
static uint8_t  g_partnum = 0xFF, g_version = 0xFF;
static size_t   g_profile = 0;
static bool     g_ext5v = false;
static uint8_t  g_marc = 0xFF;
static uint8_t  g_fscal3 = 0xFF, g_fscal2 = 0xFF, g_fscal1 = 0xFF;
static uint32_t g_rxDrops = 0;    // 連讀確認後仍不在 RX 的次數
static uint32_t g_badReads = 0;   // MARCSTATE 單次讀值不一致的次數
static uint32_t g_hangs = 0;      // SPI 操作超過 kHangMs 的次數（晶片沒回應）
static float    g_rssiNow = kNoValue;

static Bucket   g_buckets[kBucketCount];
static size_t   g_head = 0;           // 目前正在累積的那一格
static uint32_t g_bucketStartMs = 0;

static bool     g_capturing = false;
static uint32_t g_captureStartMs = 0;
static Bucket   g_capture;
static Result   g_results[kMaxResults];
static size_t   g_resultCount = 0;

// 掃描狀態
static size_t   g_band = 0;
static float    g_ptFreq[kMaxScanPoints];
static float    g_ptMax[kMaxScanPoints];
static size_t   g_ptCount = 0;
static size_t   g_ptIndex = 0;
static uint32_t g_sweepCount = 0;
static size_t   g_peakIdx[kTopPeaks];
static size_t   g_peakCount = 0;

static uint32_t g_lastSampleMs = 0, g_lastDrawMs = 0, g_lastHealthMs = 0;
static uint32_t g_btnADownMs = 0, g_btnBDownMs = 0;

static void deselectAll() {
  digitalWrite(rfpin::kCcCs, HIGH);
  digitalWrite(rfpin::kNrfCs, HIGH);
  digitalWrite(rfpin::kSdCs, HIGH);
}

// 記錄卡住：t0 是操作開始時間。回傳 true 表示這次操作卡住了（讀到的值不可信）
static bool noteHang(uint32_t t0, const char* what) {
  const uint32_t took = millis() - t0;
  if (took < kHangMs) return false;
  ++g_hangs;
  Serial.printf("[hang #%lu] at %lu ms, %s took %lu ms\n", (unsigned long)g_hangs,
                (unsigned long)t0, what, (unsigned long)took);
  return true;
}

// ───────────────────────── CC1101 ─────────────────────────
static uint8_t readMarcstateRaw() {
  return ELECHOUSE_cc1101.SpiReadStatus(cc::kStMarcstate) & 0x1F;
}

// 狀態暫存器在晶片更新的瞬間可能讀到錯值：連讀到兩次相同才採用（最多 4 次）
static uint8_t readMarcstate() {
  uint8_t prev = readMarcstateRaw();
  for (int i = 0; i < 3; ++i) {
    const uint8_t cur = readMarcstateRaw();
    if (cur == prev) return cur;
    ++g_badReads;
    prev = cur;
  }
  return prev;
}

static float readRssiDbm() {
  uint8_t prev = ELECHOUSE_cc1101.SpiReadStatus(cc::kStRssi);
  for (int i = 0; i < 3; ++i) {
    const uint8_t cur = ELECHOUSE_cc1101.SpiReadStatus(cc::kStRssi);
    if (cur == prev) break;
    prev = cur;
  }
  const int raw = (prev >= 128) ? (int)prev - 256 : (int)prev;
  return raw / 2.0f - 74.0f;  // CC1101 資料手冊的換算式（RSSI offset 74）
}

static void clearStats() {
  for (auto& b : g_buckets) b.clear();
  g_head = 0;
  g_bucketStartMs = millis();
  g_capturing = false;
}

// 手動校正頻率合成器並記下結果（診斷用）：SIDLE → SCAL → 等回到 IDLE → 讀 FSCAL3/2/1
static void calibrateAndRecord() {
  ELECHOUSE_cc1101.setSidle();
  delay(1);
  ELECHOUSE_cc1101.SpiStrobe(CC1101_SCAL);
  delay(3);
  g_fscal3 = ELECHOUSE_cc1101.SpiReadReg(CC1101_FSCAL3);
  g_fscal2 = ELECHOUSE_cc1101.SpiReadReg(CC1101_FSCAL2);
  g_fscal1 = ELECHOUSE_cc1101.SpiReadReg(CC1101_FSCAL1);
}

// 進接收：SIDLE → SRX（MCSM0=0x18，從 IDLE 進 RX 會再自動校正一次），不重置晶片
static void enterRx() {
  ELECHOUSE_cc1101.SetRx();
  delay(5);
  g_marc = readMarcstate();
}

static void writeAgc() {
  const AgcProfile& p = kProfiles[g_profile];
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_AGCCTRL2, p.agc2);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_AGCCTRL1, p.agc1);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_AGCCTRL0, p.agc0);
}

// 逐筆讀取、每筆間隔 300 µs（不連續猛讀），FREQ2 連讀三次看是否一致
static uint8_t readRegSpaced(uint8_t addr) {
  delayMicroseconds(300);
  return ELECHOUSE_cc1101.SpiReadReg(addr);
}

static void printRegs() {
  const uint32_t t0 = millis();
  const uint8_t f2a = readRegSpaced(CC1101_FREQ2), f2b = readRegSpaced(CC1101_FREQ2),
                f2c = readRegSpaced(CC1101_FREQ2);
  const uint8_t f1 = readRegSpaced(CC1101_FREQ1), f0 = readRegSpaced(CC1101_FREQ0);
  const uint8_t fsctrl0 = readRegSpaced(CC1101_FSCTRL0);
  const uint8_t mdm4 = readRegSpaced(CC1101_MDMCFG4), mdm2 = readRegSpaced(CC1101_MDMCFG2);
  const uint8_t pkt0 = readRegSpaced(CC1101_PKTCTRL0), iocfg0 = readRegSpaced(CC1101_IOCFG0);
  const uint8_t mcsm1 = readRegSpaced(CC1101_MCSM1), mcsm0 = readRegSpaced(CC1101_MCSM0);
  const uint8_t frend1 = readRegSpaced(CC1101_FREND1);
  const uint8_t agc2 = readRegSpaced(CC1101_AGCCTRL2), agc1 = readRegSpaced(CC1101_AGCCTRL1),
                agc0 = readRegSpaced(CC1101_AGCCTRL0);
  const uint8_t fs3 = readRegSpaced(CC1101_FSCAL3), fs2 = readRegSpaced(CC1101_FSCAL2),
                fs1 = readRegSpaced(CC1101_FSCAL1), fs0 = readRegSpaced(CC1101_FSCAL0);
  const uint8_t test0 = readRegSpaced(CC1101_TEST0);
  delayMicroseconds(300);
  const uint8_t marc = readMarcstate();
  delayMicroseconds(300);
  const uint8_t pktstatus = ELECHOUSE_cc1101.SpiReadStatus(cc::kStPktstatus);
  noteHang(t0, "printRegs");
  Serial.printf("[regs] PARTNUM=%02X VERSION=%02X FREQ2(x3)=%02X %02X %02X FREQ1/0=%02X %02X FSCTRL0=%02X "
                "MDMCFG4=%02X MDMCFG2=%02X PKTCTRL0=%02X IOCFG0=%02X MCSM1=%02X MCSM0=%02X FREND1=%02X "
                "AGCCTRL2/1/0=%02X %02X %02X FSCAL3/2/1/0=%02X %02X %02X %02X TEST0=%02X | "
                "cal FSCAL3/2/1=%02X %02X %02X | MARC=%02X PKTSTATUS=%02X rxdrop=%lu badread=%lu hang=%lu\n",
                g_partnum, g_version, f2a, f2b, f2c, f1, f0, fsctrl0, mdm4, mdm2, pkt0, iocfg0, mcsm1,
                mcsm0, frend1, agc2, agc1, agc0, fs3, fs2, fs1, fs0, test0, g_fscal3, g_fscal2,
                g_fscal1, marc, pktstatus, (unsigned long)g_rxDrops, (unsigned long)g_badReads,
                (unsigned long)g_hangs);
}

// 回到 MONITOR 的無線設定：433.92 MHz、256 kHz、目前的 AGC
static void setupMonitor() {
  ELECHOUSE_cc1101.setSidle();  // 改暫存器前先回 IDLE
  ELECHOUSE_cc1101.setRxBW(cc::kRxBwKhz);
  ELECHOUSE_cc1101.setMHZ(cc::kFreqMhz);
  writeAgc();
  calibrateAndRecord();
  enterRx();
  clearStats();
}

static void applyProfile(size_t index) {
  g_profile = index % kProfileCount;
  setupMonitor();
}

static void initRadio() {
  ELECHOUSE_cc1101.setSPIinstance(&g_rfSpi);
  ELECHOUSE_cc1101.setBeginEndLogic(false);
  ELECHOUSE_cc1101.setSpiPin(rfpin::kSck, rfpin::kMiso, rfpin::kMosi, rfpin::kCcCs);
  ELECHOUSE_cc1101.setGDO0(rfpin::kCcGdo0);
  g_initOk = ELECHOUSE_cc1101.Init();  // 整個程式只呼叫這一次

  // 偵測晶片：讀到 0 不立刻判死，延遲後重試
  for (int attempt = 0; attempt < 5; ++attempt) {
    g_partnum = ELECHOUSE_cc1101.SpiReadStatus(cc::kStPartnum);
    g_version = ELECHOUSE_cc1101.SpiReadStatus(cc::kStVersion);
    g_found = (g_version != 0x00 && g_version != 0xFF);
    if (g_found) break;
    delay(20);
  }

  pinMode(rfpin::kCcGdo0, INPUT);            // 非同步模式下 GDO0 由晶片輸出資料，ESP32 端只當輸入
  ELECHOUSE_cc1101.setModulation(2);         // ASK/OOK
  ELECHOUSE_cc1101.setPktFormat(3);          // 非同步序列模式：沒有封包概念，會一直留在 RX
  applyProfile(0);
}

// ───────────────────────── MONITOR 統計 ─────────────────────────
static void advanceBuckets(uint32_t now) {
  // 用有號差值：clearStats() 可能把起點設得比本輪的 now 晚幾毫秒
  while ((int32_t)(now - g_bucketStartMs) >= (int32_t)kBucketMs) {
    g_bucketStartMs += kBucketMs;
    g_head = (g_head + 1) % kBucketCount;
    g_buckets[g_head].clear();
  }
}

static Bucket windowStats() {
  Bucket w;
  for (size_t i = 0; i < kWindowBuckets; ++i) {
    const Bucket& b = g_buckets[(g_head + kBucketCount - i) % kBucketCount];
    if (b.n == 0) continue;
    if (w.n == 0) { w.mn = b.mn; w.mx = b.mx; }
    else { if (b.mn < w.mn) w.mn = b.mn; if (b.mx > w.mx) w.mx = b.mx; }
    w.sum += b.sum; w.n += b.n;
  }
  return w;
}

static void printResults() {
  Serial.println("\n===== v6 capture results (10 s each, dBm) =====");
  Serial.println(" #  AGC profile      5V    min     avg     max   samples");
  for (size_t i = 0; i < g_resultCount; ++i) {
    const Result& r = g_results[i];
    Serial.printf("%2u  %-15s  %-3s %6.1f  %6.1f  %6.1f  %lu\n", (unsigned)(i + 1),
                  kProfiles[r.profile].name, r.ext5v ? "on" : "off", r.mn, r.avg, r.mx,
                  (unsigned long)r.n);
  }
  Serial.println("================================================");
}

static void finishCapture() {
  g_capturing = false;
  if (g_capture.n == 0) return;
  if (g_resultCount == kMaxResults) {  // 滿了就丟掉最舊的
    for (size_t i = 1; i < kMaxResults; ++i) g_results[i - 1] = g_results[i];
    --g_resultCount;
  }
  Result& r = g_results[g_resultCount++];
  r.profile = (uint8_t)g_profile;
  r.ext5v = g_ext5v;
  r.mn = g_capture.mn; r.mx = g_capture.mx; r.avg = g_capture.sum / g_capture.n; r.n = g_capture.n;
  printRegs();
  printResults();
}

// ───────────────────────── SCAN ─────────────────────────
static void clearScanHold() {
  for (size_t i = 0; i < g_ptCount; ++i) g_ptMax[i] = kNoValue;
  g_ptIndex = 0;
  g_sweepCount = 0;
  g_peakCount = 0;
}

static void buildScanPoints() {
  g_ptCount = 0;
  for (size_t r = 0; r < kRangeCount; ++r) {
    if (g_band != 0 && g_band != r + 1) continue;
    for (float f = kRanges[r].lo; f <= kRanges[r].hi + 0.001f && g_ptCount < kMaxScanPoints;
         f += kScanStepMhz) {
      g_ptFreq[g_ptCount++] = f;
    }
  }
  clearScanHold();
}

// 找出 max hold 最高的幾個頻率（彼此至少相隔 kPeakSeparationMhz）
static void findPeaks() {
  g_peakCount = 0;
  for (size_t k = 0; k < kTopPeaks; ++k) {
    int best = -1;
    for (size_t i = 0; i < g_ptCount; ++i) {
      if (g_ptMax[i] <= kNoValue) continue;
      bool tooClose = false;
      for (size_t j = 0; j < g_peakCount; ++j) {
        float d = g_ptFreq[i] - g_ptFreq[g_peakIdx[j]];
        if (d < 0) d = -d;
        if (d < kPeakSeparationMhz) tooClose = true;
      }
      if (tooClose) continue;
      if (best < 0 || g_ptMax[i] > g_ptMax[best]) best = (int)i;
    }
    if (best < 0) break;
    g_peakIdx[g_peakCount++] = (size_t)best;
  }
}

static void printSweep() {
  float floorSum = 0; size_t n = 0;
  for (size_t i = 0; i < g_ptCount; ++i) if (g_ptMax[i] > kNoValue) { floorSum += g_ptMax[i]; ++n; }
  Serial.printf("[scan #%lu] band %s, %s, mean of max-hold %.1f dBm, peaks:",
                (unsigned long)g_sweepCount, kBandNames[g_band], kProfiles[g_profile].name,
                n ? floorSum / n : 0.0f);
  for (size_t j = 0; j < g_peakCount; ++j) {
    Serial.printf("  %.2f MHz %.1f dBm", g_ptFreq[g_peakIdx[j]], g_ptMax[g_peakIdx[j]]);
  }
  Serial.printf("  | hang=%lu\n", (unsigned long)g_hangs);
}

// 掃一個點（約 15 ms，阻塞）：換頻 → 進 RX → 等穩定 → 取樣保留最大值
static void scanStep() {
  if (g_ptCount == 0) return;
  const uint32_t stepStart = millis();
  ELECHOUSE_cc1101.setSidle();
  ELECHOUSE_cc1101.setMHZ(g_ptFreq[g_ptIndex]);
  ELECHOUSE_cc1101.SetRx();
  delay(kScanSettleMs);
  float mx = kNoValue;
  const uint32_t t0 = millis();
  while (millis() - t0 < kScanDwellMs) {
    const float v = readRssiDbm();
    if (v > mx) mx = v;
  }
  g_rssiNow = mx;
  const bool hung = noteHang(stepStart, "scan step");
  if (!hung && mx > g_ptMax[g_ptIndex]) g_ptMax[g_ptIndex] = mx;  // 卡住時的讀值不採用
  if (++g_ptIndex >= g_ptCount) {
    g_ptIndex = 0;
    ++g_sweepCount;
    findPeaks();
    printSweep();
  }
}

static void enterScan() {
  g_mode = Mode::Scan;
  g_capturing = false;
  ELECHOUSE_cc1101.setSidle();
  ELECHOUSE_cc1101.setRxBW(cc::kScanBwKhz);
  writeAgc();
  buildScanPoints();
  Serial.printf("[mode] SCAN band %s, %u points, step %.2f MHz, BW %.0f kHz\n", kBandNames[g_band],
                (unsigned)g_ptCount, kScanStepMhz, cc::kScanBwKhz);
}

static void enterMonitor() {
  g_mode = Mode::Monitor;
  setupMonitor();
  Serial.println("[mode] MONITOR 433.92 MHz");
  printRegs();
}

// ───────────────────────── 畫面 ─────────────────────────
static int dbmToY(float dbm, int top, int height) {
  if (dbm > kGraphTopDbm) dbm = kGraphTopDbm;
  if (dbm < kGraphBottomDbm) dbm = kGraphBottomDbm;
  const float t = (kGraphTopDbm - dbm) / (kGraphTopDbm - kGraphBottomDbm);
  return top + (int)(t * (height - 1));
}

static void drawGrid(int top, int height) {
  auto& c = g_canvas;
  const int w = c.width();
  c.drawRect(0, top - 1, w, height + 2, 0x4208);
  for (float grid : {-50.0f, -90.0f}) {
    const int gy = dbmToY(grid, top, height);
    for (int x = 0; x < w; x += 4) c.drawPixel(x, gy, 0x4208);
    c.setCursor(2, gy - 8);
    c.setTextColor(0x7BEF, TFT_BLACK);
    c.printf("%.0f", grid);
  }
}

static void drawMonitor(uint32_t now) {
  auto& c = g_canvas;
  const AgcProfile& p = kProfiles[g_profile];
  const int w = c.width();
  c.fillScreen(TFT_BLACK);
  c.setTextSize(1);

  c.setCursor(2, 2);
  c.setTextColor(TFT_CYAN, TFT_BLACK);
  c.printf("RSSI v6.5 %.2fMHz", cc::kFreqMhz);

  c.setCursor(2, 13);
  c.setTextColor(TFT_YELLOW, TFT_BLACK);
  c.printf("%s", p.name);

  c.setCursor(2, 24);
  c.setTextColor(TFT_WHITE, TFT_BLACK);
  c.printf("AGC %02X %02X %02X  5V:%s", p.agc2, p.agc1, p.agc0, g_ext5v ? "on" : "off");

  const bool marcOk = (g_marc == cc::kMarcRx);
  const bool pllOk = (g_fscal1 != cc::kFscal1Unlocked);
  c.setCursor(2, 35);
  c.setTextColor(marcOk ? TFT_GREEN : TFT_RED, TFT_BLACK);
  c.printf("MARC %02X ", g_marc);
  c.setTextColor(pllOk ? TFT_GREEN : TFT_RED, TFT_BLACK);
  c.printf("FSCAL1 %02X", g_fscal1);

  c.setCursor(2, 46);
  const bool chipOk = g_initOk && g_found && g_version == 0x14;
  c.setTextColor(chipOk ? TFT_GREEN : TFT_RED, TFT_BLACK);
  c.printf("VER %02X ", g_version);
  c.setTextColor(g_rxDrops == 0 ? TFT_GREEN : TFT_RED, TFT_BLACK);
  c.printf("d%lu ", (unsigned long)g_rxDrops);
  c.setTextColor(g_badReads == 0 ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
  c.printf("b%lu ", (unsigned long)g_badReads);
  c.setTextColor(g_hangs == 0 ? TFT_GREEN : TFT_RED, TFT_BLACK);
  c.printf("HANG%lu", (unsigned long)g_hangs);

  // 即時 RSSI（大字）
  c.setTextSize(3);
  c.setCursor(2, 60);
  c.setTextColor(TFT_WHITE, TFT_BLACK);
  c.printf("%4.0f", g_rssiNow);
  c.setTextSize(1);
  c.setCursor(80, 76);
  c.print("dBm now");

  // 5 秒統計
  const Bucket ws = windowStats();
  c.setCursor(2, 90);
  c.setTextColor(0x7BEF, TFT_BLACK);
  c.print("5s  min / avg / max");
  c.setCursor(2, 101);
  c.setTextColor(TFT_WHITE, TFT_BLACK);
  if (ws.n) c.printf("%5.0f /%5.0f /%5.0f", ws.mn, ws.sum / ws.n, ws.mx);

  // 歷史圖：每一直行 = 100 ms，畫該格的 min～max；最右邊是最新
  constexpr int kGraphTop = 114, kGraphH = 62;
  drawGrid(kGraphTop, kGraphH);
  for (size_t i = 0; i < kBucketCount && (int)i < w; ++i) {
    const Bucket& b = g_buckets[(g_head + kBucketCount - i) % kBucketCount];
    if (b.n == 0) continue;
    const int x = w - 1 - (int)i;
    const int yTop = dbmToY(b.mx, kGraphTop, kGraphH);
    const int yBot = dbmToY(b.mn, kGraphTop, kGraphH);
    c.drawFastVLine(x, yTop, yBot - yTop + 1, TFT_GREEN);
  }

  // 擷取狀態／最近一次結果
  c.setCursor(2, 182);
  if (g_capturing) {
    const uint32_t elapsed = now - g_captureStartMs;
    const uint32_t left = elapsed < kCaptureMs ? (kCaptureMs - elapsed + 999) / 1000 : 0;
    c.setTextColor(TFT_ORANGE, TFT_BLACK);
    c.printf("CAPTURING... %lus", (unsigned long)left);
  } else if (g_resultCount) {
    const Result& r = g_results[g_resultCount - 1];
    c.setTextColor(TFT_CYAN, TFT_BLACK);
    c.printf("#%u P%u 5V:%s 10s", (unsigned)g_resultCount, (unsigned)(r.profile + 1),
             r.ext5v ? "on" : "off");
    c.setCursor(2, 193);
    c.setTextColor(TFT_WHITE, TFT_BLACK);
    c.printf("%5.0f /%5.0f /%5.0f", r.mn, r.avg, r.mx);
  } else {
    c.setTextColor(0x7BEF, TFT_BLACK);
    c.print("no capture yet");
  }

  c.setTextColor(0x7BEF, TFT_BLACK);
  c.setCursor(2, 210);
  c.print("A=capture holdA=SCAN");
  c.setCursor(2, 220);
  c.print("B=AGC  holdB=SPItest");
  c.setCursor(2, 230);
  c.print("RX only, no TX");
  c.pushSprite(0, 0);
}

static void drawScan() {
  auto& c = g_canvas;
  const int w = c.width();
  c.fillScreen(TFT_BLACK);
  c.setTextSize(1);

  c.setCursor(2, 2);
  c.setTextColor(TFT_CYAN, TFT_BLACK);
  c.print("SCAN v6.5 max hold");

  c.setCursor(2, 13);
  c.setTextColor(TFT_YELLOW, TFT_BLACK);
  c.printf("band %s MHz", kBandNames[g_band]);

  c.setCursor(2, 24);
  c.setTextColor(TFT_WHITE, TFT_BLACK);
  c.printf("%s", kProfiles[g_profile].name);

  c.setCursor(2, 35);
  if (g_ptCount) c.printf("sw%lu %.1fMHz ", (unsigned long)g_sweepCount, g_ptFreq[g_ptIndex]);
  c.setTextColor(g_hangs == 0 ? TFT_GREEN : TFT_RED, TFT_BLACK);
  c.printf("HANG%lu", (unsigned long)g_hangs);

  // 頻譜圖：橫軸 = 掃描點（左低右高），直線高度 = 該頻率看過的最大 RSSI
  constexpr int kGraphTop = 50, kGraphH = 90;
  drawGrid(kGraphTop, kGraphH);
  const int yBase = kGraphTop + kGraphH - 1;
  for (int x = 0; x < w && g_ptCount; ++x) {
    const size_t i0 = (size_t)x * g_ptCount / (size_t)w;
    size_t i1 = (size_t)(x + 1) * g_ptCount / (size_t)w;
    if (i1 <= i0) i1 = i0 + 1;
    float mx = kNoValue;
    for (size_t i = i0; i < i1 && i < g_ptCount; ++i) if (g_ptMax[i] > mx) mx = g_ptMax[i];
    if (mx <= kNoValue) continue;
    const int y = dbmToY(mx, kGraphTop, kGraphH);
    c.drawFastVLine(x, y, yBase - y + 1, TFT_GREEN);
  }
  if (g_ptCount) {  // 目前掃到的位置
    const int cx = (int)(g_ptIndex * (size_t)w / g_ptCount);
    c.drawFastVLine(cx, kGraphTop, 3, TFT_ORANGE);
  }

  c.setCursor(2, 146);
  c.setTextColor(0x7BEF, TFT_BLACK);
  c.print("top peaks (MHz dBm)");
  for (size_t j = 0; j < g_peakCount; ++j) {
    c.setCursor(2, 157 + (int)j * 11);
    c.setTextColor(TFT_WHITE, TFT_BLACK);
    c.printf("%7.2f  %6.1f", g_ptFreq[g_peakIdx[j]], g_ptMax[g_peakIdx[j]]);
  }

  c.setTextColor(0x7BEF, TFT_BLACK);
  c.setCursor(2, 200);
  c.print("A=clear hold");
  c.setCursor(2, 210);
  c.print("B=band");
  c.setCursor(2, 220);
  c.print("holdA=MONITOR");
  c.setCursor(2, 230);
  c.print("RX only, no TX");
  c.pushSprite(0, 0);
}

// ───────────────────────── SPI 可靠性測試 ─────────────────────────
//  v6.4 的結果（2026-11-17）：
//    IDLE 下健康（單筆讀取 200 次只錯 1 次、連讀確認 200/200 正確、寫入 1～2 次就成功）；
//    RX 下前 48 筆還正常，進入 200 筆密集讀取後晶片就完全停止回應，每筆讀取都卡滿
//    checkMISO 的 1 秒逾時且不會自行恢復 → 測試會跑掉 20 分鐘以上。
//  v6.5 因此改為：
//    - 每種間隔只讀 50 次，並掃過 0～2000 µs 找出 RX 下「不會打死晶片」的最小間隔
//    - 任一列累積 3 次逾時就中止該列；全程累積 10 次或超過 60 秒就中止整個測試
//    - RX 被打死後，測試會下 SIDLE 再讀 10 筆，確認「回到 IDLE 是否就能恢復」（不重置晶片）
namespace spitest {
constexpr uint8_t kVersion   = 0x31;  // 狀態暫存器，唯讀，應為 0x14
constexpr uint8_t kVerValue  = 0x14;
constexpr uint8_t kSync1     = 0x04;  // 非同步模式用不到；重置預設 0xD3
constexpr uint8_t kSync1Def  = 0xD3;
constexpr uint8_t kSync1Test = 0xA5;
constexpr int     kReads     = 50;    // 每種間隔的讀取次數
constexpr int     kPattern   = 24;    // 直接印出前幾筆原始讀值，看交替規律
constexpr int     kTries     = 6;     // 連讀確認最多試幾次
constexpr int     kRowHangs  = 3;     // 一列累積幾次逾時就中止該列
constexpr int     kMaxHangs  = 10;    // 全程累積幾次逾時就中止整個測試
constexpr uint32_t kBudgetMs = 60000; // 整個測試的時間上限
static const uint32_t kGapsUs[] = {0, 50, 200, 500, 1000, 2000};
}  // namespace spitest

static uint32_t g_testStartMs = 0;
static uint32_t g_testHangs = 0;
static bool     g_testAbort = false;

static uint8_t readOnce(uint8_t addr, bool status) {
  return status ? ELECHOUSE_cc1101.SpiReadStatus(addr) : ELECHOUSE_cc1101.SpiReadReg(addr);
}

// 讀一筆並判斷是否卡住（卡住 = 耗時超過 kHangMs，代表 checkMISO 等到逾時）
static uint8_t readTimed(uint8_t addr, bool status, bool &hung) {
  const uint32_t t0 = millis();
  const uint8_t v = readOnce(addr, status);
  hung = (millis() - t0) >= kHangMs;
  if (hung) { ++g_testHangs; ++g_hangs; }
  if (g_testHangs >= (uint32_t)spitest::kMaxHangs ||
      millis() - g_testStartMs >= spitest::kBudgetMs) {
    g_testAbort = true;
  }
  return v;
}

// 連讀到兩次相同才採用。回傳 false = 試完都沒有兩筆一致
static bool consensusRead(uint8_t addr, bool status, uint8_t &out) {
  bool hung = false;
  uint8_t prev = readTimed(addr, status, hung);
  for (int i = 1; i < spitest::kTries && !g_testAbort; ++i) {
    const uint8_t cur = readTimed(addr, status, hung);
    if (cur == prev) { out = cur; return true; }
    prev = cur;
  }
  out = prev;
  return false;
}

static void printPattern(const char *label, uint8_t addr, bool status) {
  Serial.printf("  %-8s raw x%d:", label, spitest::kPattern);
  bool hung = false;
  for (int i = 0; i < spitest::kPattern && !g_testAbort; ++i) {
    Serial.printf(" %02X", readTimed(addr, status, hung));
  }
  Serial.println("");
}

// 掃過各種間隔，每種讀 kReads 次；累積 kRowHangs 次逾時就中止該列
static void printGapSweep() {
  for (uint32_t gap : spitest::kGapsUs) {
    if (g_testAbort) break;
    int err = 0, done = 0, rowHangs = 0, wc = 0;
    uint8_t wrong[3] = {0, 0, 0};
    for (int i = 0; i < spitest::kReads; ++i) {
      if (gap) delayMicroseconds(gap);
      bool hung = false;
      const uint8_t v = readTimed(spitest::kVersion, true, hung);
      ++done;
      if (hung) ++rowHangs;
      if (v != spitest::kVerValue) {
        ++err;
        if (wc < 3) wrong[wc++] = v;
      }
      if (rowHangs >= spitest::kRowHangs || g_testAbort) break;
    }
    Serial.printf("  gap %4lu us: err %2d/%2d, hang %d", (unsigned long)gap, err, done, rowHangs);
    if (wc) { Serial.print("  wrong:"); for (int i = 0; i < wc; ++i) Serial.printf(" %02X", wrong[i]); }
    if (rowHangs >= spitest::kRowHangs) Serial.print("  <- row aborted");
    Serial.println("");
  }
}

static void printConsensusStats() {
  int noAgree = 0, agreeWrong = 0, done = 0;
  for (int i = 0; i < spitest::kReads && !g_testAbort; ++i) {
    uint8_t v = 0;
    ++done;
    if (!consensusRead(spitest::kVersion, true, v)) ++noAgree;
    else if (v != spitest::kVerValue) ++agreeWrong;
  }
  Serial.printf("  consensus: no-agree %d/%d, agreed-but-wrong %d\n", noAgree, done, agreeWrong);
}

// 寫入後用連讀確認驗證，最多重試 kTries 次，回報第幾次才成功
static void printWriteTest(uint8_t addr, uint8_t value) {
  if (g_testAbort) return;
  int attempt = 0;
  uint8_t back = 0;
  for (; attempt < spitest::kTries && !g_testAbort; ++attempt) {
    ELECHOUSE_cc1101.SpiWriteReg(addr, value);
    delayMicroseconds(200);
    consensusRead(addr, false, back);
    if (back == value) break;
  }
  if (back == value) Serial.printf("  write 0x%02X=%02X ok after %d attempt(s)\n", addr, value,
                                   attempt + 1);
  else Serial.printf("  write 0x%02X=%02X FAILED, reads back %02X\n", addr, value, back);
}

// 晶片被打死後，試試「下 SIDLE 回 IDLE」能不能恢復，不重置晶片
static void printIdleRecovery() {
  ELECHOUSE_cc1101.setSidle();
  delay(5);
  int ok = 0, hangs = 0;
  Serial.print("  recovery via SIDLE, 10 raw reads:");
  for (int i = 0; i < 10; ++i) {
    const uint32_t t0 = millis();
    const uint8_t v = readOnce(spitest::kVersion, true);
    if (millis() - t0 >= kHangMs) ++hangs;
    if (v == spitest::kVerValue) ++ok;
    Serial.printf(" %02X", v);
  }
  Serial.printf("  -> %d/10 correct, %d hang\n", ok, hangs);
}

static void runSpiTest() {
  auto &c = g_canvas;
  c.fillScreen(TFT_BLACK);
  c.setTextSize(1);
  c.setCursor(2, 2);
  c.setTextColor(TFT_CYAN, TFT_BLACK);
  c.print("SPI TEST v6.5");
  c.setCursor(2, 20);
  c.setTextColor(TFT_WHITE, TFT_BLACK);
  c.print("max 60s, auto-abort");
  c.setCursor(2, 32);
  c.print("see serial monitor");
  c.pushSprite(0, 0);

  g_testStartMs = millis();
  g_testHangs = 0;
  g_testAbort = false;

  Serial.printf("\n===== SPI reliability test (v6.5): VERSION should be %02X, SYNC1 default %02X, "
                "%d reads per row, auto-abort =====\n", spitest::kVerValue, spitest::kSync1Def,
                spitest::kReads);
  for (int state = 0; state < 2 && !g_testAbort; ++state) {
    if (state == 0) {
      ELECHOUSE_cc1101.setSidle();
      delay(2);
      Serial.println("[state] IDLE");
    } else {
      ELECHOUSE_cc1101.SetRx();
      delay(5);
      Serial.println("[state] RX");
    }
    printPattern("VERSION", spitest::kVersion, true);
    printPattern("SYNC1", spitest::kSync1, false);
    printGapSweep();
    printConsensusStats();
    if (state == 0) {  // 寫入測試只在 IDLE 做（RX 下寫設定暫存器本來就不保證生效）
      printWriteTest(spitest::kSync1, spitest::kSync1Test);
      printWriteTest(spitest::kSync1, spitest::kSync1Def);  // 還原
    }
  }
  if (g_testAbort) Serial.printf("*** aborted: hangs=%lu, elapsed=%lu ms ***\n",
                                 (unsigned long)g_testHangs,
                                 (unsigned long)(millis() - g_testStartMs));
  printIdleRecovery();
  Serial.printf("===== end, test hangs=%lu, total hangs=%lu, elapsed=%lu ms =====\n",
                (unsigned long)g_testHangs, (unsigned long)g_hangs,
                (unsigned long)(millis() - g_testStartMs));
  enterRx();
  clearStats();
}

// ───────────────────────── setup / loop ─────────────────────────
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Power.setExtOutput(false);

  M5.Display.setRotation(0);
  g_canvas.createSprite(M5.Display.width(), M5.Display.height());

  Serial.begin(115200);

  pinMode(rfpin::kCcCs, OUTPUT);
  pinMode(rfpin::kNrfCs, OUTPUT);
  pinMode(rfpin::kSdCs, OUTPUT);
  deselectAll();
  pinMode(rfpin::kNrfCe, OUTPUT);
  digitalWrite(rfpin::kNrfCe, LOW);  // nRF24 保持待命，不收不發

  g_rfSpi.begin(rfpin::kSck, rfpin::kMiso, rfpin::kMosi, -1);
  delay(300);

  initRadio();
  printRegs();
}

static void handleButtons(uint32_t now) {
  if (M5.BtnA.wasPressed()) g_btnADownMs = now;
  if (M5.BtnA.wasReleased()) {
    const bool held = (now - g_btnADownMs >= kHoldMs);
    if (held) {
      if (g_mode == Mode::Monitor) enterScan(); else enterMonitor();
    } else if (g_mode == Mode::Scan) {
      clearScanHold();
      Serial.println("[scan] max hold cleared");
    } else if (!g_capturing) {
      g_capture.clear();
      g_captureStartMs = now;
      g_capturing = true;
      Serial.printf("[capture] start: %s, 5V %s\n", kProfiles[g_profile].name, g_ext5v ? "on" : "off");
    }
  }

  if (M5.BtnB.wasPressed()) g_btnBDownMs = now;
  if (M5.BtnB.wasReleased()) {
    const bool held = (now - g_btnBDownMs >= kHoldMs);
    if (g_mode == Mode::Scan) {
      if (!held) {
        g_band = (g_band + 1) % kBandCount;
        buildScanPoints();
        Serial.printf("[scan] band %s, %u points\n", kBandNames[g_band], (unsigned)g_ptCount);
      }
    } else if (held) {
      runSpiTest();
    } else {
      applyProfile(g_profile + 1);
      Serial.printf("[profile] %s\n", kProfiles[g_profile].name);
      printRegs();
    }
  }
}

void loop() {
  M5.update();
  uint32_t now = millis();
  handleButtons(now);
  now = millis();

  if (g_mode == Mode::Scan) {
    scanStep();
    now = millis();
    if (now - g_lastDrawMs >= kDrawIntervalMs) {
      g_lastDrawMs = now;
      drawScan();
    }
    return;
  }

  // ── MONITOR：取樣 ──
  if (now - g_lastSampleMs >= kSampleIntervalMs) {
    g_lastSampleMs = now;
    advanceBuckets(now);
    const float v = readRssiDbm();
    const bool hung = noteHang(now, "rssi read");
    if (!hung) {  // 卡住時的讀值不採用
      g_rssiNow = v;
      g_buckets[g_head].add(v);
    }
    if (g_capturing) {
      if (!hung) g_capture.add(v);
      if ((int32_t)(now - g_captureStartMs) >= (int32_t)kCaptureMs) finishCapture();
    }
  }

  // ── 每秒健康檢查：連讀確認不在 RX 才記一次並重新進 RX（不重置晶片）──
  if (now - g_lastHealthMs >= kHealthIntervalMs) {
    g_lastHealthMs = now;
    const uint32_t healthStart = millis();
    g_marc = readMarcstate();
    noteHang(healthStart, "marcstate read");
    if (g_marc != cc::kMarcRx) {
      ++g_rxDrops;
      Serial.printf("[rxdrop] MARC=%02X -> re-enter RX\n", g_marc);
      enterRx();
    }
    const Bucket ws = windowStats();
    Serial.printf("[%lus] %s 5V:%s MARC=%02X now=%.1f  5s min/avg/max=%.1f/%.1f/%.1f  rxdrop=%lu badread=%lu hang=%lu\n",
                  (unsigned long)(now / 1000), kProfiles[g_profile].name, g_ext5v ? "on" : "off",
                  g_marc, g_rssiNow, ws.mn, ws.n ? ws.sum / ws.n : 0.0f, ws.mx,
                  (unsigned long)g_rxDrops, (unsigned long)g_badReads, (unsigned long)g_hangs);
  }

  // ── 畫面 ──
  if (now - g_lastDrawMs >= kDrawIntervalMs) {
    g_lastDrawMs = now;
    drawMonitor(now);
  }
}
