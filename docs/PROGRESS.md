# 專案進度紀錄（PROGRESS）

> **用途**：這是各個 Session（Claude Code CLI、claude.ai／Cowork…）之間共享進度的唯一地方。各 Session 的記憶不互通，但都讀得到這份檔案。
> **規則**：
> - 新紀錄加在「紀錄」區的**最上方**（最新的在最前面）。
> - 開始工作前先讀最上面 2～3 筆；結束時新增一筆，再 `git commit` + `git push`。
> - 工程規則與設計決議寫在 `CLAUDE.md`；詳細測試數據寫在對應文件（如 `docs/hardware-acceptance-test.md`）。這裡只記摘要與連結。
>
> **每筆紀錄格式**：
> ```
> ### YYYY-MM-DD｜里程碑名稱｜Session 類型（Claude Code CLI / claude.ai / Cowork）
> - 做了什麼：
> - 驗收結果：
> - 遇到的坑／新規則：（新規則要同步寫進 CLAUDE.md）
> - 下一步：
> - 待辦／未決：
> ```

## 目前狀態（每次更新時順手改這一段）
- **階段**：**M4 空中體感滑鼠 ✅ 完成（11-23，畫面定稿）**。介面設計總則定稿於 `docs/design/`（所有新畫面照它做）。下一步 M1.5 紅外線（交接 `docs/KICKOFF_M15.md`），之後 M2 HID。
- **進行中**：`codex/m4-2` 已合併回 main（11-24）；共用元件 `ui_kit.*`；下一棒 Codex（`docs/HANDOFF_CODEX.md`）。RF R1 在 `codex/rf-lab` 已編譯，仍待 Miles 實機驗收（Codex）。
- **待實機**：共用 StickS3 執行 M1.5 IR1 新版 RMT 診斷版。USB 與電池各 10 秒的基準取樣都無 G42 跳變。USB 接入時已收到風扇遙控器的脈衝和 NEC 重複碼，完整碼待驗。B 版 `31-status-b` 可由正式韌體專案燒回。
- **硬體狀態（給所有 Session）**：RF Pack S3 **可用但 SPI 不乾淨**（2026-11-17 `rssi-test/` v6.4 共 6 輪實測定案）。接收正常（315.00 MHz −16 dBm、333.00 MHz −18 dBm）。CC1101 **單筆 SPI 讀取穩定約 0.5% 錯**，且錯誤會成叢出現，「連讀兩次相同」被騙過 2 次（最嚴重 14/200）→ 必須用**多數決＋`VERSION`=`0x14` 語意檢查**，寫入一律驗證重寫。高強度連續存取後會卡約 2 秒自行恢復；兩輪零間隔連跑會永久鎖死需重置。規則見 `CLAUDE.md` 鐵則五第 7 點與 `docs/cc1101-spi-reliability.md`。0.5% 錯誤率與 2 秒鎖死都不算正常，但單片無法判定是本片瑕疵或此型通病 → **換貨未定，已請 PINGEQUA 用樣品對照並保留期限**。nRF24、StickS3 本體正常。M3 可照常開發與燒錄（照上述規則寫）。
- **待辦**：
  - **設定後台＋LINE 入口（11-23 Miles 提出，未排期）**：中控 Worker 網域下架網頁後台調裝置參數（先做 M4 的 `@g/@n/@d/@x/@y`），LINE 官方機器人當入口（參考「Netflix 驗證碼」專案，位置待指定）。細節與待釐清事項見執行計劃「設定後台與 LINE 入口待辦」
  - **寄信前先 commit＋push `rssi-test/` 與 `docs/`**，信中要附 GitHub 連結（`https://github.com/MilesYP/m5sticks3/tree/main/rssi-test`）給 PINGEQUA 自行復現；`rssi-test/README.md` 已有英文復現步驟
  - 選做：SCAN 模式下用 315 MHz 遙控器在 1 公尺做「有天線／拔天線」對照，確認 SMA 與天線路徑
  - （已完成第 2 步即重現）分離測試（完整斷電後：MONITOR 靜置 3 分鐘 → SCAN 300-348 靜置 2 分鐘 → 遙控器 1 公尺 → 貼近 → 不按遙控器輕壓 RF Pack），記錄 drop／bad 與 checkMISO 錯誤何時出現
  - 回信 PINGEQUA：依分離測試結果決定內容（先前「撤回換貨」草稿作廢）
  - `rssi-test` 待修／M3 動工前要釐清：(1) SCAN 掃 ALL 時底噪約 -55 dBm，只掃 300-348 時約 -101 dBm（同樣 3 ms 穩定時間）→ 不是「RSSI 未穩定」，可能與跨頻段後驅動 `Calibrate()` 留下的 FSCAL2／TEST0 設定有關，未驗證；(2) `[regs]` 的 FREQ2 每次讀回值不同（1E／06／87，應為 10），MARCSTATE 開機頭幾秒偶有讀錯 → 狀態／暫存器讀取的可靠性要查；(3) PKTCTRL0 讀回 3F（預期 32）
  - A4b 充電液體已 ✅（29-a4g）；11-22 實機已看到判定「充飽」✅
  - **體驗細調（Miles 11-20：整體完工後再做，未排期）**：彈珠台／拍打／看方向等手感體感、表情動畫節奏、合成波配色實機微調（清單見執行計劃「M1-A 之後的體驗細調待辦」）
  - Grove 功能測試 `grove-test/`（CardKB 按鍵碼表、Scroll 每圈格數／方向／按鍵／LED），計畫見 `docs/hardware-acceptance-test.md` 階段 5

---

### 2026-11-24｜M1.5 IR1 新版 RMT 實機對照｜Codex
- 做了什麼：獨立 `ir-test/` 新增 `m5sticks3-rmt5`，使用 Arduino 3.2.1／ESP-IDF 5.4，依 [M5Stack 接收範例](https://docs.m5stack.com/en/arduino/m5sticks3/ir_nec) 設 1 MHz、128 個硬體符號、64 個接收緩衝符號、1 µs 最短脈衝與 20 ms 結束門檻。保留原 NEC 解碼器、畫面、PYG3 與供電診斷。新版預設關閉 G42 跳變中斷，可用 `g` 開關。舊版環境仍為預設。
- 建置與燒錄：新版 Flash 39.9%、RAM 8.6%，USB 燒錄與 esptool 雜湊驗證通過。實機回報 `READY RX=G42 RMT=IDF5 EXT_5V=ON`，PYG3 設定仍符合關閉功放。舊版編譯重跑後通過（Flash 37.1%、RAM 7.5%）。新版首編附加的 `esp-idf-size` 報 `--ng` 選項不支援，PlatformIO 容量檢查及韌體輸出均成功。
- 供電對照：亮度 127，開啟跳變診斷後，USB → 電池 → USB 三段各 10,000 ms，G42 跳變與 RMT 三類計數增量均為 0，平均 5V 分別 5.086／5.053／5.091 V，各 41 筆。開機識別 `4FB0728D` 一致，`DONE completed=3`。拔插期間整體累計由 `0/0/1` 變為 `0/3/10`，這些增量落在固定 10 秒取樣窗外，無法按供電階段歸因。
- 風扇遙控器：保持 USB 接入，關閉額外 G42 跳變診斷。Miles 短按電源鍵後，整體累計由完整／重複／未知 `0/3/10` 變為 `0/5/14`。其中一筆未知訊號為 29 個 RMT 符號，起始低電位 9029 µs、高電位 4645 µs，接近 NEC 起始時序。另兩筆記錄為 31 與 32 個符號。現有解碼器要求 34 個符號的完整 NEC 訊框，實機尚未收到可辨識的完整碼。
- 判斷與下一步：換版後高頻背景跳變在基準取樣中消失，USB 接入時可量到按鍵相關脈衝。這次同時換了 Arduino／ESP-IDF、RMT 驅動與接收設定，尚未分辨各因素的影響。下一步擷取數次風扇按鍵的完整原始脈衝，比對是訊框被切段、缺少符號，還是使用其他格式。IR1 尚未驗收，G46 留在 IR3。

### 2026-11-24｜M1.5 IR1 冷氣遙控器對照｜Codex
- 做了什麼：沿用同一份 IR1 診斷版與桌面配置，背光 127。USB 基準完成後，指定拔線等約 3 秒，冷氣遙控器在約 40 公分對準 StickS3 頂端，短按電源鍵三次再插回。風扇照片可辨識 14 吋 DC 變頻、7 片扇葉、12 段風量、遙控器用 CR2025，未見可辨識品牌或型號。
- 驗收結果：同一開機識別 `4068CBC2`，`DONE completed=3`。USB 前 121,474 次／10,048 ms、5V 平均 5.100 V（5.082～5.120）；電池 0 次／10,000 ms、5.053 V（5.050～5.060）；USB 後 121,748 次／10,063 ms、5.093 V（5.084～5.104）。每段各 41 筆電壓讀值，RMT 完整／重複／未知增量均為 0。
- 判斷與下一步：冷氣遙控器的電池段同樣未見 G42 脈衝。兩支遙控器的載波與協定均未確認，待使用已知可用的 38 kHz NEC 訊號源，或直接量接收小板的 5V、VSOP38338 輸出與 G42。IR1 尚未驗收。

### 2026-11-24｜M1.5 IR1 PYG3 與 USB／電池對照｜Codex
- 做了什麼：只更新獨立 `ir-test/`。加入 M5PM1 PYG3 唯讀診斷，以及在 USB 序列埠中斷時可繼續取樣的供電對照。PlatformIO 編譯通過（Flash 37.1%、RAM 7.5%），USB 燒錄與 esptool 寫入雜湊驗證通過。
- PYG3：實機讀回 `GPIO_FUNC0 0x16=0x00`、`GPIO_MODE 0x10=0x0C`、`GPIO_DRV 0x13=0x13`、`GPIO_OUT 0x11=0x04`。解出一般 GPIO、推挽輸出、低電位，符合官方 `m5pm1.pdf` PDF 第 2～3 頁及 `M5PM1_Datasheet_EN.pdf` PDF 第 18～20 頁的關功放設定。診斷未寫入 PYG3。
- 供電對照：桌面位置、未遮蔽、配件及背光 127 固定，三段皆由板載升壓供應紅外線接收器。USB 拔插前後開機識別均為 `4068CBC2`，韌體回報 `DONE completed=3`。每段有 41 筆 M5PM1 5V ADC 讀值。

  | 階段 | 取樣時間 | G42 跳變 | 跳變／秒 | 5V 平均（最低～最高） | RMT 完整／重複／未知增量 |
  | --- | ---: | ---: | ---: | --- | --- |
  | USB 接入 | 10,080 ms | 128,175 | 12,716 | 5.096 V（5.082～5.106） | 0／0／0 |
  | 電池 | 10,000 ms | 0 | 0 | 5.054 V（5.050～5.058） | 0／0／0 |
  | USB 再接入 | 10,058 ms | 123,817 | 12,310 | 5.092 V（5.078～5.110） | 0／0／0 |

- 判斷：這組桌面條件下，跳變隨 USB 接入出現，拔線時消失，插回時重現。表列數字是 M5PM1 電壓取樣，還無法分辨 USB 供電、接地或充電路徑的快速雜訊。USB 段反覆出現 RMT `RX buffer too small`。依 [ESP-IDF 4.4.7 驅動](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/rmt.c#L795-L817)，這表示驅動累積脈衝的緩衝區已滿，行尾的 `4` 是指標大小。接收器及供電見 StickS3 官方電路圖 PDF 第 2～4 頁，收訊結束條件見 ESP32-S3 TRM 第 1421～1422 頁。
- 風扇遙控器重測：第二輪指定拔 USB 後等約 3 秒，從約 40 公分處對準頂端，短按同一鍵三次。韌體回報同一開機識別與 `DONE completed=3`。電池段 G42 為 0 次／10,000 ms，5V 平均 5.054 V（5.050～5.058），完整／重複／未知增量均為 0。USB 前後各為 122,429 次／10,074 ms 和 121,883 次／10,066 ms，5V 平均 5.093 V 和 5.099 V，收碼增量亦均為 0。重新接線後序列埠全程累計三項仍為 0。
- 下一步：在電池條件下，用已知可用的 38 kHz NEC 遙控器做陽性對照。若 G42 仍無跳變，量接收小板的 5V_VBUS 與 VSOP38338 輸出，再比對 G42，以分辨接收器與板間訊號路徑。風扇遙控器的載波與協定待確認，IR1 尚未驗收，G46 留在 IR3。

### 2026-11-24｜M1.5 IR1 背光對照｜Codex
- 做了什麼：IR1 加入序列埠 `b`，自動量原亮度、背光關閉、恢復原亮度三段，每段 10 秒。USB、外接配件、桌面位置與未遮蔽條件固定。
- 驗收結果：編譯通過，Flash 37.1%、RAM 7.4%，USB 燒錄及寫入驗證通過。亮度設定 127 → 0 → 127。第一段 125,112 次／10,000ms，第二段 131,520 次／10,000ms，第三段 127,822 次／10,003ms，即每秒約 12,511 → 13,152 → 12,778 次。三段 RMT 完整碼、重複碼與未知碼增量均為 0，原亮度已恢復。
- 下一步：關背光時仍有同等量級跳變。下一輪保持背光與其他條件固定，對照 USB 與電池供電。

### 2026-11-24｜M1.5 IR1 G42 診斷｜Codex
- 做了什麼：Miles 用手機相機確認風扇遙控器按電源鍵時有肉眼看不到的發射光。官方電路圖標示接收器為 VSOP38338（38 kHz），接在 5V_VBUS。IR1 加入 G42 跳變計數、M5PM1 的 5V 讀值及功放控制讀回。
- 驗收結果：PlatformIO 編譯及 USB 燒錄驗證通過，實機回報 `probe=ESP_OK`。M5PM1 5V 讀值 5.08～5.11 V、功放控制輸出暫存器為 0。G42 未遮光約每秒 1.25 萬次跳變，Miles 遮光確認後仍約 1.2 萬次；RMT 有 `RX buffer too small` 訊息，完整碼、重複碼、未知碼計數皆為 0。
- 下一步：在相同照明下做背光開關與供電／配件隔離對照，再決定是否需要用已知 38 kHz 訊號源比對。IR1 收碼仍待驗。

### 2026-11-24｜M1.5 IR1 獨立收碼測試｜Codex
- 做了什麼：建立 `ir-test/`。G42 由 RMT 通道 4 收碼，主迴圈以零等待讀取。沿用 M5Unified 控制 EXT_5V 與喇叭功放，顯示 NEC／NEC EXT 位址、指令及原始碼。A 重印狀態，B 按住 3 秒停止接收。
- 驗收結果：PlatformIO 編譯通過，Flash 37.0%、RAM 7.4%。10 個合成訊號案例驗證標準 NEC、延伸 NEC、重複、壞碼、時序與極性。M5GFX 已檢視五種模擬畫面。實機收碼待驗。
- 上板：Miles 同意切換後，`q0` 回報部分 PM1 寫入失敗。用 `q` 讀回確認看門狗剩 0 秒、電源鍵下載模式仍鎖。USB 自動下載成功，esptool 驗證寫入雜湊。重啟後序列埠回報 `[ir1] READY RX=G42 RMT=4 EXT_5V=ON`。
- 風扇遙控器：首測三次按鍵後讀回 `count=0 repeat=0 unknown=0`。重測前發現 IR1 已停止接收，短按電源鍵重啟後確認序列埠回報 `READY`，再於約 40 公分處短按兩顆鍵並重按第一顆，三項計數仍為 0。遙控器品牌未知，通訊方式與 G42 收碼仍待確認。G46 排在 IR3。

### 2026-11-24｜狀態列 B 版｜Codex
- 做了什麼：Miles 確認 B 版。表情頁與體感共用的 12 個固定中文狀態用 11px Noto Serif CJK TC Black 點陣字，狀態列維持 12px。字表缺字或空間較小時沿用 `efontTW_10` 的顯示條件。
- 驗收結果：前後比對 1,441 張模擬圖，1,406 張差異都在狀態列文字區。資訊頁與切模式過場逐像素相同。正式程式模擬與 Miles 確認的 B 版逐像素相同。PlatformIO 編譯通過，`31-status-b` 的 Flash 74.5%、RAM 40.4%。經 Miles 確認後燒錄，esptool 驗證寫入雜湊，重啟後序列埠收到 `[mark] status-b-check`。
- 遇到的坑：舊模擬快取是 Linux 格式。重建 Mac 的 M5GFX 物件檔後，以 `tools/sim/regress.sh` 完成比對。
- 下一步：驗收 StickS3 實體螢幕的可讀性，再依 [M1.5 交接](KICKOFF_M15.md) 做 IR1。紅外線入口已定為獨立模式。

### 2026-11-24｜ui_kit 共用元件＋狀態列字型預覽＋合併 main｜Claude Code CLI
- 做了什麼：把各頁重複的畫圖元件抽成 `firmware/src/ui_kit.*`（色票、紅黑／黃黑斜紋、七段、六角、磚塊、六角標籤、取景角、小字混排、截斷字、點陣字縮放、狀態列、底部動作帶），表情頁、資訊頁、切模式、體感改呼叫它。新增 `tools/sim/regress.sh`（全頁模擬輸出，`time()` 固定）、`tools/sim/sim_modeswitch.cpp`、`build.sh ms`。狀態列中文三版預覽 `docs/design/img/statusbar_options.png`（A 現行／B 明朝 11px／C 明朝 14px 加高）。
- 驗收結果：模擬 1441 張（表情 face／anim／lowbat／motion、資訊、體感、切模式）重構前後逐像素 0 差異；編譯 Flash 74.5%、RAM 40.4%；已燒錄。
- 遇到的坑／新規則：`ui_kit::color` 不能 `using namespace`（Arduino 有全域 `RED` 會撞名）。改 `ui_kit` 或共用畫圖前後一定跑 `tools/sim/regress.sh` 比對（寫進 `docs/design/COMPONENTS.md`）。電源鍵長按頁的 `seg7`／`stripeBand` 沒有模擬器，未搬。
- 下一步：Codex 接手（`docs/HANDOFF_CODEX.md`）：先請 Miles 選狀態列 A／B／C，再做 M1.5 紅外線。AGENTS.md 0.1 已開放 `tools/sim/`、產生器、`docs/design/`。
- 待辦：Miles 實機看一眼 ui_kit 版（應與前一版完全相同）。

### 2026-11-23｜M4-2 畫面定稿＋介面設計總則｜Claude Code CLI
- 做了什麼：體感畫面重做（`ui_airmenu.cpp`）。第一版自創磚塊版面被退件，改照介面規格與表情頁：狀態列「MODE 04・體感」＋電量、選單左上紅色「體感」大字（同 B 切模式）、六角徽章清單、等待頁雷達倒數、已連線雷達圈綠黃交錯＋準星跟游標位移、六角鍵（左／捲／右／停）按下亮起、底部黃黑斜紋帶、B 長按紅色倒數。固定字改 Noto Serif CJK TC Black 點陣（新 `tools/gen_air_labels.py` → `air_labels.h`）；裝置名稱與提示句用 efontTW_12 一般字。`air_mouse::takeMotion()` 只供畫面讀位移。
- 設計總則：新增 `docs/design/`（README 總則＋流程、COMPONENTS 元件目錄＋程式位置、img 各頁定案截圖與元件圖）；介面規格加第 D 節、第 5a 節換定稿截圖；執行計劃 M4 標 ✅；新交接 `docs/KICKOFF_M15.md`。
- 驗收結果：Miles 實機看過三輪後定稿。編譯 Flash 74.5%、RAM 40.4%，燒錄驗證通過。模擬 `bash tools/sim/build.sh air`。
- 遇到的坑／新規則：10px 中文黑字疊實心底會糊；閱讀用的字加粗（疊兩次）反而更糊；efont 沒有「・」。規則寫進 CLAUDE.md 鐵則十二第 21 點與第 9 節、`docs/design/README.md`。
- 下一步：合併 `codex/m4-2` → main；M1.5 IR1（`docs/KICKOFF_M15.md`）。
- 待辦：已連線頁重畫對游標手感的影響請 Miles 多用幾天觀察（覺得頓就降到 10 張／秒或關準星動畫）；跑 `bash cloud/persona.sh` 上傳 Friday 人設。

### 2026-11-23｜M4-2 功能驗收與 Claude 畫面交接｜Codex
- 驗收結果：Miles 確認目前韌體除 UI 與排版外，操作皆可。最新防誤拖版 PlatformIO 編譯通過，本機驗證短按手腕回彈被抑制且長按可拖曳。
- 交接：Claude Code 由同一份 `codex/m4-2` 工作樹接手排版，完成後補介面規格的最終圖。流程與既有預設見 [M4-2 交接](HANDOFF_M4-2.md)。
- Codex 下一步：等 Miles 確認可切換同一台測試機，再在 RF Session 接續 R1 實機驗收。M4-2 配對與按鍵邏輯留作畫面回歸基準。

### 2026-11-23｜M4-2f 空中滑鼠增益 30｜Codex
- 依 Miles 要求，增益預設由 50 降至 30。曲線指數 1.2、死區 1.0 度／秒。測試建置標記 31-m42f。
- 驗收：編譯及燒錄完成，esptool 已驗證寫入資料。重置後 Mac 未重新偵測到原生 USB 序列埠，裝置上的 `@` 設定回報待確認。

### 2026-11-23｜M4-2d／e 按鍵、離合器與返回畫面｜Codex
- 做了什麼：「體感」選單與綠底「已連線」。依 Scroll 有無顯示不同按鍵說明。Scroll 左鍵／拖曳與捲動，B 短按右鍵，無 Scroll 時 A 短按左鍵。A 離合器放開時依重力重校游標方向。
- B 長按約 0.8 秒顯示倒數，放開取消。按滿 3 秒先走原急停閘門，再切斷藍牙、返回體感選單。其他模式沿用原急停。
- 手感修正：Miles 回報新版靈敏度下降。核對 M4-1 的增益 25、線性 1.0、死區 1.0 均相同。每次連線先用原 M4-1 機身軸，主動放開離合器重校後才用重力軸，並降低靜態畫面比對頻率。
- Miles 調整韌體預設為增益 35、曲線指數 1.2、死區 2.0 度／秒。
- Miles 確認按鍵可操作，回報點擊時游標上飄。新版本把游標位移暫存 30ms，遇到按鍵邊緣丟棄這段位移，並保留既有 80ms 凍結。按鍵報告仍依原有節奏送出。 實機已可精準點選，偶有點擊被當成拖曳，補上左鍵須按住 200ms 才允許帶鍵移動。
- 驗收：PlatformIO 編譯與選單、等待、兩種按鍵卡、倒數的模擬渲染。實機待驗收左右與上下方向、螢幕朝下的離合器、短按與長按分流、B 三秒後重連及其他模式急停。完成後同步正式按鍵文件、介面規格 HTML 與 Friday 人設。

### 2026-11-23｜M4-2c 裝置選單與連線｜Codex
- 做了什麼：AirMouse 先顯示裝置選單，A 鎖定後才開藍牙，等待最多 30 秒。舊配對以已存的身分位址列出，讀到主機 GAP 名稱就存入 NVS。
- 新決議：Miles 接受採用連線加密後核對身分。非選中主機會短暫連入，核對後斷線，滑鼠輸出只給符合身分的主機。Miles 確認滿三台也要保留「臨時連線」，此模式不儲存配對或名稱。有空位才顯示「新增配對」，清單滿時拒絕新紀錄，保留原配對。主機有公開 GAP 裝置名稱時，按身分位址保存名稱。
- 實機回報：臨時外部連線成功。既有配對誤拒是把 NimBLE 原生位址交給會反轉位元組的建構式。日誌另見一筆配對消失及一次 NimBLE host task 當機。已更正位址建構、在不允許綁定時保留舊配對、讓斷線收尾和重新廣播錯開，並在有空位時提供「新增配對」。修正版待實機重驗。
- 後續驗收：Miles 已完成 iPhone 新增配對，日誌確認已綁定且訂閱滑鼠報告。選單、等待與已連線畫面依資訊頁的琥珀配色、細框及紅黑斜紋重畫，已編譯並以 M5GFX 渲染三種狀態。新版外觀與第四台臨時連線待實機確認。
- 下一步：實機驗收選單、既有配對讀回、名稱存取、滿額保留配對、逾時與非選中主機切換。接著做 M4-2d 按鍵與離合器、M4-2e B 倒數與返回。最後更新介面規格 HTML。

### 2026-11-23｜M4-2a Scroll 量測與資訊頁｜Codex
- 做了什麼：新增 Scroll 的累計轉動與按鍵讀取，序列埠用 `@rscroll1`／`@rscroll0`。資訊頁 CHIP 顯示時脈比例，保留原迴圈耗時波形。原 MEM 改成每分鐘耗電紫柱與累積綠線，USB 供電時保留圖表並暫停，拔線後從零累計新一段。
- 驗收結果：PlatformIO 編譯通過。Miles 實機確認 Scroll 可讀雙向計數和按下／放開，接受資訊頁版本。
- 新規則：Scroll 暫存器與極性已寫入 `CLAUDE.md` 鐵則一。實體方向對應正負號及每圈計數留到選單與捲動時核對。
- 下一步：原計畫先驗 NimBLE 白名單。Miles 後續選擇直接驗收 M4-2c 選單與加密後身分核對。

### 2026-11-23｜M4-0 31-m4o 實機：iPhone ✅、Mac 仍 Unused｜Claude Code（Cowork 橋接）
- Mac 「附近裝置」沒按鈕的真因：系統設定 App 卡住（31-m4l 那次轉圈留下的配對程序）→ `Cmd+Q` 系統設定重開就有按鈕。之前推的「Apple VID 快取」不是主因（換新位址也一樣沒按鈕）。
- Mac（31-m4o 預設＋`@s1`）：配對成功 → 約 60ms 後設定頁放手、`Unused` 斷線；Mac 沒讀任何 HID 特徵值。
- **iPhone ✅**（`@a1`＋`@f` 新位址）：要求加密 → 讀 PnP → 配對完成 → 讀 HID Information → 讀 Report Map → **訂閱滑鼠報告** → 連線參數 15ms／延遲 4 → `@1` 手機游標畫圓 ✅。→ HID 實作本身沒問題，是 Mac 端不接手。
- 讀取紀錄：主機在配對後讀的會印；Mac 配對前讀 DIS 那次沒印（NimBLE 對某些首次讀取不呼叫 onRead）。
- 線索：OpenSnek #147（Razer 滑鼠）同樣「配對成功 → 設定頁取消連線 → ~50ms 後 Unused 斷線」，原因是另一個用 CoreBluetooth 的 App 也連著裝置、干擾 HID 接手，刪 App＋重開機解決。我們的 Mac log 裡出現過 Brave 瀏覽器的藍牙配對 session。
- 下一步（Miles：Mac 是主要場景，先解決）：查「隱私權與安全性 → 藍牙」有哪些 App 有藍牙權限；重開機、不開其他 App 後再配一次；log 加看 `CancelPeripheral|session`。

### 2026-11-23｜M4-0 31-m4n 實機 → 31-m4o 換藍牙位址｜Claude Code（Cowork 橋接）
- Miles 實機：`@v0`（Espressif VID）、`@p0`（31-m4k 的舊設定全套）都還是沒有連線按鈕、序列埠沒有「已連線」；31-m4k 當時同樣設定有按鈕 → 問題在 Mac 端對這個位址（7C:E8:B1:E1:B5:39）的快取，裝置端怎麼改都看不到。「附近裝置」另一個 43PUH7406/96 是 Philips 電視（型號），與本案無關。
- 31-m4o：藍牙改用由晶片 MAC 衍生的隨機靜態位址（`cfg::BLE_ADDR_SALT`＝1，存 NVS），對 Mac 來說是一台全新裝置；`@a1` 再換一個、`@a0` 回原廠 public 位址。開藍牙時印出目前位址。

### 2026-11-23｜M4-0 31-m4m 實機 → 31-m4n 拿掉 Apple VID｜Claude Code（Cowork 橋接）
- Miles 實機（31-m4m）：Mac 藍牙關 5 分鐘以上、Trackpad 斷開，「附近裝置」仍沒有連線按鈕；序列埠完全沒有「已連線」＝Mac 根本沒嘗試連。廣播自 31-m4e 起沒改過，31-m4k 以前有按鈕 → 推測是 31-m4l 那次連線讓 Mac 快取了 Apple VID 0x05AC／PID 0（log：`Device lost … VID 0x05AC`），被當成 Apple 配件（要走 Apple 自家配對流程）而不給按鈕。
- 31-m4n：Apple VID 預設關（`@p1` 也不再開）；Mac 端用 `sudo pkill bluetoothd` 重啟藍牙服務清快取。
- 規則：不要用 Apple 的 VID（會讓 Mac 走 Apple 配件流程，且快取不易清）。

### 2026-11-23｜M4-0 31-m4l 實機 → 31-m4m HID 特徵值加密＋讀寫紀錄｜Claude Code（Cowork 橋接）
- Miles 實機（31-m4l 對照組設定、不主動要求加密）：Mac「附近裝置」點了沒反應；斷開 Trackpad 後才連上，但只連線不配對（63 秒仍未加密），Mac 端一直轉圈、無法忘記。Mac log：讀到 DIS（Apple VID）、標成 HID 裝置，之後沒有任何配對或讀取動作。
- 結論：不主動要求加密 → Mac 不配對；主動要求 → 配對完設定頁放手、Unused 斷線。兩種情況 Mac 的滑鼠驅動都沒接手。可疑點：NimBLE 2.x 的 NimBLEHIDDevice 把 Report Map／HID Information／Protocol Mode 建成不加密就能讀，HID over GATT 規範要求加密。
- 31-m4m：HID 服務特徵值加上加密（`@e`，預設開；用子類別取用 protected 的 setProperties，在 server start 前改）；記錄主機讀／寫了哪些特徵值（`[ble] 主機讀 Report Map…`），看 Mac 驅動到底有沒有開始讀。
- 待實機：同樣流程看序列埠有沒有「主機讀 …」，以及 Mac 會不會因讀到需要加密而自己發起配對。

### 2026-11-23｜M4-0 31-m4k 實機 → 31-m4l 對照組 HID 設定｜Claude Code（Cowork 橋接）
- Miles 實機（31-m4k，@i0/@i1/@i2 都試，每輪 @f＋Mac 忘記）：全部「配對完成 → Mac 斷（0x213）」。Mac bluetoothd log：Mac 自己強制 7.5ms（「LE Mouse at 7.5ms」系統設定）；配對成功後設定頁放手、`Disconnecting GATT for unused LE device`（原因 Unused）＝滑鼠驅動沒接手；關開 Mac 藍牙不會自動重連（看得到廣播也不連）。→ 連線間隔假設排除（CLAUDE.md 鐵則十二第 16 點）。
- 31-m4l：預設改成對照 HijelHID_BLEMouse（Apache-2.0，NimBLE 2.x，標 macOS 實測可用；只參考設定值、未複製程式碼）的 HID 設定：不帶 Report ID、Apple VID 0x05AC、HID 旗標 0x01、不主動要求加密。`@r/@v/@h/@s` 逐項切回、`@p1/@p0` 全套切；改了自動清配對並重建藍牙。
- 待實機：Mac 忘記此裝置 → AirMouse（預設就是對照組）→ Mac 連線，看有沒有「主機訂閱滑鼠報告」；成功再逐項切回找出關鍵那一項（決定要不要保留 Apple VID）；iPhone 也重測。

### 2026-11-23｜M4-0 31-m4k：連線間隔照 Apple 規範｜Claude Code（Cowork 橋接）
- 做了什麼：偏好連線間隔 7.5～15ms → 預設 15～30ms（Apple HID 指南最短 11.25ms，Mac 原本選 7.5ms 後秒斷）；配對完成 1.5 秒後間隔不在範圍就主動 `updateConnParams`（逾時 4 秒）；序列埠 `@i0/1/2`（7.5～15／11.25～15／15～30ms）、`@u0/1`；連上印主機選的間隔／延遲／逾時；修 `s_conn_t` 斷線沒歸零（重連後印錯的「連上 N 秒仍未加密」）。規則見 CLAUDE.md 鐵則十二第 15 點。
- 驗收結果：橋接環境沒編譯，待 Mac 編譯。
- 待實機：乾淨 Mac 測試——Mac 忘記此裝置 → AirMouse → `@f`（要看到 → 0 台）→ Mac 終端機跑 `log stream`（bluetoothd）→ Mac 連線；看主機選的間隔、有沒有「主機訂閱滑鼠報告」。還斷就試 `@i1`，再不行查 Mac log 線索（HID Information flags、Boot Mouse、PnP VID 來源、報告描述）。

### 2026-11-23｜M4-0 31-m4j 實機＋交接｜Claude Code（Cowork 橋接）
- Miles 實機（31-m4j）：**快速來回切 AirMouse／Desktop 正常了**。Mac 仍「配對完成 → Mac 主動斷（0x213）」：預設、`@c0`、`@c0 @k0` 都一樣（但都沒 `@f`，裝置端一直留著 2 台配對，不算乾淨測試）；Mac 選了 7.5ms 連線間隔（Apple HID 指南最短 11.25ms，下一個要試）。
- 交接：`docs/KICKOFF_M4-mac.md`（Mac 收尾 → M4-1）。

### 2026-11-22｜M4-0 31-m4h 實機 → 31-m4i｜Claude Code（Cowork 橋接）
- Miles 實機（31-m4h）：`@s0`（不主動要求加密）→ Mac 連上後 24 秒都沒開始配對 → 不行；`@c0`／`@k0` 還沒測。`@f` 回「失敗」（deleteAllBonds）。**資訊頁放約 30 分鐘後當機**：中斷看門狗，兩核卡在溫度感測器（`temperatureRead()` 與 Wi-Fi 省電互卡）。另：充電判斷「充飽」實機觸發 ✅（A4b 待辦完成）。
- 31-m4i：資訊頁不讀溫度（`INFO_CHIP_TEMP`）；`@f` 改 `ble_store_clear()`。
- Miles：快速來回切 AirMouse／Desktop 會當機重開（還沒 log）。31-m4j 推測修法：Wi-Fi 開關指令帶序號（`net_hub::radioSettled()`），藍牙要等最後一個關 Wi-Fi 指令真的做完才開（原本只看 `WiFi.getMode()`，舊的「開」指令還排在後面時會誤判）。
- 待實機：Mac `@s1`＋`@c0`、再加 `@k0`；Mac 端 `log stream` 抓 bluetoothd 斷線原因。

### 2026-11-22｜M4-0 31-m4g 實機 → 31-m4h（Mac 對照測試版）｜Claude Code（Cowork 橋接）
- Miles 實機（31-m4g）：**B 切模式全部正常**（連著主機離開不再當機；回 Desktop 過場等 2.6 秒連上中控、Friday 第一次就回）。Mac：跳出配對確認視窗，按同意 → 我們這邊「配對完成（已加密）」→ Mac 主動斷（0x213），沒走到「訂閱滑鼠報告」；要在 Mac 忘記裝置才能重連；Mac 會跳「設定藍牙滑鼠」視窗（認得是滑鼠）。
- 31-m4h：配對選項改成序列埠即時切，不用重燒就能對照：`@s0/1` 裝置主動要求加密、`@c0/1` Secure Connections、`@k0/1` 交換身分金鑰；配對完成印「綁定／驗證／金鑰長度」。
- 推測順序：(1) 我們主動要求加密跟 Mac 自己的配對流程撞在一起（Mac 要等使用者按同意，我們這邊 3 秒就開始等不到）→ 先試 `@s0`；(2) 舊式配對 `@c0`；(3) 不交換身分金鑰 `@k0`。

### 2026-11-22｜M4-0 31-m4f 實機 → 31-m4g｜Claude Code（Cowork 橋接）
- Miles 實機（31-m4f）：過場等資源 OK（進 AirMouse 等 41ms）；**Mac 能連了**（31-m4e 廣播 flags 有效），但連上、加密完成後 Mac 主動斷（0x213），已配對的 iPhone 隨即自動連上；**連著主機時按 B 離開 → 當機**（`host_task` PC=0；沒連線時離開正常，patch 有套上）。
- 31-m4g：`end()` 先停廣播、斷線、等斷線回呼再 deinit；配對交換身分金鑰（IRK）；印主機是否訂閱滑鼠報告、斷線原因文字。
- 待實機：iPhone 藍牙關掉後 Mac 連線能否維持；連著時切回 Desktop 不當機。

### 2026-11-22｜M4-0 31-m4d 實機 → 31-m4e｜Claude Code（Cowork 橋接）
- Miles 實機（31-m4d）：**不再當機**（離開 AirMouse 藍牙關閉 102ms、heap 回到 183KB；回 Desktop 按 A 正常）；**iPhone 配對成功**（已加密、連線間隔 15ms）。問題：(1) Mac 藍牙「附近裝置」看得到但沒有連線按鈕；(2) 回 Desktop 後要約 12 秒才連上中控（先試熱點 10 秒才換家裡），期間說的話連上後重送，偶爾沒回（log 看到重送前又按了 A，新的一輪蓋掉舊的）。
- 31-m4e：廣播 flags 加「不支援傳統藍牙」（Mac 推測原因，未證實）；Wi-Fi 重開先試關之前連著的那組。
- 31-m4f（Miles 提議）：切模式過場等新模式資源好了才進畫面（Desktop 等中控連上、AirMouse 等藍牙廣播），最多 15 秒；過場中 A 不理（CLAUDE.md 鐵則十二第 10 點）。
- 待實機：Mac 連線；回 Desktop 連線時間；iPhone `@1` 畫圓、急停、來回切。

### 2026-11-22｜M4-0 31-m4c 失敗 → 31-m4d 修補 NimBLE deinit｜Claude Code（Cowork 橋接）
- Miles 實機（31-m4c，保留藍牙）：離開 AirMouse 不當機了，但回 Desktop 後 Wi-Fi 重連或按 A 錄音就 abort：`Should enable WiFi modem sleep when both WiFi and Bluetooth are enabled`（`pm_set_sleep_type` ← `wifi_set_ps_process`）。另：Mac 藍牙「附近裝置」看得到 M5S3Pack 但點不了連線（log 沒有 `[ble] 已連線`）。
- 31-m4d：改回離開就 deinit，用建置前腳本 `scripts/patch_nimble.py` 修 NimBLE deinit 競態（stop 後等 100ms）；`net_hub::setPsNone()` 藍牙開著時不設 PS_NONE（保險）。
- 待實機：建置 log 要看到 `[patch_nimble] 已修補`；來回切 5 次、回 Desktop 按 A 對話；Mac 連線時的 log。

### 2026-11-22｜M4-0 離開 AirMouse 當機 → 31-m4c｜Claude Code（Cowork 橋接）
- Miles 實機（31-m4b，log 有當機解碼）：進 AirMouse 正常（Wi-Fi 20ms 關好、藍牙 12ms 開始廣播、heap 107KB），**按 B 離開時必當機重開**：`InstrFetchProhibited, PC=0`，在 `NimBLEDevice::host_task`（`NimBLEDevice.cpp:882`）。原因：`deinit()` 停掉 NimBLE 後，host task 返回去呼叫 `nimble_port_freertos_deinit()`，跟主迴圈的 `nimble_port_deinit()` 同時收尾。「連到一半重開」應該也是這個（還沒實際配對）。
- 31-m4c：`BLE_DEINIT_ON_EXIT`＝false，離開只斷線＋停廣播，藍牙堆疊保留（CLAUDE.md 鐵則十二第 7 點，鐵則二的例外）。
- 待實機：來回切模式不當機；回 Desktop 後 Friday 對話與上傳正常；Mac 配對、`@1` 畫圓。

### 2026-11-22｜M4-0 實機第一輪 → 31-m4b 診斷版｜Claude Code（Cowork 橋接）
- Miles 實機（31-m4a）：NimBLE 2.5.0 在 Arduino 2.0.17 **編譯通過**（firmware.bin 約 2.45MB）；但連線到一半裝置自己重開，或停在 CONN.. 不動。沒有 log。
- 31-m4b 改：(1) 開機印「上次重開原因」（PANIC／WDT／BROWNOUT…）；(2) `monitor_filters` 加 `esp32_exception_decoder`，當機時直接印函式名稱；(3) NimBLE host task 堆疊 4096→8192（Secure Connections 配對的橢圓曲線運算可能撐爆）；(4) 進 AirMouse 先等 Wi-Fi 真的關掉（最多 2 秒）再開藍牙；(5) 連上後由裝置主動要求加密，連上 3 秒還沒加密每 3 秒印提醒；(6) 修 onConnect 位址字串（原本在 NimBLE task 配 std::string）；`cfg::BLE_SECURE_CONN` 可切舊式配對對照。
- 待實機：重配前 Mac 先「忘記此裝置」＋ `@f`；貼切到 AirMouse 到出事為止的完整 log。

### 2026-11-22｜M4-0 BLE 滑鼠最小版（31-m4a）｜Claude Code（Cowork 橋接）
- Miles 定案：連線優先 Mac → iPad → iPhone；沒插 Scroll 時 A 短按左鍵、按住拖曳；拿起來游標就動（規則見 CLAUDE.md 鐵則十二）。
- 做了什麼：新增 `ble_mouse.*`（NimBLE-Arduino 2.5.0、自寫 HID 描述、bonding＋Secure Connections、廣播「M5S3Pack」、外觀＝滑鼠、急停閘門）；AirMouse enter／exit 關開 Wi-Fi 與 ES8311（`audio_io::suspend／resume`）；`power_save` 不在 AirMouse 重連 Wi-Fi；狀態畫面加 BLE 狀態；序列埠 `@`／`@1`／`@0`／`@f`；版本標記 31-m4a；`THIRD_PARTY.md` 登記 NimBLE。
- 驗收結果：雲端環境連不到 PlatformIO 套件伺服器，**沒編譯過**；API 已對照 NimBLE-Arduino 2.5.0 原始碼。
- 待實機：Mac 編譯（含 Flash 用量）；Mac 配對、`@1` 畫圓；B 來回切 5 次（看 `[ble] 藍牙關閉｜heap`）；AirMouse 中 B 3 秒急停；回 Desktop 後 Friday 正常；iPad／iPhone 配對。
- 下一步：M4-1 陀螺儀 → 游標手感。

### 2026-11-22｜P2 ✅＋順序調整＋RF 交給 Codex＋改 AGPL｜Claude Code（Cowork 橋接）
- 驗收結果：Miles 實機走完 KICKOFF_P2 第 6 節 14 項、`persona.sh` 已上傳 → **P2 通過**（30-p2k）。P2-6 續航對照留待之後量（步驟見 `docs/KICKOFF_P2-close_M15.md`）。
- PINGEQUA 11-22 回信：承認 Bruce 軟體端還有不少 bug，建議到 Bruce GitHub 開 issue，他們也會轉給作者；**案件保持開啟、換貨權益保留**。我們的 0.5% SPI 錯誤是用自有 `rssi-test` ＋同一份 SmartRC 驅動量到的，issue 要寫清楚這點（Codex R0 起草）。
- 決議（Miles）：(1) 開發順序改 **M4 → M1.5 → M2**；(2) **M3 RF 軌交給 Codex** 在 `rf-lab/` 平行開發，照根目錄 `AGENTS.md` 的規則與 `rf_tool` 介面交件，Claude Code 併回；(3) repo 改 **AGPL-3.0-or-later**，第一版完全開源、之後版本可只自用（交給別人的版本若含 AGPL 程式碼須附原始碼），允許從 Bruce 複製協定解碼／編碼、`.sub`、頻譜資料處理（SPI 層照鐵則五重寫）。
- 做了什麼：新增 `LICENSE`、`THIRD_PARTY.md`（現有函式庫授權盤點）、`AGENTS.md`（Codex 範圍、硬性規則、Bruce 複製規則、`rf_tool` 介面、R0～R6）；README 加授權段；CLAUDE.md 鐵則十一標 ✅、第 6 節順序、第 8 節授權；執行計劃 md／html、KICKOFF_P2 狀態。
- 注意：只有一台機器，燒 `rf-lab` 前要先 `q0` 進 root（看門狗）；已寫進 AGENTS.md。
- 下一步：M4 空中滑鼠查證與計畫。
- 待辦：收費課程上架前檢查 EVA 致敬元素（「初號機」浮水印、NERV 配色、「受信」）是否要換成自有風格；課程中提醒 NCC 低功率電波規定。

### 2026-11-22｜資訊頁 MEM 調整（30-p2i～k）＋交接｜Claude Code（Cowork 橋接）
- Miles：MEM 長條改紫色；那條紅色「開機以來 heap 最低線」改綠色 1px（我第一版誤改成 CHIP 波形綠色加粗，已還原）；長條不再從 0 畫起、只放大最低值附近（最低線約 56% 高），原本柱子頂天、最低線貼柱頂看不出變化。
- 交接：執行計劃、介面規格（3c、3d）、記憶已更新；下一個 session 用 `docs/KICKOFF_P2-close_M15.md`。

### 2026-11-22｜睡眠喚醒統一（30-p2g／30-p2h）｜Claude Code（Cowork 橋接）
- 進入睡眠 10 秒自動關螢幕（表情頁約 21 秒、資訊頁約 40 秒）。短按 A「有時會亮」查出最可能是睡著變暗（背光 12）時短按 A 照常開錄、背光全亮 → Miles 定案：睡著變暗與關螢幕統一，短按 A 不理、按住 1 秒醒來（不錄音）。

### 2026-11-22｜A8 持續晃動（30-p2f）｜Claude Code（Cowork 橋接）
- Miles：一直晃要一直有液體／碎片滾動，3 秒沒晃才變回表情；原本只有第一下有反應。改：甩著時碎片重力打五折＋亂數踢力、液體甩著時步速加倍；拼回改成最後一次晃動後 3 秒。關螢幕時按住 A 改 1.5 秒（30-p2d）、資訊頁也自動關螢幕（30-p2e）。
- 注意：插線＋搖原本就 26～32ms（Miles 接受），液體步速加倍後可能再多一點，實機 `j` 看「迴圈≤」。

### 2026-11-22｜P2 實機回饋修正（30-p2c）｜Claude Code（Cowork 橋接）
- Miles 30-p2b 實機：關螢幕後短按 B 會亮、短按 A 偶爾會亮 → 改成只有長按 A、單按電源鍵會亮（A 改看實際按鍵狀態）；斷線不要右上角 Wi-Fi 圖示 → 跟 ROOT 一起改成狀態列電量左邊的標籤（模擬器確認三種拿法都放得下）；新增：沒插線時表情睡著 30 秒自動關螢幕。

### 2026-11-22｜P2 長按畫面定案（30-p2b）｜Claude Code（Cowork 橋接）
- 30-p2a 實機：`q` 讀得到電源鍵（按著＝1）、長按有計時畫面、燒錄不被看門狗打斷、`q9` 約 60 秒重開 ✅。
- 長按畫面經模擬圖 4 版定案（A EVA 斜紋＋七段／B 六角框 → 混合 → 縮小置中 → 斜紋紅黃閃）：四邊斜紋紅黑／黃黑 0.4 秒交替閃、六角框外圈進度、框內大字＋一位小數秒數。`gen_kanji.py` 加 `pk_power／pk_stop／pk_root`（既有點陣不變）。電腦上用真 M5GFX 跑韌體繪圖程式驗過畫面，介面規格加第 3d 節。
- P2-0 基準（29-a4g）：放桌上 26 分鐘 −15%（約 0.58%／分，推估滿電 2.9 小時）。

### 2026-11-22｜P2 電源鍵新機制＋省電（30-p2a）｜Claude Code（Cowork 橋接）
- 討論定案（11-21～22）：電源鍵新機制（單按開關螢幕、3～5 秒放開關機、5 秒 root、五連按強制關機、PM1 看門狗）、root＝原廠按鍵、關螢幕時只有長按 A 叫醒、睡著或關螢幕 3 分鐘斷 Wi-Fi、插線永不睡不斷線、畫面電量 0% 時（實際 3%）關機。A／B 鍵與 Scroll Unit 不動。
- 做了什麼：新增 `power_key.*`；`power_save` 加螢幕開關與斷 Wi-Fi 計時；`net_hub::setRadioOff`；`imu_pet` 關螢幕不反應、插線不睡、`wake()`；`battery_ui` 畫面電量換算與 3% 關機；`main` 關螢幕時 A／B 規則、急停亮螢幕；序列埠 `q`。Friday 人設加電源鍵與睡眠規則（`friday.md`，要跑 `persona.sh`）。規則寫進 `CLAUDE.md` 鐵則十一。
- 驗收結果：VM 只做了語法檢查（stub 標頭），**Mac 還沒編譯**。
- 待實測：PM1 `0x48` bit0 極性；燒錄中會不會被看門狗重置；`M5.Power.powerOff()` 在 StickS3 真的斷電；插著 USB 關機會不會自己開回來。

### 2026-11-21｜A8／A4b ✅ 驗收通過（29-a4g）｜Claude Code（Cowork 橋接）
- Miles 實機：29-a4g 三項（4 秒拼回、拔線淡出、d0／d）都正確 → A8、A4b 通過，M1-A 11／11。

### 2026-11-21｜A8／A4b 修正（29-a4g）｜Claude Code（Cowork 橋接）
- Miles 實機：碎片停手後改 4 秒才拼回；拔線後背景液體卡住；`d0`／`d` 之後一直顯示充飽。
- 修正：插拔結束 `d` 假裝、電壓未收滿 5 分鐘一律充電中、液體淡出用真實時間（斷線定格時也走得完）；`d` 印目前狀態。B5：Friday 快照餘額單獨一行；餘額重新校正 $4.01。
- 待實機：燒 29-a4g，`d2`→拔線液體 0.6 秒淡出；插線→`d2`→`d`→回充電中；`d0` 液體淡出。

### 2026-11-21｜A8／A4b 實機第二～四輪（29-a4d～29-a4f）｜Claude Code（Cowork 橋接）
- 做了什麼：29-a4d 液體直接寫畫布緩衝；29-a4e 流動改查表、每幀最多一步、`j` 加「液體≤」；29-a4f 找到真正主因是部分更新面積 → 充飽單顆閃、重力穩定 1.5 秒後不側滑、亮波用參考重力。
- 驗收結果（29-a4f，插線 47～48%）：液體本身 1.8～4.1ms ✅；全速靜止 20～26ms、思考 16～20ms、待機 26～29ms（30fps 預算 33ms）✅；`d2` 充飽靜置同待機 ✅；拍打、拿起、想睡切換正常。插線＋用力搖 26～32ms ⚠️ → **Miles 選接受**（約 33fps，看不出差別）。
- 遇到的坑：見 `CLAUDE.md` 鐵則九第 17 點「液體每幀只能在格點時改」。另一份 log 有一次語音失敗：中控連線半死（33 秒沒 pong、RSSI −75）、上傳 0 位元組、重連 `start_ssl_client -1`，跟畫面無關；常發生再查。
- 下一步：A8／A4b 最後驗收（真的充飽、拔線）；B5 驗收；之後 M1.5 紅外線。

### 2026-11-21｜A8／A4b 實機第一輪＋調整（29-a4d）｜Claude Code（Cowork 橋接）
- 實機（29-a4c）：沒插線時甩碎 `j` 迴圈 15～22ms ✅；`d2`（液體在）時 30～57ms ❌ → 液體改直接寫畫布緩衝（`fillHexFast`，鐵則九第 17 點），模擬每幀繪圖 91→54µs。
- Miles：拼回太快 → 停手後碎片先散 2.5 秒再拼回（說話立刻拼回）；充飽只七成 → 液體量改成電量 0～100% 對應容器 8%～92%；餘額 platform 顯示 $4.14 → 加 `BALANCE_USD`／`BALANCE_SET` 校正（鐵則七第 13 點）。
- 其他：log 開機時家裡 Wi-Fi 第一次握手回 202 後裝置重開過一次（同 a7o 偶發 202；這次有重開，再出現要抓完整 log）。
- 待實機：插線（或 `d1`／`d2`）時 `j` 迴圈要 ≤25ms；部署中控後資訊頁餘額 $4.14、Friday 問餘額答 4.14。

### 2026-11-21｜M1-A / A8 甩碎＋A4b 充電液體＋B5 簡化版（實作完成，待實機驗收）｜Claude Code（Cowork 橋接）
- 做了什麼：
  - **A8（28-a8a）**：先出「搖晃液體」模擬，Miles 看完改成**不充電時不用液體**——彈珠台一觸發，眼睛、嘴巴、符號炸成邊長 5px 小六角，受 IMU 視重力＋甩的力道滾、撞牆、互撞（像骰子／彈珠），停手 0.4 秒後彈簧拼回（約 0.5 秒）。`imu_pet` 每筆送 `setGravity`。規則見 `CLAUDE.md` 鐵則九第 16 點。
  - **A4b（29-a4b）**：Miles 否決閃電眼／液位眼／蜂巢充能三款提案，定「充電／充飽採用背景液體」——插著線背景六角格裝液體（量＝電量×容器 6 成）、跟傾斜即時流；充電中亮波往上爬，充飽變亮＋笑眼；休息臉換「充電／充飽」標籤與閃電／星芒徽章。偵測：插線看 USB 輸入（去抖 2 秒），充飽看 5 分鐘電壓（≥4.10V 且上升 <15mV，<4.05V 退出），不看 CHG_STAT。插著線背光最低停在想睡那級。序列埠 `d0/d1/d2/d`。`ui_labels.h` 加「充電」「充飽」。規則見鐵則九第 17 點。
  - **B5 簡化版（29-a4c）**：裝置送機器狀態（`{"t":"dev"}`：電量、充電、電壓、版本、開機時間、Wi-Fi 家裡／熱點＋RSSI，不送 SSID）；中控 `InfoHub.briefing()` 在 GPT-Live session 開始時把「機器與即時資訊」快照接進 Friday 指示（額度已用／上限／剩餘、輪數、帳上餘額、本機狀態、天氣、中央社 6 則）。說「播新聞」照標題念、不上網查。規則見鐵則七第 13 點。`cloud/persona/friday.md` 已更新。
  - **評估 B5 推播／B6（只評估）**：B5 定時推播要加「中控主動發起一輪」的路徑（1～1.5 片，不建議用 gpt-4o-mini-tts：預定 2027-01-06 退役）；B6 MCP＝在現有 Responses 委派 `tools` 加 `mcp`（GPT-Live 官方支援，remote MCP 不另收費），MCP 伺服器可放 Worker，約 1 片，要先定工具清單；B6 喚醒詞：ESP-SR 沒有自助訓練（TTS 版由樂鑫訓練、付費客製要 2 萬筆語料），microWakeWord 可自訓但要移植出 ESPHome，兩者多半要升 Arduino 3.x／IDF 5（牽動鐵則五第 6 點 RF 重驗）、常駐收音與 P1 省電衝突，2～4 片、高風險。
- 驗收結果：模擬器（真 M5GFX 核心＋ASan）直式／橫式無錯；部分更新：甩碎平均 23～36%、充電靜置約 10%、充飽時搖約 40～50%。中控 `node --check` 通過、`briefing()` 用假資料跑過。**韌體未在 Mac 編譯、未實機**（橋接環境連不到 PlatformIO 套件伺服器）。
- 待實機驗收：
  - A8：用力搖 → 五官碎成小六角滾到下方；翻過來 → 掉到新的下方；往右傾 → 往右（反了改 `FR_GRAV_SIGN_X`）；停手約 1 秒拼回；`j` 全速迴圈 ≤25ms；說話中不觸發；B 急停照常。
  - A4b：`d1` 液體出現、傾斜會流、亮波往上爬；`d2` 變亮＋笑眼＋「充飽」；`d0` 淡出；實際插線 2 秒內出現；充滿後 5 分鐘以上判充飽；拔線回原表情；CHG_STAT 跳動時畫面不閃。
  - B5：先 deploy 中控；問 Friday「今天額度還剩多少」「電量多少」「在充電嗎」「播新聞」「韌體版本」。
- 遇到的坑：液體亮度每幀變化會讓部分更新整片重送（14%→50%）→ 亮度只在走步／亮波格點改。`handleCmd` 在 Wi-Fi 清單之前，改用前置宣告的 `netTag()`。
- 下一步：Miles 燒錄驗收 → 標 ✅（M1-A 11／11）→ M1.5 紅外線。B6 要做的話先定 MCP 工具清單。

### 2026-11-21｜M1-A / A7 外觀統一驗收通過＋交接｜Claude Code（Cowork 橋接）
- 做了什麼：A7 標 ✅（27-a7a～a7o：單一 NERV 資訊頁＋單一像素表情頁、六角符號徽章、初號機浮水印、受信對話框、低電量閃爍、背景快取＋螢幕部分更新、Wi-Fi 家裡優先）；A5-2、P1 一併標 ✅。執行計劃 md／html 更新（M1-A 9／11 片、新增 A8 列）；介面規格新增**第 0 節 A7 定案畫面**（模擬器實際輸出）；畫面模擬器搬進 repo `tools/sim/`（`build.sh face|info|anim|lowbat|bench`）；寫 `docs/KICKOFF_A8_A4b.md`。
- 驗收結果：27-a7o 實機——全速 16～18ms、開心 18～21ms；開機直連家裡 Wi-Fi。
- 27-a7p（Miles）：資訊頁供電磚「內部／外部」移到最右邊，電量、額度、十格條在左。
- 下一步：新 session 貼 `docs/KICKOFF_A8_A4b.md`：A8 搖晃液體符號、A4b 充電表情（樣式配合像素臉重新提案），並評估 B5 新聞快報、B6 Hey Friday＋MCP。

### 2026-11-21｜A7：背景快取＋天氣磚重排（27-a7g）｜Claude Code（Cowork 橋接）
- 27-a7f 實機：Flash 66.7%；`j` 迴圈 37～38ms（待機／生氣，240MHz）超過 25ms 預算 → 背景（六角格＋浮水印）改快取、嘴巴縫改整列讀（規則見 `CLAUDE.md` 鐵則九第 14 點）。模擬每幀繪圖 70～86µs → 24～32µs（PC 上的相對值）。
- Miles：資訊頁天氣內容跟框不合 → 左欄放大圖示、右欄溫度＋描述（雨絲不再壓字）、濕度靠左／降雨機率靠右撐滿框寬。
- 27-a7h（Miles 實機看完再改）：圖示＋氣溫一列、描述一列集中在左邊；右邊一小欄（照數字實際寬度）上下放濕度、降雨機率，中間細分隔線。
- 27-a7i（Miles）：對話框加紅色警示斜紋（頁籤右側＋底邊），直式對話區高 +4px。
- 27-a7j（Miles）：天氣圖示全彩；Friday 回答綠字；「受信」改原生點陣字；低電量 <20% 臉黃紅閃、<5% 紅灰閃（序列埠 `c15`／`c3` 驗收，`c` 恢復）。
- 27-a7k（Miles）：<20% 改原本琥珀↔紅閃（黃色不好看）；Wi-Fi 加家裡那組、同時掃到時家裡優先（`CLAUDE.md` 鐵則七第 12 點；密碼只在 secrets.h）。
- 27-a7l：Wi-Fi 換家裡時上傳還沒送完也不換。27-a7n／a7o：沒連上時掃描在實機一律失敗 → 改成照順序輪流試（`CLAUDE.md` 鐵則七第 12 點）。**27-a7o 實機通過**：開機直接連上家裡（第一次握手偶爾回 202，馬上重試就連上）；a7n 的 log 也證實「已連線時掃描正常（掃到 18 個）、沒連上時掃到 0 個」。**部分更新實機**：`j` 全速（生氣）16～18ms、開心 18～21ms（a7f 時 26～28ms）。27-a7m：實機 `j` 表情頁全速 26～28ms、Miles 覺得卡 → 螢幕**部分更新**（只送有變的矩形，`CLAUDE.md` 鐵則九第 3 點），預估全速一幀省約 10ms；之後液體滾動也靠這個。
- 待實機：`j` 迴圈要回到 ≤25ms；天氣磚直式／橫式。

### 2026-11-21｜A7：表情符號改六角徽章（27-a7f）｜Claude Code（Cowork 橋接）
- Miles 參考 `~/Desktop/nerv` 的 EVA 六角介面：表情符號全部改成對齊背景格的正六角徽章（規則見 `CLAUDE.md` 鐵則九第 14 點）；驚嚇改水滴、生氣「! WARNING」、出場閃爍＋彈簧＋疊影；浮水印那塊不畫格子。
- 試過被否決：七格花徽章、五官六角像素（原型留在模擬圖 `docs/lab/a5b-sim/hexsym.png`、`hexpix.png`，未進 repo）。
- 驗證：模擬直式／兩種橫式、動畫逐格（`hexanim.gif`）；**待 Mac Build 與實機**（量 `j`）。
- 下一片（Miles 提）：搖晃時符號炸成亮格、像液體跟著重力在六角格上流動（流沙式格子模擬），排在這版定案之後。

### 2026-11-21｜A7：表情浮水印、受信頁籤、餘額紅字（27-a7e）｜Claude Code（Cowork 橋接）
- 27-a7d 實機 Build 通過（Flash 66.6%），像素臉燒錄後正常說話。
- Miles 11-21：表情背景加淡浮水印（參考 EVA 初號機字樣；大字改「初號機」（Miles：第一台機器，致敬），用自己的點陣字、不照抄 EVA 標誌）；對話框「受信」改成左上凸出頁籤（點陣圖，不再糊）；資訊頁語音額度「餘」改紅字。`ui_labels.h` 新增 `rx_head`、`wm_big`。
- 驗證：模擬五種表情／直橫三版＋資訊頁無錯（`docs/lab/a5b-sim/a7e.png`，未進 repo）；**待 Mac Build 與實機**（順便量 `j` 迴圈時間）。

### 2026-11-21｜A7：刪除 EVA 角色、狀態大字改底色標籤（27-a7d）｜Claude Code（Cowork 橋接）
- 27-a7b 超過 app 分割區（4.08MB）→ 27-a7c 狀態大字與「台北時間」改點陣圖（`ui_labels.h`，規則見 `CLAUDE.md` 鐵則九第 15 點）。
- Miles 11-21：EVA 角色那套刪掉，以後只有一個表情頁、一個資訊頁 → 刪 `ui_persona.*`、序列埠 `q`；本機的 `firmware/persona_local/`、`firmware/src/persona_assets.h`（不在 repo）已不再使用，可自行刪除。狀態大字改成實心底黑字標籤。
- 驗證：模擬 ASan 無錯；**待 Mac Build 確認 Flash 用量與實機**。

### 2026-11-20｜A7 外觀統一：表情頁像素臉（實作完成，待驗收）｜Claude Code（Cowork 橋接）
- 做了什麼：Miles 給參考圖，向量臉改像素版（規則見 `CLAUDE.md` 鐵則九第 14 點）；第二輪 Miles 要求「嘴巴與原本表情、符號整個照搬」「表情不要蓋到左上的字」→ 五官與符號全保留、改淡琥珀無光暈，大字與狀態列最後畫並塗黑底、符號限位。角色臉套同一套外框。對話框兩版統一 EVA「受信」版。`AUDIO_BUILD_TAG` 27-a7b。
- 我先定的（Miles 未回、可改）：紅黑警戒線先不加（參考圖沒有）；驚嚇維持琥珀、只有警報（生氣）變紅。
- 驗證：模擬 9 表情 × 3 拿法（含對話框開合）＋角色臉，ASan 無錯、-Wall 無新警告。**未在 Mac 編譯、未實機**；六角格每幀約 400 條線，迴圈時間待量。
- 下一步：Miles 燒錄 27-a7b，看資訊頁＋表情頁、`j` 迴圈時間 → 細調 → A4b 充電表情（`KICKOFF_A4b.md` 待寫）。

### 2026-11-20｜A7 外觀統一：資訊頁（實作完成，待驗收）｜Claude Code（Cowork 橋接）
- 做了什麼：A5-2 實機驗收通過後（天氣字擠 → 改圖示＋細體數字，26-a5d），Miles 要求兩套外觀統一、主色改橘。用模擬逐版跟 Miles 定版面（6 版，圖在 `docs/lab/a5b-sim/`，不進 repo），定案後實作：`ui_info.cpp` 改寫為單一版本，`ui_info_eva.*` 刪除。版面與規則見 `CLAUDE.md` 鐵則九第 13 點、`ui_info.h` 檔頭。`AUDIO_BUILD_TAG` 27-a7a。
- 驗證：模擬（真 M5GFX 核心＋`INFO_SIM`）直式／兩種橫式 × 正常／斷線／極端值 0 次超出、ASan 無錯、-Wall 無新警告。**未在 Mac 編譯、未實機。**
- 下一步：Miles Build／Upload 看實機（特別是斜體時鐘清不清楚、迴圈時間）→ 表情頁統一（外框 NERV＋紅黑警示線、對話框兩版都用 EVA「受信」版）先出模擬圖 → A4b 充電表情。
- 待辦：A5-2 正式標 ✅、`KICKOFF_A4b.md` 尚未寫（排在表情頁統一之後）。

### 2026-11-20｜M1-A / A5-2 資訊頁雲端資料（實作完成，待驗收）｜Claude Code（Cowork 橋接）
- 做了什麼：中控新增 `cloud/src/info.js`（`InfoHub`）：天氣＝中央氣象署鄉鎮預報 F-D0047-0xx（secret `CWA_KEY`、`WX_TOWN`，新舊兩種 JSON 都吃）；快訊＝中央社 RSS（`NEWS_FEEDS`，合併去重取最新 6 則）；語音額度改美金——今日花費＝GPT-Live `usage.seconds`×$0.05/60（開著的 session 先用經過時間估）＋搜尋 $0.01／次＋gpt-5.6-luna token（$0.20／$1.20 每百萬，11-20 查官方），每日上限 `DAILY_MAX_USD`（預設 2，取代 `DAILY_MAX_AUDIO_SEC`；`DAILY_MAX_TURNS` 留作保險）；餘額＝`CREDIT_USD`（10，自 2026-11-16）－Costs API（secret `OPENAI_ADMIN_KEY`，到昨天 UTC）－中控記的未涵蓋天數。Cron Trigger 每 30 分鐘（`worker.js scheduled` → DO `/cron`），裝置連上、每輪結束、換日、`info_req` 也送；`/info?t=` 查快取（`&refresh=1` 強制）。裝置新增 `info_data.*`、`ui_wxicon.h`；`net_hub` 解析 `info` 訊息（只存不重畫、不叫醒）、序列埠 `a` 要中控重送；q0 時鐘第 4 行額度（直式短版、橫式長版、比例變色）、天氣磚（圖示＋描述＋溫度／濕度／降雨）、快訊磚（直式 2 則／頁、橫式 2 則、每 8 秒換頁，沒資料才畫地平線）；q1 EVA 直式 CHART 34→20、記憶體與連線各併一行，加天氣／額度／快訊各一行；橫式儀表欄加額度、數據欄 CHART 40→28 加天氣、快訊兩行。已存訊號數不放（M1.5／M3 再加）。`AUDIO_BUILD_TAG` 26-a5b。`friday.md` 已更新。
- 驗證（橋接環境）：中控邏輯 node 測（CWA 新舊格式、圖示、地點對照、RSS 解析去重、餘額、記帳只記一次、超過美金上限拒絕、訊息長度 < 300 bytes）全過；裝置 `info` 解析 g++＋ASan 測過；畫面用 `LGFX_LINUX_FB` 編譯真 M5GFX 核心模擬「q0／q1 × 3 種拿法 × 無資料／正常／斷線／極端值」共 32 張，0 次超出磚塊、ASan 無錯（圖在 `docs/lab/a5b-sim/`，不進 repo）。第一版小字中文用 efontTW_10_b 糊成一塊 → 改 efontTW_10。**沒有在 Mac 上編譯、沒有實機、中控沒部署**；CWA／中央社／Costs API 的真實回應格式要部署後用 `/info` 確認。
- 下一步：Miles 設 secret（`CWA_KEY`、`WX_TOWN`、`OPENAI_ADMIN_KEY`）→ `npx wrangler deploy` → 開 `/info?t=…&refresh=1` 看三種資料 → Build／Upload（順便驗 P1）→ 驗收（見 KICKOFF_A5-2）→ 通過後寫 `KICKOFF_A4b.md`、執行計劃標 ✅。
- 待辦／未決：餘額誤差（Costs 延遲、本機估計）待跟後台對；睡著時 CPU 80MHz 下資訊頁重畫時間待量。

### 2026-11-20｜P1 省電＋外觀微調（先採用）｜Claude Code（Cowork 橋接）
- 起因：充飽拔線 21 分鐘掉到 67%（續航約 1～1.5 小時）。Miles 選先做軟體省電、不買電流表，以體感＋`h` 電壓紀錄驗證；接受待機時 Wi-Fi 深度省電。
- 做了什麼：新增 `power_save.*`（規則見 `CLAUDE.md` 鐵則十）——表情頁待機 30fps／想睡 10fps／睡著 5fps，背光 110／45／12（變暗慢慢降、變亮立刻），睡著 CPU 80MHz，想睡／睡著 Wi-Fi `MAX_MODEM`，主迴圈兩張畫面之間最多睡 10ms；語音、情緒反應、換頁、過場一律全速；資訊頁套同一套背光／CPU／Wi-Fi 分級（Miles 要求），但用靜置時間判斷：10 秒變暗、30 秒微光＋降頻。`net_hub::setIdleDeepSleep`、`voiceActive` 改成三段。Grove 沒配件關 5V。序列埠 `z`／`z0`／`z1`、`l<n>`。外觀：眼睛光暈 10／6.5／3.5 → 7／4.5／2.5px（Miles「光暈有點太大」）；合成波資訊頁 NET LINK 深紫 → 洋紅 `#FF3FD0`（跟 CHIP 藍分不出來）。`AUDIO_BUILD_TAG` 25-p1。基準版本先提交 `7fe58d6`。
- 驗收結果：**先採用（Miles 11-20「省電先這樣、之後再優化」）**。Mac 第一次編譯 `imu_pet.h` 少 `<stdint.h>` 失敗、已修；完整實機驗收排在 A5-2 開工第一件事（`docs/KICKOFF_A5-2.md`）。同步更新執行計劃 md/html（新增 A6 EVA 風、P1 兩列）、介面規格（第 3b 節 q0／q1 與 B 過場、第 3c 節省電畫面）、KICKOFF_A5-2（兩套資訊頁都要顯示雲端資料）。
- 下一步：燒錄驗收（見下）→ A5-2。
- 待辦／未決：A5-1 最後一版的錄音中迴圈時間（Miles 晚點測）；`cad/reference/` 是賣家商品照與來源不明的外殼 STEP，先不進公開 repo（待 Miles 決定）。

### 2026-11-20｜UI EVA 風：切模式過場＋本機角色＋EVA 資訊頁｜Claude（Cowork 橋接）＋Codex
- 做了什麼：Miles 定案 EVA 風資訊頁與切模式過場（提案圖兩張，琥珀橘主色、黃黑條紋、六角格、七段數字、明朝體大字）。實作 `ui_modeswitch.*`：B 短按先把過場推上螢幕，再跑 `mode_manager::next()`，進度條在資源好之前停 25%、好了再跑 `UI_MODESWITCH_MS`（450ms）；進 Desktop 用 `ui_fx` 交叉淡化到表情頁；急停 `cancel()` 不走過場。`mode_manager::peekNext()`。`tools/gen_kanji.py` → `kanji_bitmaps.h`（語音／外部操作／射頻／體感／緊急停止／內部／外部）。本機角色管線：`tools/sprite2h.py`（PNG 序列 → 8-bit 調色盤＋索引、`--demo` 測試素材、`--clean`）、`ui_persona.*`（`__has_include("persona_assets.h")` 才啟用，整數倍放大＋取景框，接在 `ui_expression::render` 版面計算之後，對話區照舊）、序列埠 `q0/q1`、`.gitignore` 加 `firmware/persona_local/`、`firmware/src/persona_assets.h`。規格與交接 prompt：`docs/persona-swap.md`。
- Codex 後續（本機套用、11-20 晚）：Desktop 風格 `q0` 合成波／`q1` EVA（初號機像素角色＋獨立 EVA 資訊頁 `ui_info_eva.*`），有素材時開機預設 q1、沒素材固定 q0（Miles：保留機制，未來可能有 q2、q3…）。EVA 對話框（黑底琥珀框、警戒斜紋、「受信 // FRIDAY」，使用者暖白、Friday 琥珀）。EVA 資訊頁六角標籤兩排三格、「內部／外部」16px 粗體點陣，供電來源走 `battery_ui::externalPower()`（M5PM1 5VIN，不用有 30 秒遲滯的充電狀態）。過場直式中央、橫式左紅字右進度條（A 左／A 右），固定警戒斜紋、結束直接切畫面，獨立於 q0／q1。
- 驗收結果：**通過（Miles 11-20 實機）**——q0／q1、短按 A 切頁、B 過場三種拿法、USB 拔插內部／外部、直式與橫式 A 左／A 右。EVA 頁的 `j` 迴圈時間未量。
- 下一步：Miles 在 Mac 編譯燒錄 → 驗收過場（B 循環四模式、直橫拿、急停中斷）與角色管線（`--demo` 素材）→ commit＋push → 資訊頁 EVA 版。
- 待辦／未決：資訊頁 EVA 版；KILLED 頁改 EVA 版（`kanji_bitmaps.h` 已含「緊急停止」）；過場期間 `j` 迴圈時間。

### 2026-11-20｜M1-A / A5-1 資訊頁本機資料｜Claude Code（Cowork 橋接）
- 做了什麼：新增 `ui_info.*`（資訊頁，自己的 PSRAM 畫布、直式＋橫式 A 左／A 右）與 `ui_fx.*`（換整頁交叉淡化）。Desktop **短按 A（<0.6 秒）切頁**：接在語音誤觸路徑（`voice_turn::onRelease()` 回 true），在資訊頁按住滿 0.6 秒自動回表情頁（`faceWanted()`），Friday 在講時短按＝打斷不切頁。欄位（Miles 選方案一磚塊＋合成波）：時鐘（NTP `configTzTime`，台北）＋日期＋開機時間（UP）＋`GPT -`（A5-2 填美金額度）｜電量％＋電壓＋6 個功能 icon（藍牙、RF、nRF24、紅外線、USB、滑鼠，Desktop 下全灰）｜IMU 氣泡｜WEATHER（A5-2）｜CHIP 溫度＋頻率＋主迴圈負載波形 60 秒｜MEM heap 長條 60 秒＋開機最低值＋PSRAM｜NET LINK Wi-Fi 4 格＋dBm、CLOUD、VOICE｜MODULES Grove、FW｜快訊（A5-2，底下畫地平線）。外框跟標題同色（時鐘淡白、電量跟電量色、IMU 黃、天氣青、CHIP 藍、MEM 淡紫、NET LINK 深紫、MODULES 綠、快訊橘）。新增 getter：`battery_ui::voltageMv/charging`、`imu_pet::tilt`、`voice_turn::stateName/faceWanted`、`audio_io::buildTag`。序列埠 `n`（切頁）、`t`（交叉淡化開關）。`AUDIO_BUILD_TAG` 24-a5。
- 同片調整表情（Miles 實機逐項要求）：右上符號放大約一倍並改向量畫（！？♪、愛心、四角星、Z、生氣青筋），直式右上／橫式臉旁、夾在螢幕與臉區域內；驚嚇大汗滴；眼睛光暈 3 圈；休息臉電量色變亮；橫式臉比例 0.8→1.0；地平線變淡、橫格越遠越暗；電量 ≥60% 改霓虹綠 `#39FF14`。
- 量測：資訊頁重畫 畫 10.5～11.8ms＋送上螢幕 14.6ms；切頁（放開後）取消錄音 1～2ms＋畫面 19～26ms；資訊頁上主迴圈約 2.5ms；表情頁 `j` 迴圈 23～24ms（讀電量那窗 30～32ms）。錄音中表情頁主迴圈一度到 44ms（新符號）→ 圓弧段數減半，**最後一版（光暈 3 圈、橫式放大）未燒錄量測**。
- 驗收結果：**通過（Miles 11-20「這一版可以了」）**——短按進出、按住說話回表情頁、打斷不切頁、對時正確、轉向三版、交叉淡化、急停與切模式、桌寵背景反應都在實機確認；最後一輪視覺微調（光暈、橫式放大、外框配色、地平線、細體電量）只做了模擬，下個 Session 燒錄確認外觀與迴圈時間。
- 遇到的坑／新規則（寫入 `CLAUDE.md` 鐵則九）：(1) 切頁慢的主因是誤觸也重開麥克風 → 誤觸不重開。(2) 整頁送螢幕 14.6ms、無 TE 同步 → 由上往下刷；背光遮法「黑一下更慢」→ 交叉淡化。(3) Font0 細、efont 粗：字母細、數字粗，但「100%」與溫度粗體會糊。(4) CPU% 取不到（核心沒開執行時間統計）→ 主迴圈 ms 波形。(5) `class M5Canvas;` 前置宣告在真函式庫編譯失敗（它是別名），模擬標頭沒抓到 → 模擬標頭改成跟真的一樣。(6) 不小心在 VS Code 裡把 `h` 打進 `platformio.ini`（游標停在設定檔）→ 已還原。
- 充電實測（A4b 用）：91%→100%、47 分鐘，CHG_STAT 到最後都沒穩定停，電量 % 會跳 → 充飽看電壓（4.10V、5 分鐘幾乎不升），紀錄 `docs/lab/chg-1120-1651.txt`（本機檔，docs/lab 不進 repo；關鍵數字見 CLAUDE.md 鐵則九第 11 點）。
- 下一步：燒錄最後一版確認 → A5-2（`docs/KICKOFF_A5-2.md`）。
- 待辦／未決：`cloud/persona/friday.md` 已更新，**請跑 `cloud/persona.sh` 上傳**；電量 ≥60% 外框與 MODULES 同為綠色（Miles 看實機再決定）。

### 2026-11-20｜M1-A / A4 直橫雙版面＋表情改版｜Claude Code（Cowork 橋接）
- 做了什麼：`imu_pet` 判斷三種拿法（直式／橫式 A 左／橫式 A 右），`[orient]` 印切換、`j` 加印平面分量／角度／方向／候選；`ui_expression` 用 `M5Canvas::setRotation` 轉畫布（螢幕不轉、不重配記憶體），對話區永遠在 A 鍵側（橫式直欄、前綴自己一行、語音中轉向重新換行）。表情整個改版（Miles 逐項選定）：合成波風格（方案 3）、每種表情加嘴巴、彈簧動畫＋點頭＋眼神亂瞄＋wink＋冒符號、聆聽改側耳傾聽（B 只有耳朵，朝 A 鍵側，聲波與 o 嘴跟 `audio_io::recLevel()`）、說話嘴型依音節換、驚嚇彈珠台（IMU 線性加速度推三顆球撞牆，停手彈簧歸位）。看方向依拿法換算，橫式左右放大 1.6 倍、嘴巴跟一半。拍打規則改版。序列埠新增 `o`（假裝拿法）、`y`（預覽表情）、`h`（充電紀錄）、`#`（log 記號）。`AUDIO_BUILD_TAG` 23-a4（量測版 23-a4m）。
- 量測（v23-a4m、23-a4）：直拿 平面 0.95～1.05 角 −9～+2°｜A 右 角 +84～+87°｜A 左 角 −86～−88°｜斜靠 平面 0.33～0.56｜倒拿 ±171～176°；手拿連拍 3 下衝擊 0.67／1.21／1.03g（舊規則 0 次）；迴圈 21～25ms。充電：CHG_STAT 與 `isCharging()` 同一刻讀數會不同（來回跳），完整充電紀錄未收（Miles 決定 A5 後再測）。
- 驗收結果：**通過**——三種拿法正著看（rotation 1＝A 右、3＝A 左）、45° 不閃、平放斜放不翻；9 種表情×3 拿法、聆聽正常；迴圈 ≤25ms；橫拿講話對話欄在 A 側可讀；B 急停；拍打改版後有反應、彈珠台方向與力道修正後 Miles 認可（手感細調留待整體完工後）。
- 遇到的坑／新規則（寫入 `CLAUDE.md` 鐵則八第 3、7～11 點）：(1) Arduino `pins_arduino.h` 的全域 `RX`／`TX` 跟表情參數撞名 → 編譯失敗，參數改 `K_` 開頭、模擬標頭補上。(2) 說話句尾瞇眼時地平線出現黑色缺口：開心遮罩圓心掉到眼睛下方，A3 黑底看不出 → 眼皮一律 clip 在眼睛範圍。(3) 橫式看方向幅度被 0.8 倍縮放吃掉。(4) 彈珠台第一版方向相反、推力太弱，加上驚嚇自己的左右亂瞄讓「上下甩看起來左右跑」→ `PIN_SIGN`＋1、推力 5000、門檻 1.2g、甩的時候關掉驚嚇亂瞄。
- 計劃調整（Miles 11-20）：充電表情拆成 A4b、排在 A5 之後（先測電池）；手感與體感互動「整體完工後再細調」列待辦。
- 下一步：A5-1 資訊頁本機資料（`docs/KICKOFF_A5.md`）。
- 待辦／未決：A4b 充電實測；`cloud/persona/friday.md` 已更新，**請跑 `cloud/persona.sh` 上傳**。

### 2026-11-20｜M1-A / A3 IMU 互動＋休息臉電量色｜Claude Code（Cowork 橋接）
- 做了什麼：新增 `imu_pet.*`（IMU 在主迴圈讀，每 100ms 一窗取最大的偏離 1g／衝擊／轉速判斷）：靜置 6 秒想睡、11 秒睡著，拿起或按 A 醒；用力搖→驚嚇；拍機身→隨機開心／生氣，睡著被拍→生氣；睡著時開待機收音，聽到拍手或講話→驚嚇／生氣；傾斜→眼睛看向傾斜方向；反應 2.5 秒後回待機；語音優先（`voice_turn::busy()` 時不碰表情）。`audio_io` 加待機收音（一次一塊 50ms，只算指標不存）。新增 `battery_ui.*`：休息臉（待機／想睡／睡著）顏色跟電量（**Miles 選方案一**：≥60 藍、40–59 綠、20–39 黃、10–19 紅、<10 紅灰閃爍；情緒與語音狀態維持固定色，語音中、充電中不閃）。`ui_expression` 加 `setRestTint`／`setLowBatteryBlink`／`setGaze`，移除 `debugNextMood`。序列埠新增 `j`（IMU 量測）、`v`（待機聲音量測）、`c<%>`（假裝電量，`c` 恢復）。`AUDIO_BUILD_TAG` 22-a3。
- 量測（v22-a3m，兩份 log）：桌上靜置 偏離≤0.015g 衝擊≤0.017g 轉速≤1.1°/s；手持不動 轉速常 2～24°/s；拿起 偏離≤0.29；輕搖 偏離≤0.54 衝擊≤0.52；用力搖 偏離 0.5～7g；拍機身 衝擊 0.86～7g；**拍旁邊桌面感測不到**。待機聲音：安靜最高 −49.3、Mac 鍵盤 −44、小聲講話 ≤−49.4、拍手 −26.5～−40.3、正常講話 −34.4～−41.3 dBFS。主迴圈約 21ms／圈（每窗約 5 筆 IMU）。電量讀數充電中在 17～26% 間跳、充電旗標來回切。
- 門檻（重播 log 驗證）：有人在動＝轉速 >5°/s 或衝擊 >0.03g；搖晃＝10 窗有 5 窗偏離 ≥0.5g；拍打＝衝擊 ≥0.7g、前 3 窗 <0.2、下一窗 <0.5；聲音 −41 dBFS。重播：桌上 6 秒想睡、拿起醒、手持 24 秒不睡、輕搖與傾斜不觸發、用力搖驚嚇、拍機身 3 次觸發、拍倒不算。
- 驗收結果：**通過（7 項全過）**——靜置想睡／睡著、拿起與按 A 醒；搖→驚嚇、拍→開心或生氣、睡著拍→生氣；睡著拍手／講話→驚嚇或生氣；語音中搖拍不影響對話；B 長按 3 秒急停；`c75/c50/c30/c15/c5` 五色正確；傾斜看方向正確（`PET_GAZE_SIGN_X`＝−1）。
- 遇到的坑／新規則（寫入 `CLAUDE.md` 鐵則八）：kickoff 寫「麥克風待機本來就開著、用現有人聲指標」，實際待機時沒有在抓聲音、人聲指標只在錄音時算 → 補待機收音。電量要平滑＋遲滯，否則在 20% 附近黃紅來回跳。
- 計劃調整（Miles 11-20）：新增 **A5 資訊頁**（A5-1 本機資料、A5-2 天氣快訊），排在 A4 之後；Desktop 短按 A（<0.6 秒）切資訊頁、按住仍是說話。
- 下一步：A4 直橫雙版面（`docs/KICKOFF_A4.md`）。
- 待辦／未決：IMU 要更靈敏（很輕的點）才改獨立 task；`cloud/persona/friday.md` 已更新，**請跑 `cloud/persona.sh` 上傳**。

### 2026-11-19｜Grove 配件（CardKB／Scroll）偵測修正｜Cowork
- 做了什麼：CardKB、Scroll 到貨。插上後兩者都掃不到（`(none)`），CardKB 有電。根因是 `input_hal` 用 `M5.Ex_I2C.isEnabled()` 判斷要不要 `begin()`，但 M5Unified 開機 `setPort` 後它就回 true → 從沒 begin。改為無條件 `begin()`；另加開 M5PM1 GPIO2（L3B_EN）、開機等待 200→300ms、序列埠指令 `e`（重開供電＋印電源狀態＋400k/100kHz 重掃）。
- 驗收結果：指令 `e` 實測 CardKB `5F`、Scroll `40`（兩種速度皆可）；5V 輸出 5.07～5.10V。重開機自動偵測、只用電池兩項待補測。
- 遇到的坑／新規則：`isEnabled()` 只代表設過埠號，不代表已 begin（已寫入 `CLAUDE.md` 鐵則一）。`selftest/` 的 Grove `(none)` 同因，無參考價值。L3B 是否必要未單獨驗證。
- 下一步：補測重開機偵測與電池供電 → 寫 `grove-test/` 做功能驗收（K1–K5、S1–S6）。語音主線不變（下一片 B3c）。

### 2026-11-20｜M1-B / B3c＋B4 接 GPT-Live＋對話列文字（合併一片）｜Claude Code（Cowork 橋接）
- 做了什麼：中控接 OpenAI GPT-Live（`cloud/src/gpt_live.js` 連線包裝、`gpt_turn.js` 一輪對話、`lab.js` 先導測試、`s2t.js` 簡轉繁、`adpcm.js` 串流編碼器 ADPCM 8k／16k／μ-law）。按 A 才開 session、回答完 30 秒關；上下文 50 輪存 DO、帶最近 10 輪；每日額度；Responses 委派＋web_search 上網查；助理改名 **Friday**、聲音 `meridian`；人設＋使用者記憶 `cloud/persona/`（`persona.sh` 上傳，`miles.local.md` 不進 repo）。韌體：`start` 帶 `mode`（`m1` gpt 預設／`m0` echo）、額度錯誤回待機、gpt 模式開口前等 20 秒、預緩衝 1.2 秒、下載預設 μ-law；B4 底部 5 行對話區（`efontTW_12`，Friday 開口後保留使用者前 2 行）、字幕依播放進度顯示、嘴巴依播放 RMS；`AUDIO_BUILD_TAG` 21-b3c。
- 先導測試（`/lab`，Miles 終端機跑 `cloud/lab.sh`，不需裝置；約 $0.5）：第一輪全部作廢——GPT-Live 沒講話也一直送靜音 delta（原本以收到當開口）、素材是雜音、測試中途素材被換掉（加「鎖定素材」）。之後定出：A 開口 1.0～1.6 秒、B 4 倍速正常、C 會搶話且 mute 擋不住、D 長回答最長停頓 0.92 秒 → 安靜 1.5 秒算講完。數字與判定見 `docs/M1B-架構.md`。
- 設計決議（Miles）：B3c、B4 合併一片省驗收；實機後送音**改回邊講邊送**（buffer 4 倍速開口 3～8 秒且長句逐字稿遺失）；這片就開上網查詢；下載選 μ-law（ADPCM 8kHz 有老電話聲）；增益 1（2.5 較爆）；聲音 meridian（Ember 與 GPT-Live-1.1 只有 ChatGPT App）；盡量全繁體顯示；喚醒詞之後用「Hey Friday」；工具改走 MCP＋skill、**不連自己的電腦**。
- 驗收結果：**通過**。問天氣會先說「我查一下」再講真的天氣；「我剛剛問了什麼」答得出；30 秒以上長問題可答；字幕跟聲音同步、全繁體無方框；回答中 `x` 斷線已收到的播完、下一輪正常；`DAILY_MAX_TURNS=2` 被拒並印「今日額度已用完」；echo 回歸正常；問「你是誰」答得出 Friday 與規劃功能；μ-law 無斷音、聲音清楚。
- 遇到的坑（寫入 `CLAUDE.md` 鐵則六第 11 點、鐵則七第 12 點）：(1) 回答中的停頓扣著不送 → 裝置斷音（改全送＋預緩衝 1.2 秒）。(2) 字幕跑在聲音前（字一產生就送；改帶 ms 播到才顯示）。(3) 使用者逐字稿有時是簡體 → `efontTW_12` 顯示方框（改 OpenCC 轉台灣繁體；單字一對一表會把皇后、頭髮轉錯，不用）。(4) 接著 USB 線待機有嗡嗡聲，拔線就沒有（電腦端雜訊）。(5) wrangler 變數 `VOICE` 和 DO 綁定撞名 → `GPT_VOICE`。(6) PlatformIO `log2file` 在 VS Code 內建 Serial Monitor 不生效（log 要用 PlatformIO 監看器或手貼）。
- 下一步：M1-A A3 IMU 互動（`docs/KICKOFF_A3.md`）；G2 Grove 功能驗收可並行。
- 待辦／未決：滾輪調音量（等 G2）、MCP＋skill、Hey Friday、prompt-to-voice 自訂聲音、新聞快報、GPT-Live-1.1 開放 API 後評估；**每完成一個功能要更新 `cloud/persona/friday.md` 並跑 `cloud/persona.sh`**。

### 2026-11-19｜M1-B / B3b-5 邊錄邊傳＋網路背景化｜Claude Code（Cowork 橋接）
- 做了什麼：WebSocket 收送、重連全部移到網路 task（core 0、優先權 1、堆疊 10KB）；主迴圈↔網路 task 用指令佇列、收件佇列（PSRAM 1MB、保序、反壓）、log 佇列（主迴圈代印）交接。錄音改 4MB 環形緩衝（128 秒），**不設錄音上限**；網路 task 邊錄邊編 **μ-law** 上傳（每則 8000 bytes），`start` 帶 `id`，斷線重連後從頭重送。`voice_turn` 改四個狀態（Idle／Recording／Flushing／Receiving），每 0.5 秒印 `[up]` 進度，播完印「送出＝收到」核對。序列埠新增 `u0/u1`（上傳 PCM／μ-law）、`x`（只斷 WebSocket）。中控解 μ-law、每輪上限 64MB、存最近 60 秒錄音（B3c 先導測試用）。拿掉 `despike()`、本機整段播放（`playBuffer`）與 6 秒上限。
- 設計決議（Miles 選定）：錄音不設上限；斷線從頭重送（甲）；上傳 μ-law（辨識無風險、頻寬是 PCM 一半）、下載維持 ADPCM 8kHz；收件路徑、task 參數由 Claude 定。**M1-B 範圍收斂**：B5 新聞快報、B6 喚醒詞／工具呼叫移待辦；B4 只做直式（橫式併入 M1-A A4）；B3c 的做法與先導測試判定規則先定案（見執行計劃 B3c 列、`docs/KICKOFF_B3c.md`）。
- 驗收結果：**通過**（v20-b3b5）。T1 連講 33 秒：錄音缺 0、斷流 0、主迴圈最長 21ms、放開後 21ms 送完、送出 533088＝收到 533088 取樣、回放 0 斷音（放開後 624ms 開始播）。T2 錄音中 `x`：3.7 秒後重連、同一 id 從頭重送（追趕 30～46KB/s）、放開後 3.5 秒送完、送出＝收到。T3 連講 108 秒：送出＝收到 1732480 取樣、放開後 23ms 送完、緩衝滿丟棄 0、斷音 0。T4 錄音中 B 長按 3 秒：立即 KILLED、這輪中止。網路 task 堆疊最少剩 5768 bytes；PSRAM 剩約 2.6MB。
- 遇到的坑／新發現（寫入 `CLAUDE.md` 鐵則七第 2、7、11 點）：(1) 熱點上傳實測跟得上 μ-law 即時速度（約 15.5KB/s），重送追趕時可到 30～46KB/s。(2) 108 秒回放期間 pong RTT 印 14 秒——中控全速送的音訊在網路上排隊，pong 排在後面，不是斷線。(3) SNR 只看前 6 秒：T3 前 6 秒沒講話（−57.7 dBFS），SNR 3.3 dB 是底噪對底噪、無意義；有講話的兩輪 15～17 dB（含 μ-law 上傳＋ADPCM 8kHz 下載＋中控 ×8 增益放大底噪）。(4) 閒置斷線結案：熱點 Wi-Fi 層 Beacon Timeout（原因碼 200→2），非中控。(5) 放開 A 時主迴圈停約 0.6 秒（量測＋重開麥克風，B3b-4 就有），不影響聲音，不處理。
- **補強（11-19 晚，同片，待實機驗收，v20b-b3b5）**：(1) 誤觸防護：按住不到 0.6 秒（`cfg::VOICE_MIN_PRESS_MS`）不上傳、不送 start/stop、直接回 Idle；做法 Miles 選 A——一按就錄、按滿 0.6 秒才送 start 與上傳（中控不需改；B 案「先送再 cancel」會讓 B3c 已開的 OpenAI session 白花錢）。(2) 靜音自動結束：連續 8 秒（`cfg::VOICE_AUTO_END_SILENCE_MS`）沒有人聲 → 自動當作放開 A；人聲判斷每 100ms 一塊算一階差分 RMS（＝量測行「高頻底噪」指標），門檻 −48 dBFS（`cfg::VOICE_VAD_DBFS`）＝ B3b-5 四輪實測安靜底噪 −56.6～−57.6（中位 −57.5）+9.5 dB（與脈衝偵測同一個 3 倍幅度判準）。`[up]` 行加印每 0.5 秒的人聲指標最低／最高與靜音秒數，驗收時核對門檻。邏輯測試：誤觸 0.3 秒送出 0 則；講話／停 3 秒／講話最長靜音 3.0 秒；安靜 8 秒觸發。
- **補強第一輪實機（v20b）**：誤觸 4 次（102～182ms）都沒送 start ✓；按住不說話 8.0 秒自動結束、照常回傳 ✓；**講話中被誤切 ✗**——停頓約 6 秒後輕聲接著講，那幾塊的人聲指標最高只有 −48.9～−53.4 dBFS，低於門檻 −48，計時沒歸零，講話中觸發 8 秒結束（觸發前的話都有送出、有回放；觸發後講的沒錄）。錯在校準來源：拿錄音量測行的「高頻底噪」（整段 2ms 格中位數，含講話）推門檻，不是每 100ms 一塊的安靜值。v20b 逐塊數據：安靜 −58.8～−60.5、講話 −39.8～−55 → **門檻改 −55 dBFS**（v20c）。
- **補強驗收通過（v20c）**：不說話按住 2 次都在 8.0 秒自動結束（安靜每塊 −58.2～−61.5 dBFS）；誤觸 157ms 不上傳；講 25.6 秒、中間停 5.1 秒與 3.4 秒，重新開口時靜音計時立即歸零、沒被切（輕聲每塊 −44～−55），送出＝收到 410176 取樣。門檻與安靜最高值相差約 3 dB，規則寫入 `CLAUDE.md` 鐵則六第 10 點。
- 計劃調整（Miles 11-19）：B3c 加「中控每日額度」（輪數或音訊秒數超過就拒絕，wrangler 變數，預設 200 輪／30 分鐘）；M1-A 的 A3、A4 排在 B4 之後。
- 下一步：B3c 接 GPT-Live（`docs/KICKOFF_B3c.md`），第一步是中控先導測試。
- 待辦／未決：無新增。

### 2026-11-19｜M1-B / B3b-4 邊收邊播＋斷線處理｜Claude Code（Cowork 橋接）
- 做了什麼：中控 stop 後一則一則送（`pace=fast` 預設／`rt` 真實速度），音量正規化移到中控，新增 `down=adpcm8k`（低通＋降到 8kHz＋ADPCM，約 4KB/s）並設為裝置預設（Miles 定案，不另做聽感測試）。裝置 `audio_io` 新增 8 秒 PSRAM 環形緩衝＋串流播放：緩衝 0.3 秒開始播，由 M5Unified 播放 task 的緩衝釋放回呼自己餵喇叭（聲道 0、先查隊列），斷音後補到 1 秒再繼續；`adpcm` 加 8 階半帶濾波器升取樣；`voice_turn` 改邊收邊解壓邊播，拿掉本機回放。`net_hub` 加 `[link]` 斷線紀錄、Wi-Fi 層原因碼、`i<秒>` ping 間隔；中控印 `[ws] close/error`。斷線畫面（Miles 選方案 c）：斷線 >2 秒眼睛變灰、表情定格、右上 Wi-Fi 斜線，恢復回待機。序列埠新增 `f3`／`r0`／`r1`／`b<ms>`／`i<秒>`。
- 設計決議（Miles 選定）：下載預設 ADPCM 8kHz；中控預設全速送；喇叭由播放 task 餵（主迴圈收一則訊息實測卡最久 2.5 秒，原本「主迴圈餵」的建議因此撤回）；斷線提示 c＋恢復回待機；回答播到一半斷線播完已收到的；上傳／等待中斷線不回放。
- 驗收結果：**通過**（v18／v19-b3b4）。中控送完後 0.6～1.4 秒開始播、0 斷音、緩衝最低 1.6～2.5 秒；下載實測 35～75KB/s（ADPCM 8kHz 需 3.9KB/s）；SNR 20～22 dB（含 8kHz 頻寬損失）；`w0` 印 `[link]`、畫面變灰定格，`w1` 恢復回待機；開機第一個序列埠指令已正常。按住 A 10 秒以上不再爆音。
- 遇到的坑（規則寫入 `CLAUDE.md` 鐵則七第 6～7、9～10 點）：(1) 當晚熱點 RTT 630～760ms，上傳約 9.5KB/s、下載約 8.8KB/s（μ-law 跟不上）→ 改 ADPCM 8kHz。(2) `ws.loop()` 等整則訊息收完才放手，最久卡 2.5 秒；斷線重連（TLS 交握）卡 2.3 秒，錄音中發生會少錄 2 秒。(3) **中控 DO 閒置約 10 秒休眠、記憶體清空**：按住 A 10 秒 → 中控忘了格式、照 pcm16 回傳 → 裝置當 ADPCM 解 → 大爆音；改存連線附件。我一開始推測是斷線重連，log 沒有 `[link]` 而否定。(4) M5Unified `playRaw` 不指定聲道會挑空聲道同時混音、隊列滿會原地等（查原始碼）。(5) 線性內插升取樣只有約 25dB，換半帶濾波器。
- 下一步：B3b-5 上傳邊錄邊傳＋網路背景化（`docs/KICKOFF_B3b5.md`）。
- 待辦／未決：`ssl -80` 連線重置原因未抓到（當晚連 185 秒沒斷），請閒置收 `[link]` 紀錄；錄音上限仍 6 秒（B3b-5 處理）；橋接環境無法編譯（PlatformIO 套件伺服器被擋），v19 的格式警告只做過語法檢查，下次 Build 時確認。

### 2026-11-19｜M1-B / B3b-3 下載壓縮定案（μ-law）＋查證 GPT-Live｜Claude Code（Cowork 橋接）
- 做了什麼：查 OpenAI 官方文件——GPT-Live 端點是 `wss://api.openai.com/v1/live/sessions`（先前寫的 `/v1/realtime` 有誤，已改執行計劃與 `docs/M1B-架構.md`），支援 PCM 16kHz（與裝置相同、不需重取樣），輸入輸出同一格式。切法改為 B3b-3 下載壓縮／B3b-4 邊收邊播／B3b-5 邊錄邊傳（Miles 同意）。中控新增 `cloud/src/adpcm.js`（IMA ADPCM、μ-law 編碼與訊息合併），`start` 帶 `up`／`down` 格式、中控回 `format` 確認；裝置新增 `firmware/src/adpcm.*` 解碼，驗收印 SNR。序列埠 `f0/f1/f2` 切 PCM／ADPCM／μ-law。
- 設計決議（Miles 選定）：上傳原始 PCM（OpenAI 聽得最清楚）、下載壓縮；加格式欄位當保險；**預設 μ-law**，ADPCM 為弱網備案。
- 驗收結果：**通過**。三種格式各錄 2 次（5～6 秒）：PCM 等待回傳 6.3～6.6 秒（CRC 一致）、μ-law 約 3.6 秒（SNR 36.6 dB）、ADPCM 約 2.0 秒（SNR 25 dB）。實聽：μ-law 最清楚、ADPCM 較糊、PCM 與 μ-law 聽不出差。
- 遇到的坑（規則寫入 `CLAUDE.md` 鐵則七第 5～7 點）：(1) 每則 WebSocket 訊息約 67ms 固定成本——804 bytes 的 ADPCM 幀下載只剩 12KB/s，合併成約 8KB 一則後回到 23～26KB/s。(2) 第一次比較時 `f0` 沒生效、PCM 那輪又斷線改本機回放，造成「f0 較快較清楚」的誤判 → 指令要確認有回 `下載格式 → …` 再錄。(3) 開機後第一個序列埠指令常失效 → 解析時略過看不見的字元，並在不認得時印 16 進位以便追查。(4) 熱點 `ssl -80 Connection lost` 變頻繁（閒置約每 40～80 秒、上傳中也有），RTT 偶飆 1～3 秒。
- 下一步：B3b-4 邊收邊播＋斷線處理（含 Miles 要的斷線畫面提示：斷線標誌、表情定格，恢復後繼續或回起始）→ B3b-5 → B3c。
- 待辦／未決：連線重置原因未查；開機第一個指令失效是否已修需燒錄確認。

### 2026-11-18｜M1-B / B3b-2 音訊來回中控（整段上傳）｜Claude Code（Cowork 橋接）
- 做了什麼：中控 `voice_session.js` 收二進位幀暫存（每輪上限 256KB），`stop` 時原樣回傳＋`{"t":"audio_end","n":...}`；B3a 的 stop→OpenAI 文字撤下（`askOpenAI()` 留給 B3c）。裝置新增 `voice_turn.*`（錄音→3200 bytes 幀上傳，每圈 2 幀→等 audio_end→播放），CRC32 比對上傳／收回；中控未連線、上傳失敗、中途斷線退回本機回放；表情本機決定、中控 state 只印出。`audio_io` 拆成 `stopRecording()`／`playBuffer()`＋PSRAM 收音緩衝；PGA 預設改 `0x15`。量測：每輪印 `[perf]`、ping 回來印 RTT。修正：Wi-Fi 沒連上不跑 WebSocket（避免 DNS 卡主迴圈）、等待逾時改「5 秒沒進展」。
- 設計決議（Miles 選定，全照建議）：表情本機決定；中控未連線退回本機回放；背景嘶聲只把 PGA 改 `0x15`（B3c 後使用者聽的是 AI 語音，不值得做雜訊閘）；`/test` 的 stop 不再呼叫 OpenAI。
- 驗收結果：**通過**。十餘輪 CRC 全部一致，最長 6 秒；無滋滋聲；`g15` 實聽優於 `g17`（高頻底噪約 −50～−58 vs −45～−49 dBFS）；`w0` 退回本機回放正常、不再重連；6 秒錄音不再誤判逾時。
- 遇到的坑（規則寫入 `CLAUDE.md` 鐵則七）：(1) **熱點 RTT 約 290～400ms**，加上 ESP32 TCP 緩衝小 → 上傳約 36KB/s（`sendBIN` 3200 bytes 平均 88ms，偶發 3.3 秒重傳）、下載約 25KB/s；6 秒錄音來回 12～13 秒。原始 PCM 跟不上即時，B3c 前必須處理 → 新增切片 B3b-3。(2) Wi-Fi 關閉時 WebSocket 每 5 秒重連、DNS 查詢卡住主迴圈數秒，錄音只錄到 2 秒（已修）。(3) 固定 8 秒逾時對 6 秒錄音不夠（已改）。(4) 舊的「斷流」計數在放開 A 時來不及計 → 改用「按住時間 vs 實際錄到」判斷。
- 下一步：B3b-3 傳輸提速（先比較方案）→ B3c。
- 待辦／未決：錄音上限 6 秒（`CAP_SAMPLES`），邊錄邊傳後再改；中控 WS 偶發 `ssl_client (-80)` 被重置；省電關閉期間的耗電未量；不說話錄音約 160ms 週期來源未查。

### 2026-11-18｜M1-B / B3.6 Wi-Fi 滋滋聲定案｜Claude Code（Cowork 橋接）
- 做了什麼：韌體加序列埠診斷指令 `firmware/src/diag_cmd.*`（`w0/w1` Wi-Fi、`s0/s1` 省電手動、`sa` 省電自動、`p<dBm>` 發射功率、`g<hex>` PGA、`k` ping、`?` 設定），`net_hub` 加對應函式與讀回實際設定的標籤；`audio_io` 每次錄音印量測行（整段 RMS、高頻底噪、脈衝、週期峰、斷流，附 Wi-Fi 設定與 PGA），版本標記印在錄音行。`platformio.ini` 加 `monitor_echo`／`send_on_enter`／`monitor_eol=LF`，Serial Monitor 才能打指令。修正：按下 A 關 Wi-Fi 省電、播完／離開 Desktop 開回（`net_hub::voiceActive()`，Miles 選方案 B）。
- 驗收結果：**通過**。Wi-Fi 連著（WS connected）講話，6 次都沒有滋滋聲，錄音開頭也乾淨；不說話錄音高頻底噪 −53.2 dBFS，與手動關省電相同。
- 量測經過：(1) 省電開 4 次都有滋滋、關 3 次都沒有；降發射功率到 8.5dBm 無效；斷流皆 0。(2) v9 的平均音量指標只差 1～3dB 分不出來，10ms 週期指標永遠落在搜尋下限（寫錯）；v10 改「高頻底噪」（差分 2ms 格 RMS 中位數）才分出來：省電開約 −50.4、關約 −53.5 dBFS，可重複。脈衝偵測為 0 → 雜音是持續的，**不是**原推測的每 102ms beacon 脈衝；機制推測為低負載時電源漣波進入可聽頻段（未證實）。規則寫入 `CLAUDE.md` 鐵則六第 9 點。
- 遇到的坑：上一輪實作途中連線中斷、回覆變英文，重送後我把自己中斷前寫的檔誤判成「另一個 session 在改」→ 以後先比對內容再判斷。
- 新發現：(1) 背景嘶聲（偏高頻）不是 Wi-Fi，是麥克風自身底噪經 PGA +21dB 與正規化放大；`g15`（PGA 降兩階）明顯改善 → 併入 B3b-2。(2) 不說話錄音有約 160ms 規律週期（Wi-Fi 關時也有，聽不出來），來源未查。(3) 中控 WS 偶發 `ssl_client (-80)` 被對方重置後自動重連，B3b-2 傳音訊時留意。
- 下一步：B3b-2（方案 A＋背景嘶聲）→ B3c。
- 待辦／未決：省電關閉期間的耗電增加未量；B3c 之後「聽→想→說」整段要維持關省電。

### 2026-11-18｜M1-B / B3.5 連線 token 驗證｜Claude Code（Cowork 橋接）
- 做了什麼：中控 `/ws` 要求 `?t=<HUB_TOKEN>`，SHA-256＋`crypto.subtle.timingSafeEqual` 固定時間比對，不符回 401、未設定回 503，皆不進 DO（`cloud/src/worker.js`）；/test 頁加 token 輸入框、「記住在這個瀏覽器」（localStorage）、清除鈕。裝置 `net_hub.cpp` 連線網址加 token、斷線印原因、`secrets.h` 缺 `HUB_TOKEN` 會 `#error`。`cloud/README.md` 加產生／更換 token 的一行指令；`docs/M1B-架構.md`、`CLAUDE.md` 第 8 節補規則。
- 設計決議（Miles 選定）：**token 放 query（方案 A）**，裝置與 /test 頁同一套、好維護；代價是 `wrangler tail` 會印出含 token 的網址 → 貼 tail 輸出前要遮掉。header＋subprotocol（方案 C）不採用。裝置 `beginSSL` 不驗中控憑證（函式庫 `setInsecure()`），只連家用 Wi-Fi 與自己的熱點，接受此風險。
- 驗收結果：**通過**。/test 頁不填、填錯被拒，填對可連、記住正常；裝置用正確 token 連上（`[hub] WS connected`、持續收到 pong）；Miles 刻意換錯 token 可見被拒原因。
- 遇到的坑：(1) 第一版 token 設定步驟要 Miles 在步驟間靠剪貼簿傳 token，他複製下一步指令時把剪貼簿蓋掉，中控與 secrets.h 都存到指令文字 → 改成一行指令（token 只存在 shell 變數，直接 pipe 給 `wrangler secret put` 並寫進 secrets.h），規則已寫入 `CLAUDE.md`。(2) iPhone 熱點連不上時要開「最大化相容性」（ESP32 只支援 2.4GHz）並停在熱點設定頁。
- 新發現：(1) **按 A 不送 start/stop 給中控是預期行為**——B3b-1 把 A 改成本機錄放音（`main.cpp` 註解：B3b-2 改回串給中控），表情由本機切換。KICKOFF_B35 的驗收項「按 A 照常切表情（經中控）」不適用。(2) **Wi-Fi 連上後錄音有電波滋滋聲**，未連上時沒有（數據見 `CLAUDE.md` 鐵則六第 9 點）→ 新增切片 B3.6，排在 B3b-2 之前。
- 下一步：B3.6 Wi-Fi 滋滋聲（先量測）→ B3b-2（方案 A）→ B3c。
- 待辦／未決：日後若要上不可信網路，改 `beginSslWithCA` 驗證 Cloudflare 根憑證。

### 2026-11-18｜M1-B / B3b-1 本機錄放音音質定案＋執行計劃改版｜Claude Code（Cowork 橋接）
- 做了什麼：`firmware/src/audio_io.*` 重寫——分塊預排錄音（無空檔）、緩衝清零、ES8311 麥克風增益改「模擬 +21dB／數位 0dB」、喇叭取樣率 22050→16000 與乘數 1→4、DAC 靜音時序、正規化、淡入淡出、頭尾各切 60ms 按鍵聲；序列埠每次錄音印版本標記（`AUDIO_BUILD_TAG`）與滿幅診斷。`main.cpp` 的 `feedRecording()` 改名 `loop()`。`docs/StickS3 執行計劃.{md,html}` 改版：所有里程碑拆切片（M1-A 4、M1-B 11、M1.5 3、M4 3、M2 4、M3 8），RF Pack 狀態改為「接收正常、SPI 需防錯」。
- 驗收結果：**通過**。噠聲消失、音量正常（正規化 ×3～×6）、不破音。
- 遇到的坑（規則已寫入 `CLAUDE.md` 鐵則六）：(1) 噠聲主因是 **A 鍵機械聲被錄進去**（滿幅固定在按下後 38～52ms），不是喇叭；(2) 自己額外通斷喇叭放大器反而更響，已撤回；(3) 夾波在 ES8311 內部（`0x17`=`0xFF`），調 M5Unified `magnification` 無效；(4) 版本標記只印在開機時會被丟掉 → 改印在每次錄音行。
- 下一步：**B3.5 連線 token 驗證**（新 session，接手 prompt `docs/KICKOFF_B35.md`）→ B3b-2（方案 A）→ B3c。
- 待辦／未決：錄音上限 6 秒（`CAP_SAMPLES`），B3b-2 改串流後消失，不另改；`despike()` 實測只修掉 1～4 個、留著無害，B3b-2 時評估移除；每次 `micOn()` 印的 ES8311 讀回行可在 B3b-2 收掉。

### 2026-11-17｜v6.4 六輪完整實測：錯誤率與叢發特性定案｜claude.ai
- 做了什麼：連續跑 6 輪 SPI 可靠性測試（每輪間隔數秒），期間穿插切換 AGC 設定與一次 10 秒擷取。
- 驗收結果：
  - **單筆讀取錯誤率穩定約 0.5%**：12 次量測（IDLE／RX 各 6 次）每次 200 筆幾乎都恰好錯 1 筆。
  - **「連讀到兩次相同」不可靠**：2 次出現「兩筆一致但值是錯的」，分別 1/200 與 **14/200** → 錯誤成叢出現、同一錯值會連續重複，上一輪「連讀確認 400 次 0 錯」的結論**作廢**。
  - 寫入驗證 1～2 次成功，IDLE 與 RX 皆可（先前「RX 寫不進去」的推測也作廢，那是 v6.3 測試設計錯誤造成的）。
  - **6 輪都正常跑完，無永久鎖死**；但 2 次在測試結束、恢復 RSSI 取樣時卡 2022 ms（＝逾時 1011 ms × 2）後自行恢復。永久鎖死只在「兩輪零間隔連跑」時發生。
  - 錯值新增 `08`／`20`／`34`／`C7`／`F8`，其中高位元為 1 者對應 CHIP_RDY＝未就緒或 RXFIFO_OVERFLOW，仍是狀態位元組。
  - 一次 hang 之後的 `[regs]` 大量錯誤，印證剛恢復時讀值特別不可信。
- 修正：`CLAUDE.md` 鐵則五第 7 點改為「多數決（至少 3 筆）＋ `VERSION` 語意檢查」，並補上錯誤叢發特性；`docs/cc1101-spi-reliability.md` 同步改寫並新增第 6 節「這是正常的嗎」。
- 下一步：寄出回信（草稿已附六輪數據）；M3 動工時依新規則實作 `rf_transceiver` 的讀寫層。
- 待辦／未決：換貨決定（退換期可能 11/20）。

### 2026-11-17｜v6.4 定案：讀取偶發錯誤＋高強度存取會鎖死｜claude.ai
- 做了什麼：修好 v6.4（先前 `device_commit_files` 回報成功但檔案沒落地，改用 device_bash 直接改檔），跑 IDLE／RX 對照；再加 v6.5（每列 50 筆、自動中止、SIDLE 恢復測試），避免測試卡死跑 20 分鐘。
- 驗收結果（重置後乾淨一輪）：IDLE 單筆讀取 0/200 錯、RX 1/200 錯；**兩種狀態的連讀確認都 0 失敗 0 錯值**；寫入 1～2 次成功。前 24 筆原始讀值裡偶有 `0F`／`07`／`00`／`34` 等錯值，且狀態切換後第一筆特別容易錯。
- 關鍵發現：**鎖死的觸發條件是「短時間大量連續交易」**。一輪測試（約 900 筆）正常；連按或長按 B 導致兩輪無間隔連跑 → 立刻完全停止回應且不恢復；間隔 2 秒再跑 → 正常。每 2 ms 讀 RSSI 連續 5 分鐘無事。
- 判定：接收與暫存器存取都可用，韌體防錯即可開發 M3。錯誤形態（錯值多為 CC1101 狀態位元組）偏向時序問題而非晶片損壞，但單片無法斷定是否瑕疵。
- 新文件 `docs/cc1101-spi-reliability.md`；`CLAUDE.md` 鐵則五新增第 7 點（連讀確認、寫入驗證、禁用 read-modify-write 函式、禁 burst、高強度存取間留間隔）。
- 下一步：回信 PINGEQUA 附此報告，請其在樣品上對照並保留換貨選項；M3 動工。
- 待辦／未決：換貨決定（退換期可能 11/20）。

### 2026-11-17｜v6.3 測試無效＋改寫為 v6.4（IDLE／RX 對照）｜claude.ai
- 做了什麼：跑 v6.3 的 SPI 時序測試（寫 A5 到 0x04，10 種間隔各讀 200 次）。
- 驗收結果：**所有間隔都 200/200 錯、奇偶各半、hang 0**。但讀回值一律含 `D3`，而 **D3 正是 SYNC1（0x04）的原廠預設值** → 是測試的寫入沒生效，不是讀錯；此測試設計無效，結論不成立。
- 新線索（值得記）：v6.2／v6.3 讀錯時出現的 `01`、`03`、`07`、`0F`、`1F` 都符合 **CC1101 狀態位元組**的格式（bit6-4＝狀態、bit3-0＝FIFO 位元組數；RX 時高位元為 001→1x，IDLE 為 000→0x）→ 推測每隔一筆交易「讀到狀態位元組而非資料位元組」，即資料晚一個位元組，屬軟體／時序問題而非晶片損壞。未驗證。
- 另一線索：驅動 `SpiStart()` 的順序是 `CS 高 → endTransaction → CS 低 → beginTransaction`，CS 先拉低才設定時脈；v5（IDLE）沒事、v6（RX）每隔一筆錯，兩版唯一的結構差異就是晶片狀態。
- 新增 v6.4（取代 v6.3 的測試）：不依賴寫入，改讀唯讀且值已知的 VERSION（應 0x14），在 **IDLE 與 RX 兩種狀態**下各做：前 24 筆原始讀值（看交替規律）、200 筆單筆讀取統計、200 筆「連讀到兩次相同」統計、寫入後驗證要重試幾次。
- 下一步：跑 v6.4 → 若 IDLE 正常、RX 出錯，則為狀態相關的時序問題（韌體可避開，硬體不必換）；若兩者都錯且連讀確認也救不回，才偏向硬體。

### 2026-11-17｜v6.2 結果：不是自發卡住，是讀取時序問題＋v6.3｜claude.ai
- 驗收結果（v6.2，靜置 5 分鐘以上＋按 B 三次）：**hang 0**（取消週期性的連續讀取後不再卡住）。設定 3（C7 01 40）背景約 -80 dBm。
- 新發現：`[regs]` 改成每筆讀取前停 300 µs 後，**每隔一筆就讀錯**（錯值多為 01／03；對的那幾筆值都正確，寫入的 AGC 也讀得回來）。與 v5 手寫 SPI 的「每隔一次交易出錯」是同一現象 → 用 SmartRC 函式庫也會發生，取決於兩次交易的間隔。v6.1 連續讀取時大多正確，但偶爾引發約 2 秒不回應。
- 判讀：規律性很強（嚴格交替），偏向匯流排／驅動時序問題而非隨機的晶片損壞；但只有一片板子，無法排除本片緩衝電路不良。接收與寫入設定實際有效（頻率、AGC 改了都有對應反應）。
- 新增 v6.3：MONITOR 下 B 按住 1 秒執行 SPI 時序測試（暫存器 0x04 寫入 A5，間隔 0～5000 µs 各讀 200 次，單筆讀取與 burst 讀取分開統計）。
- 下一步：跑 v6.3 取得「哪些間隔安全」的表 → 決定韌體規則，並附給 PINGEQUA 請其在樣品上對照。

### 2026-11-17｜分離測試第 2 步即重現＋v6.2｜claude.ai
- 做了什麼：完整斷電後、電量 100%、機器放桌上不碰、不按遙控器，MONITOR 靜置 3 分鐘。
- 驗收結果：**靜置就會發生**。`checkMISO()` 逾時出現在 61.1／62.2 秒、91.0／92.0 秒、121.0／122.0 秒（每次連續 2 筆，約 2 秒沒回應後自行恢復）；rxdrop 0。三次都緊接在每 10 秒一次的 `[regs]`（20 多筆連續讀取）之後，但 18 次 `[regs]` 只有 3 次觸發。逾時時驅動回傳 0x00，所以統計裡出現 -74 dBm 的假值。`[regs]` 的 FREQ2 每次讀回都不同（1E／06／87／E3／20／07／7C／03／1F，應為 10），FREQ1／0 正確。
- 排除：電量、接觸晃動、近距離發射、Bruce 的重置時序 bug（我們是 IDF 4.4）。
- 未排除：晶片或 RF Pack 匯流排緩衝的硬體問題；驅動在「連續密集交易」下的時序問題（v5 的手寫 SPI 也有類似敏感性）。
- 新增 `rssi-test` v6.2：SPI 操作超過 500 ms 記為 hang 並顯示次數；取消週期性 `[regs]`；`[regs]` 內每筆間隔 300 µs、FREQ2 連讀三次。
- 下一步：v6.2 靜置 5 分鐘看 hang 是否仍出現（仍出現＝自發性；不出現＝密集讀取觸發），並回信 PINGEQUA 保留換貨案。

### 2026-11-17｜v6.1 追加：晶片間歇性不回應｜claude.ai
- 做了什麼：Miles 確認 333 MHz＝鐵捲門遙控器、315 MHz＝Kuga 鑰匙。SCAN 切到 300-348 頻段續測。
- 驗收結果：安靜時底噪約 -101 dBm（20 輪穩定）。按鐵捲門遙控器後，整段讀值升高（平均 -87 → -77 dBm）、峰值落在 300.00／336.00／320.25 等不相干頻率（另一次顯示 387）；隨後序列埠連續出現 `checkMISO(): CC1101: Fail checking MISO pin`（每次 1 秒逾時、連續 5 筆），恢復掃幾輪後又再發生，**放開遙控器後仍持續間歇出現**。
- 遇到的坑／新規則：先前「SCAN 底噪 -55 是 RSSI 未穩定」的推測不成立（單一頻段同設定為 -101），已更正文件，回信草稿中相關段落作廢。「硬體正常」的結論改為「接收鏈有反應，但 SPI 回應不穩，待查」。
- 下一步：分離測試（見待辦），判斷是自發、受近距離發射影響，還是機械接觸。
- 待辦／未決：換貨決定、回信內容。

### 2026-11-17｜CC1101 接收驗收 v6／v6.1 實測｜claude.ai
- 做了什麼：Miles 在 Mac 編譯燒錄 `rssi-test/`。v6 首測後加上 v6.1（MARCSTATE 連讀確認、每 10 秒印 `[regs]`、SCAN 掃頻模式）。
- 驗收結果：
  - MONITOR 433.92 MHz／256 kHz：設定 1（AGC 03 40 91）背景平均 -111 dBm；設定 2（C7 00 B2）背景 -96.0 dBm 且完全不動 → `0xC7` 確實讓基線升高變平（方向符合 PINGEQUA 說法，但沒到 Bruce 上的 -50）。
  - 遙控器貼近 10 公分在 433.92 只到 -104 dBm → 遙控器不在 433.92。
  - SCAN（812 kHz、0.75 MHz 一點）：第一支遙控器 **315.00 MHz -16 dBm**（另有 340.50 MHz -36 dBm），第二支 **333.00 MHz -18 dBm**（336.00 MHz -46.5 dBm）；433 一帶無峰值。鐵捲門鑰匙與車鑰匙各是哪一個頻率未確認。
  - VER 14、MARC 0D、v6.1 下 rxdrop 0。
  - **結論：CC1101 接收鏈與頻率合成正常，硬體非故障。**
- 遇到的坑／新規則：手邊的遙控器是 315／333 MHz，不是 433.92；驗收 RF 要先掃頻找訊號源頻率。SCAN 的底噪顯示約 -55 dBm（MONITOR 是 -111），疑為每點換頻進 RX 後 3 ms 就取最大值、抓到 RSSI 起始暫態——Bruce 的 RSSI Spectrum 恆定 -50～-55 可能是同一現象（未驗證）。`FSCAL1` 讀回 00 但頻率正確，不代表異常。
- 下一步：選做天線有／無對照 → 回信 PINGEQUA → M3 動工前釐清待辦中的三個讀值疑點。
- 待辦／未決：見「目前狀態」待辦。

### 2026-11-17｜CC1101 接收驗收 v6 撰寫｜claude.ai
- 做了什麼：新增 `rssi-test/`（獨立 PlatformIO 專案，設定同 `acceptance-test/`）。433.92 MHz、OOK、非同步模式進 RX，顯示 MARCSTATE／FSCAL1／即時 RSSI／5 秒統計／歷史圖；B 切換三組 AGC（晶片出廠值 03 40 91、SmartRC／Bruce 的 C7 00 B2、Bruce 固定頻率 OOK 的 C7 01 40），A 做 10 秒擷取並印結果表，B 長按切 EXT 5V。全程只接收。
- 驗收結果：**尚未編譯、尚未實測**。雲端無法連 PlatformIO 套件庫，只用假標頭＋真實驅動標頭做過語法檢查。
- 遇到的坑／新規則：SmartRC `Init()` 本身就寫入 `AGCCTRL2=0xC7`（與 Bruce 相同），「預設 AGC」必須明確寫回 `03 40 91`；已更正 `docs/bruce-1.17-rf-findings.md` 第 3 節第 4 點。
- 下一步：Miles 在 Mac 上 Build／Upload，照 `rssi-test/README.md` 做 6 組擷取並貼回結果表 → 判定硬體 → 再回信 PINGEQUA。
- 待辦／未決：退換期可能 11/20 截止。

### 2026-11-16｜PINGEQUA 回覆＋Bruce 1.16.1／1.17 原始碼對照｜claude.ai
- 做了什麼：PINGEQUA 回覆判定 RF Pack 硬體正常，症狀來自 Bruce 韌體（`AGCCTRL2=0xC7` 使 RSSI 基線偏高、IDF 5 下 `checkMISO()` 被編譯掉造成偶發 not found、`rf_SquareWave()` 連續初始化兩次）。下載 Bruce `1.16.1` 與 `1.17` 及驅動原始碼比對。
- 驗收結果：官方三點在原始碼皆屬實；**兩個 bug 在 1.17 都還在**（1.17 新增 `radioReady` 參數但 SquareWave 沒用上）。僅為讀碼推論，未實機重現。詳見 `docs/bruce-1.17-rf-findings.md`。
- 遇到的坑／新規則：自有 RF 韌體避坑清單寫入上述文件第 3 節，並同步 `CLAUDE.md` 鐵則五第 6 點。重點：我們用 `espressif32@6.12.0`（IDF 4.4），`checkMISO()` 有效；升 Arduino 3.x 前要補等待並重跑 v5。
- 下一步：回信 PINGEQUA 回報 1.17 未修；寫 v6 在現有板子上獨立驗證 CC1101 接收。
- 待辦／未決：CC1101 接收是否正常仍無自有數據；退換期可能 11/20 截止。

## 紀錄

### 2026-11-23｜M4-1 第一輪實測調整（31-m4r）｜Claude（Cowork）
- 做了什麼：`@m` 實測：方向對、零點在 Desktop 開機就校正完（訊息早於進 AirMouse，所以沒看到）、IMU 約 200 筆/秒、靜止判定正常。Miles：太鈍、沒有指哪跑哪 → 改線性（指數 1.0）、增益 25、死區 1.0；快甩時報告頂到 ±127 → 累積量上限 2 筆避免手停游標還在跑；進 AirMouse 印 `[air]` 狀態；`@n` 調曲線指數。
- 驗收結果：待燒錄實測。
- 下一步：Miles 用 `@g`／`@n` 微調手感 → 寫回預設 → 驗收（桌上 1 分鐘不飄、Finder 點擊拖曳、移動中急停）。

### 2026-11-23｜M4-1 陀螺儀游標實作（31-m4q，待實機驗收）｜Claude（Cowork）
- 做了什麼：iPad（AssistiveTouch）配對＋`@1` 畫圈 ✅ → M4-0 全部通過。新增 `air_mouse.*`：陀螺儀轉速→游標（零點自動校正、死區、加速曲線、放下靜止就停）、A＝左鍵（點擊／拖曳，按鍵前後凍結 80ms）；序列埠 `@m` 量測、`@g` 靈敏度、`@d` 死區、`@x/@y` 翻方向。不動畫面。
- 驗收結果：Mac 上 g++ 語法檢查 air_mouse.cpp 通過；待 Miles 編譯燒錄實測。
- 遇到的坑／新規則：CLAUDE.md 鐵則十二第 18 點。
- 下一步：`@m` 量左右轉／上下點的軸向與手抖幅度 → 定 `AIR_*` → 驗收（游標跟手、桌上 1 分鐘不飄、Finder 點擊與拖曳、移動中 B 3 秒急停）。


### 2026-11-23｜M4-0 Mac 通過（31-m4p）｜Claude（Cowork）
- 做了什麼：`ble-mouse-ref/`（Bluedroid）在同一台 Mac 也是「配對完 70ms Unused 斷線」→ 判定 Mac 側。Miles 把隱私權裡所有 app 的藍牙權限移除＋重開機（Teams 開機要求藍牙，按不允許）→ 對照版成功；燒回主韌體也成功。31-m4p：`@u`（配對後要求連線參數）預設改關。
- 驗收結果：Mac ✅（主機訂閱報告、`@1` 畫圈、畫圈中 B 按住 3 秒急停→Safe、B 短按回 AirMouse 後 Mac 自動重連並訂閱）；iPhone ✅（11-23 稍早）；iPad 待測。
- 遇到的坑／新規則：CLAUDE.md 鐵則十二第 17 點（Mac 測藍牙前先關其他 app 藍牙權限；查問題看 bluetoothd log）。序列埠 `!@0`＝監看視窗多送了 `!`，不是韌體問題。
- 下一步：iPad 配對（AssistiveTouch）→ M4-1 規劃（陀螺儀轉速→游標，先給計畫與檔案清單）。


### 2026-11-23｜M4-0 Mac 斷線查證＋Bluedroid 對照韌體｜Claude（Cowork）
- 做了什麼：Mac 端 `log show` 三輪全程比對（bluetoothd＋其他程式）。新增 `ble-mouse-ref/`（Arduino 內建 Bluedroid＋BLEHIDDevice 最小滑鼠，名稱 M5Ref，PID 0x4D42，A 短按畫圈），用來判斷問題在 NimBLE 實作還是 macOS。
- 查證結果（macOS 26.6.2）：Mac 有找到 HID 服務（「The device supports HID」、讀到 DIS／PnP）、配對加密成功，但 HID 驅動始終沒接手（「Number of LE HIDs connected: 0」、沒讀 Report Map、沒訂閱），系統設定一退出就以 Unused 斷線（配對後 0.07～2 秒）。「Incompatible LE HID／HIDBadBehavior」是 macOS 把第三方 LE 滑鼠切 7.5ms 時一起標的，斷線後回 Compatible，判斷為結果非原因。大量「does not have _HID_SERVICE_」是周圍其他 7 台裝置，不是我們。Brave：只被動收事件，沒有去連我們；但它註冊的配對代理會攔截配對確認（第二輪配對提示由 Brave 跳出，等了 8 秒），測試時在 `brave://settings/content/bluetoothDevices` 設不允許。
- 驗收結果：待燒 `ble-mouse-ref/` 在同一台 Mac 對照。
- 下一步：參考版能用 → 逐項對照兩邊 GATT／配對流程改 firmware；參考版也被斷 → 判定 macOS 側問題，改走其他路線。燒參考版前主韌體先 `q0`（PM1 看門狗）。


### 2026-11-16｜M1-B 進度：中控上線、B3a 真語音文字通、B3b-1 回放出聲｜Cowork
- **B1 中控骨架**：`cloud/` Cloudflare Worker + Durable Object，已部署：**https://sticks3-hub.yenpang.workers.dev**（`/ws` WebSocket、`/test` 測試頁）。WebSocket 協定、存對話、ping/pong 全通。
- **B2a/B2b**：裝置連 Wi-Fi + 中控 WS；長按 A → start/stop；中控 state → 切 Listening/Thinking/Speaking 表情。實機通過。Wi-Fi/中控位址在 `firmware/src/secrets.h`（git 忽略）。
- **B3a**：中控收到 stop 真的呼叫 OpenAI（`gpt-4o-mini`，文字）回覆，`/test` 頁驗證真回覆。**OpenAI 走 pay-as-you-go，已儲值 US$10**（Plus 方案沒有免費 API 額度，只有 Pro 才有）。key 以 `wrangler secret put OPENAI_API_KEY` 存在 Worker（不在 repo）。
- **B3b-1**：`audio_io` 本機回放（按住 A 錄音→放開播放）。已出聲，但原本分塊錄音有空檔→小聲/破/偏快；**已改連續錄音＋喇叭音量 220，待實機再驗**（commit d075d81）。
- **表情**：M1-A 切片 1、2 完成，9 種表情（含 Listening/Thinking/Speaking）定案、固定配色。切片 3（IMU 互動）、切片 4（直橫版面）尚未做。
- **架構文件**：`docs/M1B-架構.md`（切片 B1~B6）、`docs/StickS3 介面規格.html`。
- **待辦**：B3b-1 音質再驗 → B3b-2 音訊串中控 → B3c 接 GPT-Live 即時語音 → B4 對話列文字 → B5 新聞快報。另 M1-A 切片 3/4、M3 等新 RF 版。
- **未 push**：本機有多個 commit 未 push（Miles 自行 `git push`）。

### 2026-11-16｜M1-A 切片 2：九種表情定案｜Cowork（VS Code 驗收）
- 做了什麼：`ui_expression` 擴充為 9 種表情，固定配色、平滑過渡：Idle(青) / Listening(綠·漣漪) / Thinking(藍·上看游移＋三點) / Speaking(白·嘴巴隨音量) / Happy(黃·笑眼) / Angry(紅·八字眉) / Scared(紫·發抖冒汗) / Sleepy(灰藍·半閉) / Asleep(暗灰藍·閉眼飄Z)。眼睛放大（32×46）、呼吸改上下平移（±9px、約 2 秒）。Desktop 按 A 循環切換（測試用，切片 3 接 IMU 後移除）。`setSpeakLevel()` 預留給 M1-B 接喇叭音量。
- 驗收結果：**通過**（實機逐一確認 9 種表情、配色、過渡）。
- 下一步：Listening/Speaking 的真實觸發要等 M1-B（麥克風/喇叭）；先做 M1-B 架構。切片 3、4（IMU 互動、直橫版面）稍後補。
- 並行：M1-B 架構規格見 `docs/M1B-架構.md`；介面規格 HTML 的九表情與對話狀態更新，交由另一 session 處理。

### 2026-11-16｜M1-A 切片 1：離線 Idle 表情｜Cowork（VS Code 燒錄驗收）
- 做了什麼：新增 `firmware/src/ui_expression.{h,cpp}`（PSRAM sprite 畫兩個青色圓角眼睛、呼吸起伏＋隨機眨眼）；`main.cpp` 加 `renderActive()`，Desktop 模式畫表情、其餘模式維持 `ui_status`。
- 驗收結果：**通過**（Build 無紅字、Upload、實機）— 開機即 Idle 表情；B 切模式正常；HidAgent 按 A 急停、B 恢復皆正常。
- 調整：呼吸幅度 ±3→±7、週期放慢（Miles 反映原幅度太小看不出）。
- 下一步：切片 2 — 六種情緒（開心/生氣/驚嚇/想睡/睡著）與眼型，固定配色，平滑過渡。

### 2026-11-16｜M0 實機驗收通過｜（VS Code 燒錄）
- 做了什麼：在 Mac 用 VS Code（PlatformIO）燒錄 `firmware/` M0 骨架、實機驗收。
- 驗收結果：**通過** — 開機顯示 Desktop；B 短按循環四模式、序列埠印 exit→enter；HidAgent 按 A 即急停（紅底 KILLED）、B 恢復；AirMouse 按 A 不停、B 長按 3 秒才停；A/B 交錯連打不當機。
- 待補：Grove 配件偵測（插 CardKB 0x5F／Scroll 0x40）因配件尚未到貨，待到貨補測；目前無配件顯示 None 正常。
- 工具：Miles 用 VS Code 操作（非終端機），後續說明以 VS Code 按鈕為準。
- 下一步：M1-A 切片 1（離線 Idle 呼吸＋眨眼），貼 `docs/KICKOFF_M1A.md` 進 Claude Code CLI 執行。

### 2026-11-15｜介面規格定稿、M1-A kickoff｜Cowork
- 做了什麼：`docs/StickS3 介面規格.html` 收尾（語音對話列：直式 ≤5 行、橫式對話欄寬＝直式對話列高、同比例；資訊頁補橫式；選單直式整頁 5 項／橫式一頁三個）。新增 `docs/KICKOFF_M1A.md`（M1-A 離線表情的 CLI 啟動 prompt，四個切片）。已 commit（未 push）。
- 分工提醒：韌體編譯/燒錄在 Mac 的 Claude Code CLI（有 PlatformIO 工具鏈）；Cowork/claude.ai 這端做規格與文件。
- 下一步（依序執行）：
  1. **M0 實機驗收**（你在 Mac：長按電源鍵約 2 秒到綠燈閃→放開→`pio run -t upload`；照 11-13 紀錄的 5 點清單測）。
  2. **M1-A**：VS Code 終端開 Claude Code CLI（plan mode），貼 `docs/KICKOFF_M1A.md`，一個切片一個切片做。

### 2026-11-15｜UI 與雲端架構定案、文件改名｜Cowork
- 做了什麼：
  - `docs/StickS3 養成計畫.{html,md}` → 改名 **執行計劃**（當規格/說明書用）。
  - 新增 `docs/StickS3 介面規格.html`（介面說明書）：表情（RoboEyes 風＋互動）、語音「上下分區」不蓋表情、資訊頁（密集儀表板）、一頁三個選單、功能模式、直橫雙版面、待辦清單。
  - 執行計劃路線圖改版；`CLAUDE.md` 第 6 節更新開發順序。
- 架構決議（本次對話）：
  - **雲端中控**：Cloudflare Workers＋Durable Objects（存對話、工具呼叫）；StickS3 常連中控（WebSocket 休眠省電），講話時才開 OpenAI 語音連線。
  - **語音**：GPT-Live-1（$0.05/分，Pro 送的 API 額度可付）；後端先全用 GPT（Responses 委派，少一段往返），之後再做可換後端（Claude/Grok）的機制。常駐背景，長按 A／喚醒詞叫出，對講機式一人一句。
  - **快報**：Cloudflare Workers AI 免費額度（10k neurons/日）定時抓新聞摘要＋TTS。
  - **位置**：GPS 取消（板子無 GPS，nRF24/CC1101 收不到手機基地台）。Wi-Fi 定位列未來選項。
- 里程碑定案：M1-A → M1-B → **M1.5 紅外線萬能遙控器（新增，S3 本體）** → M4 → M2 → **M3 無線電工具箱（S3 軌 Wi-Fi/BLE 可現在做；RF 軌 433/nRF 等新板）**。
- 排除/待辦：警用無線電（P25 數位加密，技術不可解＋截收違法）**永久排除**；FM 收音機（需 TEA5767、佔 Grove 孔）、航空/航管監聽（需 RTL-SDR）列**待辦**，規劃內功能完成後再評估。
- 下一步：M0 實機驗收（不需 RF Pack）→ M1-A 表情。

### 2026-11-13｜養成計畫文件更新＋RF 開發規則調整｜Cowork
- 做了什麼：`docs/StickS3 養成計畫.html` 新增「目前進度」（M0～M4 路線圖＋RF Pack 換貨支線＋養成關卡對照表）與「M0 圖解」（可操作的畫面／按鍵模擬器、狀態機圖、開機與主迴圈流程、M0 驗收清單）；第 6 關加上換貨註記。`.md` 同步更新（含 Obsidian 相容的 Mermaid 圖）。
- 新規則：RF Pack 換貨期間**開發不必避開 RF（M3 程式照寫）**，只有燒錄實測要等新板通過 v5＋v6 驗收。已同步改 `CLAUDE.md` 第 6 節與本檔「目前狀態」。
- 下一步：M0 實機驗收（不需裝 RF Pack）→ M1-A。

### 2026-11-13｜CC1101 收訊複驗（汽車鑰匙）｜claude.ai
- 做了什麼：刷回 Bruce 1.16.1（勾 Erase flash 後 RF Module 會重置成 `M5 RF433T/R`，要改回 `CC1101`）；用福特 Kuga 鑰匙（滾動碼、FSK 調變）只做接收測試。
- 驗收結果：Bruce「Spectrum」無反應——查原始碼（`rf_spectrum.cpp`）確認它只畫 OOK 開關脈衝，FSK 鑰匙本來就看不到。改用「RSSI Spectrum」固定 433.92MHz：有小尖峰，**但不按鑰匙時也會出現同樣大小的小尖峰**（背景雜訊），無法和按鍵對應 → **鑰匙測試不成立**。範圍掃描（300-348／387-464／779-928／All）也無明顯反應（掃頻輪詢可能錯過短脈衝）。
- 判定：**CC1101 收訊功能仍未確認**。目前唯一正面證據是 11-12 公司大門遙控器（OOK，433.92）在 Spectrum 上時有時無、不按時無線。SPI 讀寫（v5）已確認正常，問題範圍縮小在 RF 前端（天線、匹配、靈敏度）或測試訊號源（鑰匙頻率不是 433.92、電池弱）。
- 學到（Bruce 操作）：A＝選擇、B＝下一個、B 雙擊＝上一個、B 長按＝返回；頻率／範圍選單要在 RSSI Spectrum 畫面內按 A 叫出（Config → RF Frequency 只是輸入預設頻率的鍵盤）。
- **追加診斷（同晚）**：RSSI Spectrum 在 433.92／315／868 每個頻率都是「開頭一下 -20（啟動假訊號）→ 之後恆定約 -55 dBm 的水平直線」。正常背景應約 -95～-105 dBm。**拔掉天線讀值不變；拔 USB、遠離電腦與電子產品讀值仍不變** → 讀值與天線輸入無關，排除外部干擾。
- **再追加**：切換 Bruce 頁面（例如進 RF → Scan/Copy）時會間歇跳出紅底「CC1101 not found」，有時閃一下就進去、有時卡很久。當時電池僅約 12%。賣家疑難排解寫明：模組無回應／卡住 → 確認側邊電源開關、重插確保排針沒歪、**電池保持 30% 以上**。→ 在宣判硬體故障前，先排除**供電不足與排針接觸**（充電 >50%、重插並鎖螺絲、完整斷電重開）再重測。
- 補充症狀：RF → **SquareWave Spec 每次必定**跳出「CC1101 not found」並退回主選單（Bruce 在 `initRfModule()` 呼叫 `getCC1101()` 讀 VERSION 得到 0 時顯示此錯誤，代表當下 SPI 讀不到晶片回應）。
- **11-13 上午複測**：電池充飽後，RSSI Spectrum 433.92 仍恆定約 -50 dBm → **排除電量不足**，維持換貨判斷。
- **11-13 上午**：已重插 RF Pack 並重測，仍恆定約 -50 dBm → 排除接觸不良。**結論：CC1101 接收故障，維持換貨。**
- **決定**：為趕在退換期內，先以「CC1101 接收故障」向 PINGEQUA（support@pingequa.com）**申請換貨**（英文信已備，附 RSSI 有／無天線照片與影片）。若充電＋重插後問題消失，再回信取消。
- 判定：**CC1101 接收路徑疑似故障**（RF 前端、匹配電路或 PLL 未鎖定），但尚未排除「Bruce 在 StickS3 上的 RSSI 讀取有問題」這個軟體可能。
- 下一步：用自有韌體（SmartRC，已驗證 SPI 正常）做 `acceptance-test` v6：設 433.92MHz 進 RX → 讀 MARCSTATE（應為 0x0D=RX）、FSCAL1（0x3F 代表 PLL 未鎖定）、RSSI dBm 數值＋峰值保持，有天線／無天線／按鑰匙三種情境對照。若仍「恆定且與天線無關」→ 附數據向 PINGEQUA 申請換貨（退換期約 2026-11-26 前）。

### 2026-11-13｜M0 韌體骨架｜Claude Code CLI
- 做了什麼：按鍵配置決議（方案 1：A＝做事／模式 2 急停，B＝切模式／1～3 秒重印／3 秒急停備援，電源鍵不用），寫回 `CLAUDE.md` 第 2 節與鐵則三。新建 `firmware/`：`config.h`、`input_hal`（Grove 掃描＋按鍵事件）、`killswitch`（G11 中斷＋最高優先 task 關輸出閘門，兩段式）、`mode_manager`（四模式＋Safe，先 exit 再 enter）、`ui_status`（狀態畫面＋序列埠報告）、`main.cpp`。
- 驗收結果：Mac 上 `pio run` 逐步編譯皆 SUCCESS（RAM 7.3%、Flash 16.6%）。**尚未燒錄實測。**
- 驗收步驟：(1) 開機顯示 Desktop＋輸入裝置；(2) B 短按循環四模式，序列埠印 exit→enter；(3) 插 CardKB／Scroll 開機能辨識；(4) HidAgent 按 A → 紅底 KILLED，B 短按恢復；AirMouse 按 A 不急停、B 按住 3 秒急停；(5) A／B 交錯連打 20 次不當機。
- 遇到的坑／新規則：M5Unified 預設不 `begin()` `Ex_I2C`，selftest 的 Grove `(none)` 結果可信度待確認 → 韌體明確 `begin()`（已寫入 `CLAUDE.md` 鐵則一）。
- 下一步：實機驗收 M0 → M1-A 模式 1 離線表情。

### 2026-11-12｜CLAUDE.md 更新與跨 Session 進度機制｜claude.ai
- 做了什麼：`CLAUDE.md` 修正文件路徑、新增按鍵表、已驗證腳位表、鐵則五（RF Pack 驅動規則）、實測版本；`docs/KICKOFF_PROMPT.md` 改寫為 M0 骨架 prompt；新增本檔，並在 `CLAUDE.md` 第 9 節規定各 Session 結束時要更新本檔。
- 下一步：在 Mac 的 VS Code 終端機啟動 Claude Code CLI，以 plan mode 貼上 M0 prompt。
- 待辦／未決：正面鍵 A 職責衝突待決議。

### 2026-11-12｜Git／GitHub 建立｜claude.ai
- 做了什麼：專案移至 `~/Developer/m5sticks3`（移出 iCloud）；建立根目錄 `.gitignore`、`README.md`；推上公開 repo `MilesYP/m5sticks3`。
- 遇到的坑：`.pio/` 動輒數百 MB 必須忽略；賣家網頁離線存檔（PINGEQUA）有著作權，不上傳。

### 2026-11-12｜硬體驗收四階段｜claude.ai
- 做了什麼：`selftest/`（StickS3 本體自檢）、Bruce 1.16.1 基準（RF Pack）、`acceptance-test/` v1→v5（自有工具鏈讀寫 CC1101／nRF24）。
- 驗收結果：全數通過。CC1101 為 TI 原廠（PARTNUM 0x00／VERSION 0x14），寫入→讀回 16/16、400 次讀取 0 錯誤；nRF24 PASS。詳見 `docs/hardware-acceptance-test.md`。
- 遇到的坑／新規則（已寫入 `CLAUDE.md` 鐵則五）：手寫 CC1101 SPI 時序會每隔一次交易出錯 → 一律用 SmartRC 函式庫；RF 走 SPI2（螢幕佔 SPI3）；RF Pack 不需 EXT 5V；CC1101 不隨 ESP32 重開機斷電。
- 待辦：汽車鑰匙複驗 CC1101 收訊穩定度。
