// ─────────────────────────────────────────────────────────────────────────
//  ui_status：M0 狀態畫面與序列埠報告
//
//  螢幕：目前模式、Grove 輸入裝置、急停狀態、按鍵提示；急停時整頁紅底 KILLED。
//  原生 USB CDC 在電腦打開序列埠前印的訊息會遺失 → B 按住 1～3 秒放開可重印報告。
// ─────────────────────────────────────────────────────────────────────────
#pragma once

namespace ui_status {

void begin();
void render(bool force);   // force=false：只在 UI_REFRESH_MS 到期時重畫
void printReport();

}  // namespace ui_status
