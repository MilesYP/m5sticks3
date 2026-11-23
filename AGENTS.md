# AGENTS.md — 給 Codex（及其他非 Claude Code 代理）的專案規則

> 本 repo 的完整工程約定在 `CLAUDE.md`（Claude Code 讀那份）。這份是 Codex 的入口：**先讀本檔，再讀下面「必讀」**。
> 兩份衝突時以 `CLAUDE.md` 的鐵則為準；本檔只多規定「Codex 的工作範圍、Bruce 程式碼複製規則、併回正式韌體的介面」。
> 本檔由 Miles 與 Claude Code 維護；Codex 要改本檔先問 Miles。

## 0. 分工（2026-11-22 Miles 定案）

- **Claude Code**：正式韌體 `firmware/`、中控 `cloud/`，目前做 M4 空中滑鼠 → M1.5 紅外線 → M2 HID。
- **Codex**：**M3 無線電工具箱（RF 軌）**，在獨立子專案 `rf-lab/` 開發與實機驗證；完成的模組照第 5 節介面交件，由 Claude Code 併進 `firmware/`。
- **Miles**：PM，不親手寫 code；判斷「為什麼這樣設計」、實機操作、燒錄、貼序列埠 log、決定取捨。

## 0.1 暫時共同維護正式韌體（2026-11-23 Miles 決定，到 Claude 的 token 恢復為止）

- 這段期間 **Codex 也可以改 `firmware/`、`docs/PROGRESS.md`、`docs/StickS3 執行計劃.*`、`docs/KICKOFF_*.md`**，接手正式韌體的進行中工作（11-23 起：M4 已完成，接下來狀態列字型決定 → M1.5 紅外線，交接見 `docs/HANDOFF_CODEX.md`）。`CLAUDE.md` 仍以 Claude 維護為主；Codex 得出的新工程規則先寫在 PROGRESS 該筆紀錄的「新規則」，Claude 回來後搬進 `CLAUDE.md`（Miles 明確要求時 Codex 也可直接改）。
- 改正式韌體在主資料夾 `~/Developer/m5sticks3`（`main`）；**開工前 `git pull`、收工前 commit＋請 Miles `git push`**，一次只做一件事、commit 小一點，避免和 Claude 同時改同一檔。RF 軌照舊在 `codex/rf-lab` worktree。
- 正式韌體一律遵守 `CLAUDE.md` 全部鐵則（急停最高優先、主迴圈不阻塞、網路只在網路 task、藍牙開著不可設 Wi-Fi `PS_NONE`、不碰語音流程與 P2 電源鍵規則除非 Miles 要求）；版本號改 `firmware/src/audio_io.cpp` 的 `AUDIO_BUILD_TAG`；每片結束在 `docs/PROGRESS.md` 最上方新增一筆（Session 類型寫「Codex」）。
- 原本第 2 節「不可以改」對上述檔案暫停適用；`cloud/`、`selftest/`、`acceptance-test/`、`rssi-test/`、`LICENSE` 仍不改（Miles 要求除外）。**11-23 起 `tools/sim/`、`tools/gen_kanji.py`、`tools/gen_air_labels.py`、`docs/design/` 也可以改**（畫面工作需要）；`cloud/persona/friday.md` 可依 CLAUDE.md 第 9 節更新功能說明。
- **畫面一律照 `docs/design/README.md`（介面設計總則）與 `docs/design/COMPONENTS.md`**：共用元件用 `firmware/src/ui_kit.*`，不要自創風格；先用 `tools/sim/` 出所有狀態的模擬圖給 Miles 看，他同意才改 `firmware/`；改到 `ui_kit` 或共用畫圖時先跑 `bash tools/sim/regress.sh` 前後比對。

## 1. 和 Miles 協作

- 回覆用**繁體中文**；技術名詞第一次出現時用一句白話解釋。
- **先討論再實作**：新功能先釐清需求、查證可行性、給計畫與要改的檔案；Miles 說「開始實作」才寫程式。Miles 說「先討論」＝停在需求與計畫。技術選擇請直接定，真的無法判斷才問。
- 外觀（畫面）先描述或出模擬圖給 Miles 看再動程式（可參考 `tools/sim/`）。
- 給 Miles 的指令要能直接複製執行（含 `cd`），並說明看序列埠哪一行判斷成功。
- **不要臆測腳位、暫存器、函式庫行為**：查規格書、原始碼或實測；不確定就停下來問。

## 2. 工作範圍（避免和 Claude Code 撞檔）

| 可以改 | 不可以改 |
| :-- | :-- |
| `rf-lab/`（全部）、`docs/rf/`（全部） | `firmware/`、`cloud/`、`selftest/`、`acceptance-test/`、`rssi-test/`、`tools/` |
| `THIRD_PARTY.md` 第四節表格（只新增列） | `CLAUDE.md`、`docs/PROGRESS.md`、`docs/StickS3 執行計劃.*`、其他 `docs/` 檔、`AGENTS.md`、`LICENSE` |

- 進度寫在 **`rf-lab/PROGRESS.md`**（格式同 `docs/PROGRESS.md`，最新在最上面）；新的工程規則寫在 **`docs/rf/RULES.md`**，併回時由 Claude Code 搬進 `CLAUDE.md`。
- Git：在分支 **`codex/rf-lab`** 上 commit（訊息用繁體中文、說清楚改了什麼）；併回 `main` 由 Miles／Claude Code 處理。
- **一定要用獨立資料夾（git worktree），不要在主資料夾 `~/Developer/m5sticks3` 切分支**：主資料夾是 Claude Code 在用的 `main`，在那裡 `git checkout` 會把 Claude 的工作檔換掉、Claude 的 commit 也會落到你的分支（11-22 發生過一次）。做法：在主資料夾 `git worktree add ../m5sticks3-rf codex/rf-lab`，之後只在 `~/Developer/m5sticks3-rf` 工作；要拿 `main` 的新東西就在你的資料夾 `git merge main`。
- **絕不提交或讀取**：`firmware/src/secrets.h`、`firmware/persona_local/`、`cloud/persona/miles.local.md`、任何密碼／token／Wi-Fi 帳密。

## 3. 必讀（依序）

1. `CLAUDE.md`：第 0、1 節（**鐵則一～五、鐵則十一第 3 點**）、第 2 節按鍵配置、第 3 節技術棧、第 7、8 節。
2. `docs/cc1101-spi-reliability.md`、`docs/bruce-1.17-rf-findings.md`、`docs/hardware-acceptance-test.md`。
3. `rssi-test/`（目前唯一實機驗證過、照鐵則五寫的 CC1101 程式；`rf-lab/` 的建置設定照抄它的 `platformio.ini`）。
4. `docs/M5StickS3+RF Pack S3.md`（PRD）第 2 節模式 3。

## 4. 硬性規則（摘要；細節以 CLAUDE.md 為準）

### 4.1 硬體與驅動
- 腳位（已實機驗證）：CC1101 SCK G5／MISO G4／MOSI G6／CS G2／GDO0 G3；nRF24 CS G8／CE G1（同一組 SPI）；SD CS G7（不用，初始化時拉高）。螢幕佔 **SPI3_HOST** → RF 一律用 **SPI2（Arduino `FSPI`）**；存取任一顆前其他 CS 必須是高電位。
- 建置固定：`platform = espressif32@6.12.0`（Arduino 2.0.17／ESP-IDF 4.4）、`M5Unified`／`M5GFX` `#0.3.0`、SmartRC 鎖 `#7bba5217…`。**不要升級到 Arduino 3.x／IDF 5**（`checkMISO()` 會失效，正式韌體也不會升）。
- **CC1101 SPI 防錯（鐵則五第 7 點，最重要）**：單筆交易約 0.5% 錯、會成叢。讀暫存器用多數決（至少 3 筆，無多數擴大到 9 筆）；切換狀態或改設定後先確認 `VERSION`＝`0x14` 才信任其他讀值；寫入一律讀回驗證、不符重寫；**禁用會先讀回再寫回的函式**（`setModulation`／`setPktFormat`／`setRxBW`／`setCCMode` 等，內部 `Split_*()`）→ 自己算完整位元組整顆寫入並驗證；不做 burst 連續讀，高強度存取之間留 2 秒。
- `Init()`／SRES **整個程式只做一次**；切頻率或 RX／TX 只改暫存器與下 strobe，切頻率前先 SIDLE。不照抄 Bruce 的 `AGCCTRL2=0xC7`。
- CC1101／nRF24 的電源不隨 ESP32 重開機斷電，暫存器會殘留上一支韌體的設定 → 初始化不可假設出廠值。晶片卡死：拔 USB → 快速按兩下電源鍵關機（root 狀態下）→ 等 10 秒。
- Grove I2C（`M5.Ex_I2C`，G9／G10）一律**無條件 `begin()`**；CardKB `0x5F`／Scroll `0x40` 二選一，開機掃描判斷，**不可假設 Scroll 一定在**。

### 4.2 燒錄（只有一台機器，Miles 會在 Claude 與 Codex 的韌體間輪流燒）
- 正式韌體開了 M5PM1 看門狗（60 秒沒人餵就重置）。**燒 `rf-lab` 之前，Miles 必須先在正式韌體的序列埠打 `q0` 進 root**（或長按電源鍵 5 秒放開），否則 `rf-lab` 每 60 秒被重置。每次請 Miles 燒 `rf-lab` 都要提醒這一步。燒回正式韌體後會自動回新機制。
- 燒錄：`cd ~/Developer/m5sticks3/rf-lab && pio run -t upload -t monitor`（port busy 時先 `kill $(lsof -t /dev/cu.usbmodem1101)`）。連不上：長按電源鍵約 2 秒至綠燈閃 → 放開 → Upload。

### 4.3 程式規範
- 非阻塞：主迴圈與 task 不可長 `delay()`；用 `millis()` 排程。急停相關 ISR／高優先權 task 內不可 `Serial` 或任何可能阻塞的呼叫。
- 大緩衝放 PSRAM（`ps_malloc`，記得 `memset`），在模組 `exit()` 時釋放。
- 中文字型只能用 `efontTW_10`、`efontTW_10_b`、`efontTW_12`（大字型會把整套中文字庫編進韌體，超過 app 分割區）；英文小字 `Font0`。大字用 `tools/gen_kanji.py` 產點陣圖（請 Miles 或 Claude 產）。
- Flash 預算：正式韌體目前約 2.23MB／3.34MB；RF 模組（含協定表）目標 **≤ 500KB**。不改分割表 `default_8MB.csv`。
- 腳位、位址、門檻寫成具名常數。每個功能先寫 2～3 個函式實機驗證再展開；每複製或新增一個檔案就先編譯一次。

### 4.4 安全與法律（CLAUDE.md 第 8 節，不可妥協）
- RF 只用於 **Miles 自己的設備**；433／315MHz 被動接收合法，主動發射僅限自有、低功率、短時間。
- **不做**：干擾（jamming，含連續載波當干擾用）、滾動碼／KeeLoq（含 Bruce 的金鑰庫）、暴力破解（含 de Bruijn）、對他人設備的 2.4GHz 鍵鼠注入（mousejack）、Wi-Fi 斷線攻擊、釣魚頁。就算 Bruce 有也不搬。
- 每次發射前都要查急停閘門（第 5 節 `rfOutputAllowed()`），連續重送的每一輪之間也要查。

## 5. 從 Bruce 複製程式碼的規則（2026-11-22 Miles 決定 repo 改 AGPL-3.0-or-later）

- 來源：`BruceDevices/firmware`。開工時固定一個 commit（用最新 release tag），寫進 `THIRD_PARTY.md` 與 `rf-lab/PROGRESS.md`，之後要跟進新版另開一筆。
- **只讀需要的檔案**（省 token）：從 `src/modules/rf/` 找協定、`.sub`、頻譜相關檔案，不要瀏覽整個 repo。
- **可以複製**：協定解碼／編碼與時序表（固定碼）、`.sub`（Flipper 格式）讀寫解析、頻譜／瀑布圖的資料處理、頻率清單與預設參數表。
- **只能參考、必須照第 4.1 節重寫**：CC1101 初始化與 SPI 存取（`rf_utils` 一類）、AGC 設定、選單、顯示 HAL、設定存檔、輸入流程。
- **不可複製**：第 4.4 節排除的功能相關檔案。
- 複製的檔案放 `rf-lab/src/third_party/bruce/`，保留原檔頭；檔頭加 `// Copied from BruceDevices/firmware@<commit> <原路徑>; modified by m5sticks3: <改了什麼>`；在 `THIRD_PARTY.md` 第四節加一列。
- Bruce 用 Arduino 3.x／IDF 5；複製後若用到新版才有的 API（新版 RMT／SPI 寫法等），改寫成 Arduino 2.0.17 可用的版本，或換做法。

## 6. 併回正式韌體的介面（交件格式）

正式韌體是互斥狀態機（鐵則二）：B 短按在 Desktop → HidAgent → RfTool → AirMouse 間循環，切換時舊模式 `exit()`、新模式 `enter()`。RfTool 模式下 Wi-Fi、BLE、ES8311、USB HID 都關著。

把 RF 功能做成一個獨立模組 `rf-lab/src/rf_tool/`（`rf_tool.h/.cpp` 加子檔），**不依賴 `rf-lab` 的 `main.cpp`**，介面如下（可以加函式，但這幾個名稱與語意要保留）：

```cpp
// rf_tool.h
#pragma once
#include <stdint.h>
namespace m5gfx { class M5Canvas; }   // 只能這樣前置宣告（M5Canvas 是 using 別名）

enum class RfInput : uint8_t {
  ScrollUp, ScrollDown, ScrollPress,   // Scroll Unit（可能不存在）
  APress, ARelease,                    // 正面 A 鍵：模式內主動作
  ALong,                               // A 按住 ≥ 0.8 秒（沒有 Scroll 時當「確定」用）
};

namespace rf_tool {
bool enter(bool hasScroll);            // 進入模式：開 SPI、初始化晶片（Init 全程只做一次，第二次進入只喚醒＋驗證暫存器）、配緩衝。失敗回 false 並顯示原因
void exit();                           // 離開模式：停止收發、晶片進省電（SIDLE→SPWD／nRF24 power down）、釋放所有緩衝與中斷。必須在 50ms 內完成
void loop(uint32_t nowMs);             // 每圈主迴圈呼叫；非阻塞，單次 ≤ 5ms（接收解碼用中斷／佇列）
void onInput(RfInput ev);
void render(m5gfx::M5Canvas* cv);      // 畫到 135×240 直式畫布（呼叫端負責送上螢幕），每幀 ≤ 10ms
bool wantsRedraw();                    // 有東西要更新才回 true（省電）
}

// 由呼叫端提供（正式韌體接 killswitch::outputAllowed()；rf-lab 自己實作：B 按住 3 秒＝關閉）
bool rfOutputAllowed();
```

- **按鍵**：B 短按、B 長按（1～3 秒、3 秒急停）、電源鍵都是**系統鍵，模組收不到也不能用**。模組內只用 A 與 Scroll；沒有 Scroll 時要能只靠 A 操作（短按＝下一項、`ALong`＝確定）。
- **急停**：閘門一關，正在發射的要在 1ms 級內停（下 SIDLE），之後任何 TX 都不准開始。
- **畫面**：只畫到傳進來的畫布，不直接呼叫 `M5.Display`；字型限制見 4.3；配色可先用簡單的，A7 風格（琥珀橘 `#FF9B00`、黑底）由 Claude 併入時統一。
- **聲音**：模式表規定 RfTool 關 ES8311。蓋革嗶聲需要喇叭 → **先做無聲版**，喇叭版等 Miles 決定（若要做：只開喇叭、不開麥克風，遵守鐵則六第 1、4 點）。
- **儲存**：學到的碼存 NVS（`Preferences`，namespace `rf`）；`.sub` 匯入若要檔案系統用 LittleFS（不改分割表），先在 `docs/rf/` 寫清楚容量估算。
- **`rf-lab/README.md`** 要有：每個功能的實機驗收步驟（看序列埠哪一行、畫面看到什麼）、已知問題、Flash／PSRAM 用量。

## 7. 建議階段（每階段：計畫 → Miles 說「開始實作」→ 編譯 → Miles 實機 → commit）

- **R0**：起草給 Bruce 的英文 issue（`docs/rf/bruce-issue-draft.md`）：用 `rssi-test/` 的重現步驟與 `docs/cc1101-spi-reliability.md` 的數據，說明 0.5% SPI 錯誤是用**自有程式＋同一份 SmartRC 驅動**量到的，附 repo 連結。Miles 看過後自己貼。
- **R1**：`rf-lab/` 骨架＋照 4.1 寫的 CC1101 安全存取層（`cc1101_safe.*`：多數決讀、驗證寫、VERSION 檢查、存取節流）＋nRF24 初始化；序列埠自檢與錯誤統計。
- **R2**：315／433.92MHz 固定碼接收解碼，畫面顯示協定、碼值、位元數、脈寬。
- **R3**：學習 → 命名 → 存 NVS → 選單重放（過急停閘門）。
- **R4**：RSSI 掃描（柱狀／瀑布圖），蓋革嗶聲先做無聲（畫面閃爍）版。
- **R5**：`.sub` 匯入與重放（只限自有設備的碼）、頻率分析。
- **R6**：nRF24 2.4GHz 被動頻譜掃描。
- 每階段結束：更新 `rf-lab/PROGRESS.md`、`docs/rf/RULES.md`、`THIRD_PARTY.md`（如有複製），commit 到 `codex/rf-lab`。
