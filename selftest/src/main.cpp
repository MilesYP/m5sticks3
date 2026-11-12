// ─────────────────────────────────────────────────────────────────────────
//  M5StickS3 硬體自檢 (Stage 2 Self-Test)
//
//  目的：板子到貨後，一次驗證核心硬體沒有出廠故障(DOA)。驗項：
//    LCD / 兩顆按鍵(G11,G12) / BMI270 六軸 IMU / ES8311 喇叭+麥克風 /
//    PSRAM 容量 / M5PM1 電池電壓 / I2C 匯流排掃描(內部 + Grove)
//
//  全程被動，不涉及 RF、不發射任何訊號。RF Pack 的驗收是另外的階段。
//
//  I2C 位址對照：
//    內部匯流排(G47 SDA / G48 SCL)：0x18 ES8311 / 0x68 BMI270 / 0x6E M5PM1
//    Grove 匯流排(G9 SDA / G10 SCL)：0x5F CardKB 或 0x40 Scroll（有插才會出現）
// ─────────────────────────────────────────────────────────────────────────
#include <M5Unified.h>

// ── 期望在「內部 I2C」上看到的裝置（用來判定 PASS/FAIL）──
struct I2cExpect { uint8_t addr; const char* name; };
static const I2cExpect kInternalExpect[] = {
  {0x18, "ES8311"}, {0x68, "BMI270"}, {0x6E, "M5PM1"},
};

// ── 開機自檢的結果，存起來給 loop() 持續顯示 ──
static String g_i2c_internal_line;   // 內部匯流排掃描結果文字
static String g_i2c_grove_line;      // Grove 匯流排掃描結果文字
static bool   g_imu_ok   = false;    // IMU 是否成功初始化
static bool   g_psram_ok = false;    // PSRAM 是否約 8MB
static bool   g_i2c_core_ok = false; // 內部三顆核心晶片是否到齊
static int32_t g_mic_peak = -1;      // 麥克風錄到的峰值振幅(-1=尚未測)
static size_t g_psram_size = 0;

// 全螢幕畫布(sprite)，避免每次重畫閃爍；順便用到一點記憶體當作顯示測試
static M5Canvas g_canvas(&M5.Display);

// ───────────────────────── 工具函式 ─────────────────────────

// 掃描一條 I2C 匯流排，回傳找到的位址清單文字；found[] 回填 128 格布林
static String scanBus(m5::I2C_Class& bus, bool* found) {
  bus.scanID(found);               // M5Unified 內建掃描：逐一探測 0x00~0x7F
  String s;
  for (int addr = 0x08; addr < 0x78; ++addr) {
    if (found[addr]) {
      char buf[8];
      snprintf(buf, sizeof(buf), "%02X ", addr);
      s += buf;
    }
  }
  if (s.length() == 0) s = "(none)";
  return s;
}

// 喇叭測試：嗶一聲（ES8311 輸出路徑）
static void beep(float freq = 2000.0f, uint32_t ms = 150) {
  M5.Speaker.setVolume(128);
  M5.Speaker.tone(freq, ms);
  // tone 為非阻塞，稍等它播完再往下（避免立刻被 Mic 搶走 I2S）
  uint32_t t0 = millis();
  while (M5.Speaker.isPlaying() && millis() - t0 < ms + 100) { delay(1); }
}

// 麥克風測試：切到 Mic 錄一小段，計算峰值振幅，再切回喇叭
static int32_t micPeakTest() {
  static int16_t buf[512];
  M5.Speaker.end();                 // ES8311 同一時間只能喇叭或麥克風，先關喇叭
  if (!M5.Mic.begin()) { M5.Speaker.begin(); return -1; }
  M5.Mic.record(buf, 512, 16000);   // 錄 512 點 @16kHz ≈ 32ms
  while (M5.Mic.isRecording()) { delay(1); }
  int32_t peak = 0;
  for (size_t i = 0; i < 512; ++i) {
    int32_t a = buf[i] < 0 ? -buf[i] : buf[i];
    if (a > peak) peak = a;
  }
  M5.Mic.end();
  M5.Speaker.begin();               // 切回喇叭，讓 loop 還能嗶
  return peak;
}

// ───────────────────────── setup ─────────────────────────
void setup() {
  auto cfg = M5.config();
  cfg.internal_imu = true;
  cfg.internal_spk = true;
  cfg.internal_mic = true;
  M5.begin(cfg);

  // Grove(EXT_5V) 供電：M5Unified 預設關閉，打開才能驗 CardKB/Scroll
  M5.Power.setExtOutput(true);

  M5.Display.setRotation(0);
  g_canvas.createSprite(M5.Display.width(), M5.Display.height());
  g_canvas.setTextSize(1);

  Serial.begin(115200);
  delay(300);
  Serial.println("\n===== M5StickS3 Self-Test =====");

  // ── 晶片 / 記憶體資訊 ──
  g_psram_size = ESP.getPsramSize();
  g_psram_ok   = (g_psram_size > 4 * 1024 * 1024);   // >4MB 視為 8MB PSRAM 正常掛載
  Serial.printf("Chip   : %s rev%d, %d core\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
  Serial.printf("Flash  : %u bytes (%.1f MB)\n",
                ESP.getFlashChipSize(), ESP.getFlashChipSize() / 1048576.0);
  Serial.printf("PSRAM  : %u bytes (%.1f MB)  free=%u  -> %s\n",
                (unsigned)g_psram_size, g_psram_size / 1048576.0,
                (unsigned)ESP.getFreePsram(), g_psram_ok ? "OK" : "FAIL");

  // ── I2C 掃描（內部 + Grove）──
  static bool foundIn[128];
  static bool foundEx[128];
  g_i2c_internal_line = scanBus(M5.In_I2C, foundIn);
  g_i2c_grove_line    = scanBus(M5.Ex_I2C, foundEx);
  Serial.printf("I2C in : %s\n", g_i2c_internal_line.c_str());
  Serial.printf("I2C ex : %s\n", g_i2c_grove_line.c_str());

  g_i2c_core_ok = true;
  for (auto& e : kInternalExpect) {
    bool present = foundIn[e.addr];
    Serial.printf("   %-7s @0x%02X : %s\n", e.name, e.addr, present ? "OK" : "MISSING");
    if (!present) g_i2c_core_ok = false;
  }

  // ── IMU 檢查 ──
  float ax, ay, az;
  g_imu_ok = M5.Imu.getAccel(&ax, &ay, &az);
  Serial.printf("IMU    : %s", g_imu_ok ? "OK" : "FAIL");
  if (g_imu_ok) Serial.printf("  accel=(% .2f,% .2f,% .2f)g", ax, ay, az);
  Serial.println();

  // ── 電源 / 電池 ──
  Serial.printf("Batt   : %d mV, level=%d%%\n",
                M5.Power.getBatteryVoltage(), (int)M5.Power.getBatteryLevel());

  // ── 喇叭嗶一聲 → 麥克風錄音峰值 ──
  Serial.println("Speaker: beep...");
  beep(2000, 150);
  delay(120);
  g_mic_peak = micPeakTest();
  Serial.printf("Mic    : peak=%ld  -> %s\n",
                (long)g_mic_peak, g_mic_peak >= 0 ? "captured" : "FAIL");

  Serial.println("===== setup done, see screen =====");
}

// ───────────────────────── loop ─────────────────────────
void loop() {
  M5.update();   // 更新按鍵狀態（必呼叫）

  // BtnA(G11) 按下 → 嗶一聲：同時驗「按鍵 + 喇叭」
  if (M5.BtnA.wasPressed()) beep(2600, 120);
  // BtnB(G12) 按下 → 重新量一次麥克風峰值（對它說話可看到數字跳動）
  if (M5.BtnB.wasPressed()) g_mic_peak = micPeakTest();

  // ── 每 ~120ms 重畫一次畫面 ──
  static uint32_t last = 0;
  if (millis() - last < 120) return;
  last = millis();

  float ax = 0, ay = 0, az = 0;
  bool imu_now = M5.Imu.getAccel(&ax, &ay, &az);

  auto& c = g_canvas;
  c.fillScreen(TFT_BLACK);
  c.setTextColor(TFT_CYAN, TFT_BLACK);
  c.setCursor(2, 2);
  c.println("StickS3 SELF-TEST");
  c.setTextColor(TFT_WHITE, TFT_BLACK);

  c.setTextColor(g_psram_ok ? TFT_GREEN : TFT_RED, TFT_BLACK);
  c.printf("PSRAM %.1fMB %s\n", g_psram_size / 1048576.0, g_psram_ok ? "OK" : "X");

  c.setTextColor(g_i2c_core_ok ? TFT_GREEN : TFT_RED, TFT_BLACK);
  c.printf("I2C core %s\n", g_i2c_core_ok ? "OK" : "MISS");
  c.setTextColor(TFT_WHITE, TFT_BLACK);
  c.printf("in :%s\n", g_i2c_internal_line.c_str());
  c.printf("gv :%s\n", g_i2c_grove_line.c_str());

  c.setTextColor(imu_now ? TFT_GREEN : TFT_RED, TFT_BLACK);
  c.printf("IMU %s\n", imu_now ? "OK" : "X");
  c.setTextColor(TFT_WHITE, TFT_BLACK);
  c.printf(" a% .2f% .2f% .2f\n", ax, ay, az);

  c.printf("Batt %dmV %d%%\n",
           M5.Power.getBatteryVoltage(), (int)M5.Power.getBatteryLevel());
  c.printf("Mic peak %ld\n", (long)g_mic_peak);

  c.setTextColor(TFT_YELLOW, TFT_BLACK);
  c.printf("A%s B%s\n",
           M5.BtnA.isPressed() ? "[#]" : "[ ]",
           M5.BtnB.isPressed() ? "[#]" : "[ ]");
  c.setTextColor(0x7BEF, TFT_BLACK);   // 灰
  c.println("A=beep B=mic");

  c.pushSprite(0, 0);
}
