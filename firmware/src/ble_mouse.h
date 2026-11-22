// ─────────────────────────────────────────────────────────────────────────
//  ble_mouse：藍牙低功耗（BLE）HID 滑鼠（M4-0，2026-11-22）
//
//  HID（Human Interface Device）＝鍵盤、滑鼠這類輸入裝置的標準格式；走藍牙時叫 HID over GATT。
//  用 NimBLE-Arduino（比 Arduino 內建的 Bluedroid 省 Flash 與 RAM）。只在 AirMouse 模式開（鐵則二）：
//    enter → begin()：開藍牙、建 HID 服務、開始廣播「M5S3Pack」
//    exit  → end()  ：斷線、關藍牙、釋放記憶體
//  配對紀錄（bonding）存在 NVS，同一台電腦／平板下次會自動重連。
//  每送一筆滑鼠封包前都查急停閘門 killswitch::outputAllowed()；閘門關了就停送並斷線（鐵則三）。
//  藍牙回呼在 NimBLE 自己的 task 執行：回呼裡只改旗標，log 由主迴圈 loop() 代印。
//  序列埠 @：@ 狀態、@1 游標畫圓測試、@0 停止、@f 清除所有配對紀錄
// ─────────────────────────────────────────────────────────────────────────
#pragma once
#include <stdint.h>

namespace ble_mouse {

enum class State : uint8_t { Off, Advertising, Connected };

bool begin();                    // 開藍牙並開始廣播；失敗回 false（log 會印原因）
void end();                      // 斷線、關藍牙、釋放（可重複呼叫）
void loop();                     // 每圈主迴圈呼叫：代印 log、畫圓測試、急停檢查

State state();
bool  starting();               // 進 AirMouse 了，還在等 Wi-Fi 關掉才開藍牙
const char* stateName();         // "OFF" / "ADV" / "CONN"
bool  secured();                 // 已加密（配對完成，HID 報告才會被對方接受）
int   bondCount();

// 送一筆滑鼠報告（dx／dy／wheel 會夾在 −127～127）；沒連線、急停中回 false
bool report(int dx, int dy, int wheel, uint8_t buttons);
enum : uint8_t { BTN_LEFT = 0x01, BTN_RIGHT = 0x02, BTN_MIDDLE = 0x04 };

void setCircleTest(bool on);     // @1／@0：游標自己畫圓（驗收連線用）
bool circleTest();
void forgetBonds();              // @f：清除所有配對紀錄並斷線
void printStatus();              // @

}  // namespace ble_mouse
