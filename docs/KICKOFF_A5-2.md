# 接手啟動 prompt：M1-A / A5-2 資訊頁雲端資料（天氣、快訊、語音額度美金）

> 用法：開新的 Claude Code（或 Cowork）session，把下面整段貼進去。
> 產生於 2026-11-20（M1-A / A5-1 完成之後）；11-20 晚改版：EVA 風（q0／q1）與 P1 省電已併入，第一件事改為燒錄確認 P1。

---

我在接手 M5StickS3 桌寵專案（機器名 M5S3Pack，助理叫 Friday；不要叫「小S」）（~/Developer/m5sticks3，公開 repo MilesYP/m5sticks3）。這一片是 **M1-A / A5-2 資訊頁雲端資料**：中控（Cloudflare Worker＋Durable Object，`cloud/`）把天氣、快訊、語音額度送給裝置，填進 A5-1 已留好的位置。
我是 PM、不親手寫 code；燒錄用 VS Code 的 PlatformIO 按鈕；中控用 `cd cloud && npx wrangler deploy`。回覆用繁體中文、術語要解釋。技術選擇請直接定，真的無法判斷才問我；動工前給一次計畫與要改的檔案，我說「開始實作」才寫。Cowork／橋接環境不能 git push（commit 前先請我允許刪檔），commit 後給我一行 `cd ~/Developer/m5sticks3 && git push`。橋接環境無法完整編譯：韌體用模擬標頭做語法檢查（**模擬標頭要跟真函式庫的型別結構一樣**：`M5Canvas` 是 `m5gfx::M5Canvas` 的別名、要有 Arduino 的 `RX`／`TX` 常數，見 CLAUDE.md 鐵則九第 8 點）、純邏輯用 g++ 測；畫圖用 AddressSanitizer 跑「每種畫面 × 3 種拿法」並輸出圖檔自己看。

【開工第一件事：燒錄確認 P1 省電版】
HEAD 之後的最新版本是 **P1 省電＋外觀微調**（`AUDIO_BUILD_TAG` 25-p1，Miles 11-20「省電先這樣、之後再優化」，已提交但沒有完整實機驗收）。請我 Build／Upload 後確認並貼 log：
1. 編譯通過（上一輪 `imu_pet.h` 少 `<stdint.h>` 已修）；開機有 `[pwr] 省電 開｜背光 原本 xx → 正常 110…`。
2. 表情頁放著 6 秒變暗、11 秒微光（`[pwr] → 想睡／睡著｜CPU 80 MHz`）；資訊頁放著 10 秒變暗、30 秒微光；拿起、拍、按 A 立刻亮。**睡著被拍要生氣**（CPU 80MHz 時每張畫面變慢，要確認拍打還抓得到；不行就 `config.h` 的 `PWR_CPU_SCALE` 改 false）。
3. 按住 A 講一輪：語音照常、無滋滋聲；貼 `[perf] 錄音…` 那行（A5-1 欠的錄音中迴圈時間，目標 25～30ms）。
4. q0／q1、短按 A、B 過場、急停照常；q0 光暈變小、NET LINK 洋紅。
有問題先修再進 A5-2；結果寫回 `docs/PROGRESS.md` 最上面那筆（P1）。

【先讀】
- CLAUDE.md：鐵則七（連網／中控：DO 閒置 10 秒會休眠、跨訊息的狀態存 `ws.serializeAttachment()`；第 12 點 GPT-Live 照連線時間計費、`session.closed` 的 `usage.seconds`、每日額度）、鐵則九（資訊頁、換頁 `ui_fx`、字型規則）
- docs/PROGRESS.md 最上面 3 筆（P1 省電、UI EVA 風、M1-A / A5-1）；`docs/persona-swap.md`（q0／q1 與本機角色）
- CLAUDE.md 鐵則十（省電：資訊頁靜置 10 秒變暗、30 秒微光＋CPU 80MHz、Wi-Fi 深度省電）
- docs/StickS3 執行計劃.md 的 A5-2、A4b 列；docs/StickS3 介面規格.html 第 3 節（天氣、快訊、RF/IR 計數的原始設計）
- 中控：`cloud/src/worker.js`（路由、token）、`voice_session.js`（DO、WebSocket）、`gpt_turn.js`（每日額度 `quotaLimits`、`quota:<台北日期>` 存 storage、`usage` 訊息）、`cloud/wrangler.toml`（`[vars]`、secret）、`cloud/README.md`
- 裝置：**資訊頁有兩套**——q0 合成波 `firmware/src/ui_info.cpp`（`drawClock` 的 `GPT -`、`drawWeather`、`drawNews` 是 A5-2 的位置）、q1 EVA `firmware/src/ui_info_eva.cpp`（目前只有 `news()` 的「快訊 --」，天氣與額度要另外排位置）；兩套共用 `ui_info::drawAll` 收集的 `ui_info_eva::Status`，雲端資料建議放進同一個 Status 給兩邊畫；另看 `net_hub.*`（收件佇列、`onText` 等回呼的寫法）、`voice_turn.cpp`（收到中控訊息的處理範例）

【這片要做】
1. **語音額度兩行（Miles 11-20 選 A＋B，美金）**
   - A：今日花費／每日上限：中控用連線秒數（`usage.seconds`）× GPT-Live 官方單價估算；每日上限改用美金設定（例如 `DAILY_MAX_USD`，取代或並存原本的 `DAILY_MAX_TURNS`／`DAILY_MAX_AUDIO_SEC`，做法你定並說明）。**單價、計費單位動手前先查 developers.openai.com 最新官方資料**（鐵則七第 12 點：模型、價格會變）。
   - B：帳上餘額＝儲值金額（中控變數，Miles 儲值後手動改）−已花費。已花費來自 OpenAI **Costs API**（`GET /v1/organization/costs`，需要 **Admin API key**，只能放 `wrangler secret`；按天彙總、有延遲）。OpenAI 沒有能讀預付餘額的 API（11-20 查證）。Admin key 的建立與存放步驟要寫成一行指令（照 B3.5 token 的做法：不經剪貼簿、不印出、不叫 Miles 貼給你）。
   - 顯示：直式時鐘磚第 4 行**一行放不下完整格式**（小字一行約 14～16 字）→ 直式 `$0.42/2 餘8.37`（上限只寫整數、餘額省 `$`），今日花費依用掉上限的比例變色（<70% 綠、≥70% 黃、≥90% 紅）；橫式完整 `$0.42/$2.00 餘$8.37`。沒資料／斷線顯示上次的值（或 `GPT -`）。
   - 送法：中控在裝置連上時、每輪對話結束後、每日換日時送一則（例如 `{"t":"info","usd_today":…,"usd_cap":…,"usd_left":…}`），裝置存起來給資訊頁畫。
2. **天氣**：資料來源先查證再提 2～3 個方案（免費額度、需不需要 API key、台灣地區、更新頻率、條款）給我選。地點預設台北（`cloud/persona/miles.local.md` 有使用者所在地區，不進 repo）。WEATHER 磚：圖示（晴／雲／雨，向量畫）＋溫度＋地點與天氣描述＋濕度、降雨機率；直式磚塊 97×34px、橫式 112×34px（實際像素），中文用 `efontTW_12`。
3. **快訊**：資料來源同樣先查證提方案（例如新聞 RSS 或其他免費來源；不要每次都花 OpenAI 的錢）。快訊磚直式 131×44px、橫式 86×27px，2～3 則標題、一行約 10 個中文字，要截斷或輪播（你定）。有內容時不畫地平線、沒內容時畫。
4. **已存訊號數（RF／IR）**：M1.5／M3 還沒做，這片不放或顯示「-」（你定並說明）。
5. 中控定時更新（Cloudflare Cron Trigger 或連上時抓、快取在 DO storage，你定）；裝置斷線時顯示上次資料並標示資料時間。
6. 序列埠：收到雲端資料時印一行摘要；可加指令要中控立即重送（字母你挑，避開已用的 w/s/p/g/f/u/m/x/r/b/i/k/e/j/v/c/h/o/y/n/t/q/z/l/#/?）。
7. **兩套資訊頁都要顯示**：q0 照上面的磚塊尺寸；q1 EVA 版沿用它的琥珀色、六角標籤與點陣字風格，版面你提案（直式、橫式 A 左／A 右）。沒有角色素材時只有 q0，要確認 q0 正常。
8. 版本標記 `AUDIO_BUILD_TAG` 改 `26-a5b`。

【限制】
- 不要動 A3／A4 的互動門檻、拿法判斷、語音流程；急停最高優先；主迴圈不可阻塞（收資料走 `net_hub` 收件佇列，網路只在網路 task）。
- 資訊頁每秒最多重畫一次；換整頁一律經 `ui_fx`。
- 不要動 `power_save` 的分級規則；收到雲端資料不可把裝置叫醒（只更新資料，下一次重畫時顯示）。睡著時 CPU 80MHz，資訊頁重畫會變慢，量一下。
- `firmware/persona_local/`、`firmware/src/persona_assets.h` 不進 repo（提交前 `git status` 確認）。
- repo 公開：API key、Admin key、儲值金額以外的個資不進 repo；`wrangler tail` 輸出含 token 要遮。
- 完成後更新 `cloud/persona/friday.md`（資訊頁雲端資料從「規劃中」移到「現在會做的事」），並提醒我跑 `cloud/persona.sh`。

【驗收】
1. 資訊頁顯示天氣、快訊、語音額度兩行，且會更新；斷線時顯示上次資料。
2. 講一輪話後，今日花費有增加、跟 OpenAI 後台當天的數字大致對得上；上限依美金設定，超過會被拒絕並回待機（照 B3c 的額度錯誤流程）。
3. 帳上餘額跟 OpenAI 後台的餘額大致對得上（Costs 有延遲，誤差說明清楚）。
4. q0 與 q1 的直式、兩種橫式都不擠不切字；迴圈 ≤25ms；急停、切模式、B 過場照常。

【收尾】
通過後：CLAUDE.md 補規則；PROGRESS.md 最上面加一筆、改「目前狀態」；執行計劃 md/html 把 A5-2 標 ✅、下一片改 A4b；寫 `docs/KICKOFF_A4b.md`（充電表情：B 閃電眼；「充飽」判斷用電壓，規則見 CLAUDE.md 鐵則九第 11 點、數據 `docs/lab/chg-1120-1651.txt`）；更新 friday.md；git commit。

【已知待辦（不在這片）】
- A4b 充電表情（實測已完成，見上）。
- 體驗細調（整體完工後）：彈珠台、拍打、看方向、表情節奏與配色。
- P1 省電優化（量測後再做）：深度休眠、背光數值、EVA 頁迴圈時間。
- 角色生成 skill（以 `tools/sprite2h.py` 與 `docs/persona-swap.md` 素材格式為基礎；角色討論在另一個 session）。
- G2 Grove 功能驗收（`grove-test/`）。
