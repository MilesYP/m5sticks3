# 第三方程式碼與授權（THIRD_PARTY）

本專案以 **GNU Affero General Public License v3.0 or later（AGPL-3.0-or-later）** 發布，全文見 [`LICENSE`](LICENSE)。
（2026-11-22 Miles 決定：第一版完全開源；之後要從 Bruce 複製部分 RF 程式碼，Bruce 為 AGPL-3.0-or-later。）

> 這份清單不是法律意見。新增任何第三方程式碼或函式庫前，先在下表登記來源、版本、授權。

## 一、透過 PlatformIO `lib_deps` 使用的函式庫（不在 repo 內，建置時下載）

| 函式庫 | 版本（鎖定） | 授權 | 用在哪 |
| :-- | :-- | :-- | :-- |
| M5Unified | `#0.3.0` | MIT | 全部韌體 |
| M5GFX（含 efont 點陣字型 `efontTW_*`） | `#0.3.0` | MIT（efont 字型另有其授權，見 M5GFX `src/lgfx/Fonts/efont/`） | 全部韌體 |
| links2004/WebSockets | `^2.4.224` | LGPL-2.1 | `firmware/`（中控連線） |
| h2zero/NimBLE-Arduino | `2.5.0` | Apache-2.0 | `firmware/`（M4 BLE 滑鼠） |
| bmorcelli/SmartRC-CC1101-Driver-Lib | `#7bba5217dc0632f718a3278dc27e401016741a95` | 自訂寬鬆條款（ELECHOUSE 授權修改發布；「可自由使用、修改、散布，需註明來源」，見其 `LICENSE.txt`） | `acceptance-test/`、`rssi-test/`、`rf-lab/` |

## 二、中控（`cloud/`）npm 套件

| 套件 | 版本 | 授權 | 用途 |
| :-- | :-- | :-- | :-- |
| opencc-js | 1.4.2 | 見套件（上游 OpenCC 為 Apache-2.0） | 簡轉台灣繁體 |
| wrangler（開發工具，不隨程式發布） | ^4 | MIT OR Apache-2.0 | 部署 |

## 三、字型（轉成點陣圖內嵌）

| 字型 | 授權 | 產出檔 |
| :-- | :-- | :-- |
| Noto Serif CJK TC（Black／Bold） | SIL Open Font License 1.1 | `firmware/src/kanji_bitmaps.h`、`ui_labels.h`（`tools/gen_kanji.py` 產生）、`air_labels.h`（`tools/gen_air_labels.py` 產生） |

## 四、從其他專案複製進 repo 的程式碼

複製規則（給所有人與 AI 代理，詳見 `AGENTS.md`）：
1. 檔案放在 `<子專案>/src/third_party/<來源>/`，**保留原檔頭的著作權與授權宣告**。
2. 檔頭加一行：`// Copied from <repo>@<commit> <原路徑>; modified by m5sticks3: <改了什麼>`。
3. 在下表登記。

| 來源 | commit／版本 | 原授權 | 複製的檔案（原路徑 → 本 repo 路徑） | 修改摘要 |
| :-- | :-- | :-- | :-- | :-- |
| （尚無） | | | | |

已知上游關係：Bruce 的 `src/modules/rf/protocols/rf_decoder`／`rf_encoder`／`rf_keeloq` 源自 Flipper Zero 韌體（GPL-3.0-or-later），與 AGPL-3.0 相容。**KeeLoq、滾動碼相關檔案與金鑰庫不得複製**（`CLAUDE.md` 第 8 節）。
