// ─────────────────────────────────────────────────────────────────────────
//  mode_manager：互斥狀態機（鐵則二）
//
//  任何時刻只有一個模式的資源在運作。切換一律「先舊模式 exit() 回收資源，
//  再新模式 enter() 開資源」。Safe 是急停後的安全狀態：不屬於循環的四個模式，
//  沒有任何模式資源啟用。
// ─────────────────────────────────────────────────────────────────────────
#pragma once
#include <stdint.h>

enum class Mode : uint8_t { Desktop, HidAgent, RfTool, AirMouse, Safe };

namespace mode_manager {

void begin(Mode initial);
Mode current();
Mode previous();              // 進入 Safe 前的模式（resumeFromSafe 會回到這裡）
const char* name(Mode m);
const char* currentName();

void next();                  // 四個模式循環（Safe 中呼叫無效）
Mode peekNext();              // next() 會切到哪個模式（過場畫面要先知道）
void switchTo(Mode m);        // 先 exit() 舊模式，再 enter() 新模式
void emergencyStop();         // 目前模式 exit() → Safe
void resumeFromSafe();        // 解除急停 → 重新 enter() 急停前的模式

// 31-m4f（Miles 11-22）：新模式的資源是否真的好了（切模式過場等它，好了才進新畫面）。
// *step＝還在等什麼（給過場畫面顯示，4 個全形字以內）。Desktop＝中控連上；AirMouse＝藍牙開始廣播。
bool currentReady(const char** step);

}  // namespace mode_manager
