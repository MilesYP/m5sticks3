#include "diag_cmd.h"

#include <Arduino.h>
#include <stdlib.h>
#include <ctype.h>

#include "audio_io.h"
#include "battery_ui.h"
#include "imu_pet.h"
#include "input_hal.h"
#include "net_hub.h"
#include "voice_turn.h"

namespace {

char s_line[16];
size_t s_n = 0;

void printConfig() {
  Serial.printf("[diag] 目前：%s pga=0x%02X 對話=%s\n", net_hub::configLabel(), audio_io::pga(),
                voice_turn::gptMode() ? "gpt" : "echo");
}

void printHelp() {
  Serial.println("[diag] 指令：w0/w1 Wi-Fi關/開｜s0/s1 省電關/開(手動)｜sa 省電自動｜p20 p15 p11 p8 p5 p2 發射功率｜g17 PGA(16進位)｜f0/f1/f2/f3 下載PCM/ADPCM/μ-law/ADPCM8k｜u0/u1 上傳PCM/μ-law｜x 斷開中控連線(測重送)｜r0/r1 中控全速/真實速度｜b300 預緩衝ms｜i20 ping間隔秒(i0不ping)｜k 送 ping｜e 重開 Grove 供電並重掃｜m1/m0 對話 GPT/echo｜j IMU 量測開/關｜v 待機聲音量測開/關｜c50 假裝電量%(c 恢復)｜? 目前設定");
}

void run(char* cmd) {
  for (char* c = cmd; *c; ++c) *c = (char)tolower((unsigned char)*c);
  switch (cmd[0]) {
    case 'w':
      if (cmd[1] == '0' || cmd[1] == '1') net_hub::setWifiEnabled(cmd[1] == '1');
      else printHelp();
      break;
    case 's':
      if (cmd[1] == '0' || cmd[1] == '1') net_hub::setPowerSave(cmd[1] == '1');
      else if (cmd[1] == 'a') net_hub::setPowerSaveAuto();
      else printHelp();
      break;
    case 'p':
      if (isdigit((unsigned char)cmd[1])) net_hub::setTxPowerDbm(atoi(cmd + 1));
      else printHelp();
      break;
    case 'g':
      if (isxdigit((unsigned char)cmd[1])) audio_io::setPga((uint8_t)strtol(cmd + 1, nullptr, 16));
      else printHelp();
      break;
    case 'f':
      if (cmd[1] >= '0' && cmd[1] <= '3') voice_turn::setDownFormat(cmd[1] - '0');
      else printHelp();
      break;
    case 'u':   // B3b-5：上傳格式
      if (cmd[1] == '0' || cmd[1] == '1') voice_turn::setUpFormat(cmd[1] - '0');
      else printHelp();
      break;
    case 'm':   // B3c：對話模式 m1＝GPT-Live（預設）、m0＝echo 原樣回傳（回歸測試）
      if (cmd[1] == '0' || cmd[1] == '1') voice_turn::setGptMode(cmd[1] == '1');
      else printHelp();
      break;
    case 'x':   // B3b-5：只斷 WebSocket（模擬連線重置），測錄音中斷線重送
      net_hub::dropLink();
      break;
    case 'r':   // B3b-4：中控送法
      if (cmd[1] == '0' || cmd[1] == '1') voice_turn::setPaceRealtime(cmd[1] == '1');
      else printHelp();
      break;
    case 'b':   // B3b-4：預緩衝（ms）
      if (isdigit((unsigned char)cmd[1])) audio_io::setPrebufferMs((uint32_t)atoi(cmd + 1));
      else printHelp();
      break;
    case 'i':   // B3b-4：ping 間隔（秒），量連線重置用
      if (isdigit((unsigned char)cmd[1])) net_hub::setPingInterval((uint32_t)atoi(cmd + 1));
      else printHelp();
      break;
    case 'k':
      net_hub::pingNow();
      break;
    case 'e':   // 11-19 Grove 診斷：重開 5V＋L3B、印電源狀態、重掃 I2C
      input_hal::rescan();
      break;
    case 'j':   // A3：IMU 量測（j 切換、j1 開、j0 關）
      imu_pet::setMeasureImu(cmd[1] == '1' ? true : cmd[1] == '0' ? false : !imu_pet::measureImu());
      break;
    case 'v':   // A3：待機聲音量測（v 切換、v1 開、v0 關）
      imu_pet::setMeasureSound(cmd[1] == '1' ? true : cmd[1] == '0' ? false : !imu_pet::measureSound());
      break;
    case 'c':   // A3：假裝電量（驗收休息臉各色）；c 單獨＝恢復真實讀數
      battery_ui::setOverride(isdigit((unsigned char)cmd[1]) ? atoi(cmd + 1) : -1);
      break;
    case '?':
      printConfig();
      break;
    default: {
      // 印出收到的內容（含 16 進位），開機第一個指令失效時可以看到混進了什麼
      Serial.printf("[diag] 不認得的指令「%s」（hex:", cmd);
      for (const char* c = cmd; *c; ++c) Serial.printf(" %02X", (unsigned char)*c);
      Serial.println("）");
      printHelp();
      break;
    }
  }
}

}  // namespace

namespace diag_cmd {

void begin() { printHelp(); }

void loop() {
  while (Serial.available() > 0) {
    const int ch = Serial.read();
    if (ch == '\r' || ch == '\n') {
      if (s_n > 0) {
        s_line[s_n] = '\0';
        run(s_line);
        s_n = 0;
      }
    } else if (ch < 0x21 || ch > 0x7E) {
      // B3b-3：開機後第一個指令常失效——序列埠剛連上時會混進看不見的字元。
      // 指令只用可見 ASCII，其他一律略過（空白也略過）。
      continue;
    } else if (s_n < sizeof(s_line) - 1) {
      s_line[s_n++] = (char)ch;
    }
  }
}

}  // namespace diag_cmd
