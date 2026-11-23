# 共用元件目錄

> 新畫面要用下列元件組合。「標準實作」是要照抄的那一份（數值、比例一致）；目前各頁各有一份，抽成 `ui_kit.*` 前**複製時不要改參數**。
> 圖：[img/components.png](img/components.png)（C1～C8 對應下表）。

| # | 元件 | 規格 | 標準實作 |
| :-- | :-- | :-- | :-- |
| C1 | 狀態列 | 高 12px；左「名稱」Font0 琥珀＋2×2 點＋中文 efontTW_10；右電量 % Font0（綠／黃／紅）；y=12 琥珀 1px 線；ROOT（紅底）／斷線（灰底）實心小標籤放電量左邊 | `ui_expression.cpp` `pxHeader()`；體感 `ui_airmenu.cpp` `statusBar()` |
| C1b | 實心狀態標籤 | 琥珀底黑色明朝點陣字（待機…），警報時紅底 | `ui_expression.cpp`（`ui_labels.h` 的 `st_*`） |
| C2 | 磚塊 | 框線色 1px＋左上／右下四角暗橘 4px；左上六角標籤（琥珀底黑字，10px 高，左右各削 3px） | `ui_info.cpp` `frame()`、`hexLabel()`、`hexLabelAt()` |
| C2b | 七段數字 | 段兩端削尖、段間 1px 縫、未亮段畫 `#241300`；寬 ≥11 段粗 3px；斜體版每 6 列偏 1px | `ui_info.cpp` `seg7()`／`seg7I()`；`ui_airmenu.cpp` `seg7()` |
| C2c | 小字混排 | 英文 Font0（下移 1px）、數字 efontTW_10_b、中文 efontTW_10 | `ui_info.cpp` `txt()`、`mixedWidth()`、`nextRun()` |
| C3 | 紅黑警示斜紋 | 紅 6px／週期 12px，高 4px（橫式 3px） | `ui_info.cpp` `hazard()` |
| C4 | 黃黑斜紋 | 黃 `#FFD000` 8px／週期 16px，相位 `(y) % 16`；與內容之間 2px 琥珀線 | `ui_modeswitch.cpp` `stripes()` |
| C4b | 模式大字 | 紅 `#FF2923` 明朝 Black 點陣，原生尺寸畫 | `ui_modeswitch.cpp` `drawKanji()`；體感左上同一張 `kanji::find("air")` |
| C5 | 六角徽章清單 | 列高 31px；徽章半寬 11（左右尖），選中實心琥珀＋黑字、其他暗橘框；名稱 efontTW_12（選中亮橘白）；第二行 Font0 英文代號＋點陣說明；選中列底 `#241300` | `ui_airmenu.cpp` `drawMenu()`、`hexShape()` |
| C6 | 六角按鍵 | 半寬 15；平常琥珀實心＋黑色 18px 明朝字；按下＝亮橘白底＋外圈半寬 17 黃框；不可用＝框線色暗框；下方 12px 明朝小標寫按哪顆 | `ui_airmenu.cpp` `keyHex()` |
| C7 | 雷達 | 半徑 44、三圈（100%／60%／25%）＋十字＋每 30° 刻度；進度弧在半徑外 1～3px、12 點起順時針；掃描線＋3 道殘影；準星 7px 四角＋5px 中心點；鎖定＝外圈綠、中圈黃、內圈綠 | `ui_airmenu.cpp` `radar()` |
| C8 | 底部動作帶 | 高 18px 黃黑斜紋（C4 規格）＋上 2px 琥珀線＋中間黑底 13px 明朝字；破壞性改紅 | `ui_airmenu.cpp` `footer()` |
| C9 | 六角背景格 | 邊長 9px、左右尖，每格只畫 3 條邊；背景用快取畫布 | `ui_expression.cpp` `pxHexGrid()`、`pxBackground()` |
| C10 | 六角符號徽章 | 邊長＝背景格 2 倍（36×31），中心吸附格子中心；事件實心、狀態框線 | `ui_expression.cpp` `drawBadge()` |
| C11 | 取景角 | 四角 L 形 9px | `ui_expression.cpp` `pxBrackets()` |
| C12 | 長按倒數框（電源鍵） | 四邊交替閃斜紋、六角框外圈進度、框內明朝大字＋七段一位小數、下方實心標籤 | `power_key.cpp` `overlayDraw()`、`stripeBand()` |
| C13 | 受信對話框 | 左上凸出實心頁籤「受信」＋紅黑斜紋；你說的話米白、Friday 綠 | `ui_expression.cpp`（`convHazard()` 附近） |
| C14 | 換頁交叉淡化／部分更新 | `ui_fx::start()` → 畫新頁 → `ui_fx::present()` | `ui_fx.*` |

## 產生器

| 工具 | 產出 | 字型 |
| :-- | :-- | :-- |
| `tools/gen_kanji.py --set mode` | `firmware/src/kanji_bitmaps.h`（模式大字） | Noto Serif CJK TC Black |
| `tools/gen_kanji.py --set ui` | `firmware/src/ui_labels.h`（狀態大字等） | Noto Serif CJK TC Bold |
| `tools/gen_air_labels.py` | `firmware/src/air_labels.h`（體感固定字，依文字查表；字級 key 18／head 15／item 14／body 13／cap 12px） | Noto Serif CJK TC Black |

新增固定字：把字加進產生器清單 → 重跑 → 在模擬器看 → 編譯確認 Flash 用量。

## 模擬器

| 指令 | 畫面 |
| :-- | :-- |
| `bash tools/sim/build.sh face` | 表情頁（`.out/ex/`） |
| `bash tools/sim/build.sh info` | 資訊頁（`.out/out/`，`INFO_SIM` 會檢查字超出磚塊） |
| `bash tools/sim/build.sh air` | 體感選單／等待／已連線／倒數（`.out/air/`） |
| `bash tools/sim/build.sh motion`／`anim`／`lowbat` | 甩碎、動畫、低電量 |

模擬器輸出是 PPM，轉 PNG 時用最近鄰放大（`Image.NEAREST`）2～3 倍，不要平滑縮放。
