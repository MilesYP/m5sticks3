#include "diag_cmd.h"

#include <Arduino.h>
#include <stdlib.h>
#include <ctype.h>

#include "audio_io.h"
#include "net_hub.h"

namespace {

char s_line[16];
size_t s_n = 0;

void printConfig() {
  Serial.printf("[diag] 目前：%s pga=0x%02X\n", net_hub::configLabel(), audio_io::pga());
}

void printHelp() {
  Serial.println("[diag] 指令：w0/w1 Wi-Fi關/開｜s0/s1 省電關/開(手動)｜sa 省電自動｜p20 p15 p11 p8 p5 p2 發射功率｜g17 PGA(16進位)｜k 送 ping｜? 目前設定");
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
    case 'k':
      net_hub::pingNow();
      break;
    case '?':
      printConfig();
      break;
    default:
      printHelp();
      break;
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
    } else if (s_n < sizeof(s_line) - 1) {
      s_line[s_n++] = (char)ch;
    }
  }
}

}  // namespace diag_cmd
