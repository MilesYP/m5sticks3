# 接手啟動 prompt：M1-A / A8 搖晃液體符號＋A4b 充電表情（同時評估 B5／B6）

> 用法：開新的 Claude Code（或 Cowork）session，把下面「---」以下整段貼進去。
> 產生於 2026-11-21（A7 外觀統一驗收通過之後，HEAD 約 27-a7o）。

---

我在接手 M5StickS3 桌寵專案（機器名 M5S3Pack，助理叫 Friday；不要叫「小S」）（~/Developer/m5sticks3，公開 repo MilesYP/m5sticks3）。這個 session 要做兩片：**A8 搖晃液體符號**、**A4b 充電表情**，並**評估 B5、B6 能不能一起做完**（評估就好，要不要做由我決定）。
我是 PM、不親手寫 code；燒錄用 `cd ~/Developer/m5sticks3/firmware && pio run -t upload -t monitor`（port busy 時先 `kill $(lsof -t /dev/cu.usbmodem1101)`）；中控用 `cd cloud && npx wrangler deploy`。回覆用繁體中文、術語要解釋。技術選擇請直接定，真的無法判斷才問我；動工前給一次計畫與要改的檔案，我說「開始實作」才寫；外觀先出模擬圖給我看再動正式程式。Cowork／橋接環境不能 git push，commit 後給我一行 `cd ~/Developer/m5sticks3 && git push`。

【先讀】
- CLAUDE.md：鐵則八（IMU：每 100ms 一窗、主迴圈讀 IMU、搖晃／拍打判斷、線性加速度、驚嚇彈珠台 `PIN_*`）、鐵則九（第 3 點**螢幕部分更新**、第 9／11 點畫圖與充電「充飽看電壓」、第 12～15 點 A7 畫面規則、**第 15 點不要用大的中文字型**）、鐵則十（省電分級）
- docs/PROGRESS.md 最上面幾筆（A7 整串，27-a7a～a7o）
- docs/StickS3 執行計劃.md 的 A8、A4b 列；docs/StickS3 介面規格.html **第 0 節**（A7 定案畫面）
- firmware/src/ui_expression.cpp：`PIXEL` 分支、`pxHexGrid`／背景快取 `pxBackground`、六角符號（`hexSnap`、`fillHexCell`、`drawBadge`、`badgeFit`）、驚嚇彈珠台（`setMotion`、`s_pin`）、粒子 `emit`／`Part`；firmware/src/ui_fx.cpp（部分更新 `diffRects`）；imu_pet.cpp（搖晃判斷、線性加速度、重力低通）；battery_ui.cpp（電量分級、充電判斷）；power_save.cpp
- **畫面模擬器** `tools/sim/README.md`：`bash tools/sim/build.sh face|info|anim|lowbat|bench` 就能出圖，A7 全程用它定版

【A8 搖晃液體符號（Miles 11-21 提出）】
- 想要的效果：**用力搖**時（`imu_pet` 的搖晃判斷，或彈珠台觸發門檻），畫面上的符號（六角徽章）炸成一顆顆**亮格**，像液體一樣在**背景六角格**上流動；重力方向用 IMU 即時算（拿法轉了、機身翻過來，亮格整片流到新的「下方」）；停手後淡掉、五官歸位。和現有驚嚇彈珠台並存（亮格畫在五官後面、六角格之上）。
- 我在上一個 session 提過的做法（可以推翻）：在六角格上做「流沙」格子模擬——每格每幀往重力方向的空鄰格移動，被擋住就斜滑，堆起來像液體；直式約 70 格、亮 40 格上下，每幀幾十次比對＋填色。
- 限制：全速迴圈 ≤25ms（現在 16～18ms，靠部分更新；亮格到處跑會讓要送的面積變大，要量）；語音中不觸發（同彈珠台）；不能改 A3／A4 的門檻與拿法判斷；Miles 已否決的外觀不要再提：**七格花徽章、五官改六角像素**。
- 驗收：搖得出液體、翻轉流向正確、停手約 0.4 秒後歸位、`j` 全速迴圈 ≤25ms、語音中不觸發、B 急停照常。

【A4b 充電表情】
- 偵測（已查證，見執行計劃 A4b 列）：`isCharging()` 只讀 CHG_STAT（只分充電中／未充電，沒有「充飽」），充電中 CHG_STAT 會來回跳；**「充飽」改看電壓**——有 USB（VBUS＞4.5V）且最近 5 分鐘電池平均 ≥4.10V、5 分鐘上升 <15mV；平均 <4.05V 或拔線就離開（CLAUDE.md 鐵則九第 11 點、數據 `docs/lab/chg-1120-1651.txt`）。
- 樣式：Miles 原本選「B 閃電眼」（充電中＝眼睛挖閃電、亮度呼吸；充飽＝笑眼＋星星），但那是合成波時代的設計 → **配合 A7 像素臉（掃描線眼睛、六角格、狀態標籤）重新提案 2～3 款，先出模擬圖給我選**。狀態標籤要新字（例如「充電」「充飽」）就加進 `tools/gen_kanji.py` 的 `WORDS_UI` 再產生 `ui_labels.h`（不要連結大字型）。
- 規則：充電中不閃低電量（現在就是）；語音、情緒反應優先；省電分級照舊（充電中背光要不要降，你提案）。
- 驗收：插線顯示充電中、充滿顯示充飽、拔線回原本表情、CHG_STAT 來回跳時畫面不閃。可用序列埠 `c<百分比>` 假裝電量；需要的話加一個假裝充電／充飽的指令（字母避開已用的 a/w/s/p/g/f/u/m/x/r/b/i/k/e/j/v/c/h/o/y/n/t/z/l/#/?）。

【評估 B5、B6（只評估、先不做）】
在執行計劃 M1-B 表裡兩片目前是「移到待辦」：
- **B5 新聞快報**：原本規劃「Workers AI Cron→摘要→TTS 推播」。現在中控已經有中央社 RSS 快訊、Cron 每 30 分鐘、GPT-Live 語音（`cloud/src/info.js`、`gpt_turn.js`），評估怎麼做最省：何時播（定時？我按什麼鍵？）、用哪個 TTS／模型與每日成本、裝置端怎麼收（沿用 `voice_turn` 的下載與播放）、睡著時要不要叫醒。
- **B6 Hey Friday 喚醒詞＋MCP／skill**：
  - 喚醒詞：ESP32-S3 上可行的方案（例如 Espressif ESP-SR／WakeNet、microWakeWord 等，**實際可用性、自訂「Hey Friday」要怎麼訓練、授權、需不需要換 ESP-IDF／Arduino 版本，一律查官方最新資料**）；跟現在「睡著時才開麥克風聽聲音」、省電、ES8311 不能同時錄放（鐵則六）的衝突；誤觸發率怎麼量。
  - MCP＋skill：讓 Friday 的 Responses 委派能呼叫 MCP 工具（不連我的電腦），查 OpenAI 官方目前怎麼接遠端 MCP、計費、延遲。
- 產出：一張表（每項：做法、要改哪裡、成本／風險、預估幾片、能不能跟 A8／A4b 同一個 session 做完），附查證來源；我看完再決定。

【限制】
- 急停最高優先；主迴圈不可阻塞；網路只在網路 task。
- 不要動語音流程、Wi-Fi 優先順序邏輯（鐵則七第 12 點）。
- `firmware/src/secrets.h`、`firmware/persona_local/` 不進 repo；不要讀、印出 secrets.h 的值（要檢查只看格式）。
- repo 公開：API key 不進 repo；`wrangler tail` 輸出含 token 要遮。
- 版本標記 `AUDIO_BUILD_TAG`：A8 用 `28-a8a` 起、A4b 用 `29-a4b` 起。

【收尾】
每片通過後：CLAUDE.md 補規則；PROGRESS.md 最上面加一筆、改「目前狀態」；執行計劃 md／html 標 ✅；介面規格第 0 節補新畫面；`cloud/persona/friday.md` 更新「現在會做的事」並提醒我跑 `cloud/persona.sh`；git commit。

【已知待辦（不在這片）】
- 體驗細調（整體完工後）：彈珠台、拍打、看方向、表情節奏與配色。
- 橫式又打開對話框時，符號區比徽章窄，徽章會壓到右眼邊（A7 已知取捨）。
- 偶發：GPT 有收到完整錄音但沒回答（逐字稿 0 bytes，仍計費約 17 秒），再出現就抓 `wrangler tail`。
