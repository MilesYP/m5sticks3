#include "diag_cmd.h"

#include <Arduino.h>
#include <stdlib.h>
#include <ctype.h>

#include "audio_io.h"
#include "battery_ui.h"
#include "imu_pet.h"
#include "input_hal.h"
#include "mode_manager.h"
#include "net_hub.h"
#include "ui_expression.h"
#include "ui_fx.h"
#include "ui_info.h"
#include "power_save.h"
#include "ui_persona.h"
#include "voice_turn.h"

namespace {

char s_line[16];
size_t s_n = 0;

void printConfig() {
  Serial.printf("[diag] 目前：%s pga=0x%02X 對話=%s\n", net_hub::configLabel(), audio_io::pga(),
                voice_turn::gptMode() ? "gpt" : "echo");
}

void printHelp() {
  Serial.println("[diag] 指令：a 中控重送資訊頁資料｜w0/w1 Wi-Fi關/開｜s0/s1 省電關/開(手動)｜sa 省電自動｜p20 p15 p11 p8 p5 p2 發射功率｜g17 PGA(16進位)｜f0/f1/f2/f3 下載PCM/ADPCM/μ-law/ADPCM8k｜u0/u1 上傳PCM/μ-law｜x 斷開中控連線(測重送)｜r0/r1 中控全速/真實速度｜b300 預緩衝ms｜i20 ping間隔秒(i0不ping)｜k 送 ping｜e 重開 Grove 供電並重掃｜m1/m0 對話 GPT/echo｜j IMU 量測開/關｜v 待機聲音量測開/關｜c50 假裝電量%(c 恢復)｜h 充電紀錄(h0 清空)｜o0/o1/o2 假裝直式/橫式A左/橫式A右(o 恢復自動)｜y0～y8 預覽表情(y 結束)｜n 切換表情頁/資訊頁(n0 表情 n1 資訊)｜t 換頁交叉淡化開/關｜q0/q1 合成波/EVA（表情＋資訊頁）｜z 省電狀態(z0 關 z1 開)｜l110 正常背光0～255｜#文字 在 log 做記號｜? 目前設定");
}

void run(char* cmd) {
  if (cmd[0] == '#') {   // A4 量測：在 log 裡做記號（例：#1 直拿），方便事後分段
    Serial.printf("[mark] %s ===== 開機後 %lu ms\n", cmd + 1, (unsigned long)millis());
    return;
  }
  for (char* c = cmd; *c; ++c) *c = (char)tolower((unsigned char)*c);
  switch (cmd[0]) {
    case 'a':   // A5-2：要中控立即重送資訊頁雲端資料（天氣、快訊、美金額度）並強制更新
      net_hub::requestInfo();
      break;
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
    case 'o':   // A4：假裝拿法（o0 直式、o1 橫式 A 左、o2 橫式 A 右；o 單獨＝恢復自動）
      imu_pet::setOrientOverride((cmd[1] >= '0' && cmd[1] <= '2') ? cmd[1] - '0' : -1);
      break;
    case 'y':   // A4：預覽表情（y0～y8；y 單獨＝結束），只改畫面、不影響互動邏輯
      if (cmd[1] >= '0' && cmd[1] <= '8') {
        ui_expression::setPreview(cmd[1] - '0');
        Serial.printf("[ui] 預覽表情 %c：%s（y 結束）\n", cmd[1], ui_expression::moodNameOf(static_cast<Mood>(cmd[1] - '0')));
      } else {
        ui_expression::setPreview(-1);
        Serial.println("[ui] 結束表情預覽");
      }
      break;
    case 'n':   // A5-1：切頁（n 切換、n0 表情頁、n1 資訊頁），驗收用；只有 Desktop 模式有資訊頁
      if (mode_manager::current() != Mode::Desktop) {
        Serial.println("[ui] 資訊頁只在 Desktop 模式（B 短按切回 Desktop）");
      } else {
        const bool want = cmd[1] == '1' ? true : cmd[1] == '0' ? false : !ui_info::shown();
        if (want != ui_info::shown()) ui_fx::start(ui_info::shown() ? ui_info::canvas() : ui_expression::canvas());
        ui_info::setShown(want, "序列埠 n");
      }
      break;
    case 't':   // A5-1：換頁交叉淡化（t 切換、t1 開、t0 關），比較用
      ui_fx::setEnabled(cmd[1] == '1' ? true : cmd[1] == '0' ? false : !ui_fx::enabled());
      break;
    case 'z':   // P1 省電：z 狀態、z0 關（對照）、z1 開
      if (cmd[1] == '0') power_save::setEnabled(false);
      else if (cmd[1] == '1') power_save::setEnabled(true);
      power_save::printStatus();
      break;
    case 'l': {   // P1：正常背光（l110）
      const int v = atoi(cmd + 1);
      if (cmd[1] && v >= 0 && v <= 255) power_save::setAwakeBrightness((uint8_t)v);
      else power_save::printStatus();
      break;
    }
    case 'q':   // q0 用合成波、q1 用角色＋EVA 資訊頁（需 persona_assets.h）
      if (!ui_persona::available()) {
        Serial.println("[ui] 沒有本機角色（firmware/src/persona_assets.h），見 docs/persona-swap.md");
      } else {
        ui_persona::setEnabled(cmd[1] != '0');
        Serial.printf("[ui] 表情與資訊頁：%s（角色 %s）\n",
                      ui_persona::active() ? "EVA" : "合成波", ui_persona::name());
      }
      break;
    case 'h':   // A4 量測：充電紀錄（h 印出、h0 清空）
      if (cmd[1] == '0') battery_ui::clearLog();
      else battery_ui::printLog();
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
