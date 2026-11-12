自學路線圖 · M5StickS3

# StickS3 養成計畫

用你手上的四樣硬體，從完全不會到能自己寫韌體：先讓機器動起來，再一路學會 C/C++、I2C、紅外線與 Sub-GHz 訊號、選單介面，最後把「用語音控制家電」整合成自己的作品。每一關都有動手目標和驗收標準。

**M5StickS3**ESP32-S3 主機・螢幕・紅外線收發・麥克風喇叭・IMU

**Scroll Unit**滾輪編碼器＋按鍵・I2C 0x40・當選單操作

**CardKB**迷你鍵盤・I2C 0x5F・輸入文字與指令

**RF Pack S3**CC1101 433MHz＋nRF24・SPI・Sub-GHz 訊號

0 / 9

## 怎麼用這份計畫

照編號一關一關往下走，每關做完勾「完成」，進度會自己記住。前期不用買齊，**第 0～4 關只要 StickS3 本體**就能開始；Scroll Unit、CardKB、RF Pack 到貨後再接上對應的關卡。

**把 Claude Code 當你的隨身助教。**你不用背語法——遇到看不懂的錯誤、要補完程式、要查某顆晶片的腳位，就把畫面和這份計畫貼給它，請它解釋、生成、逐行帶你讀。你負責理解「為什麼這樣設計」，打字交給它。

0 開箱與環境準備 先確認硬體是活的，再把 Mac 的開發環境裝好收合展開

硬體

StickS3 ＋ USB-C 資料線

程式觀念

韌體/firmware燒錄/flash序列埠

時間

約 1 晚

#### 動手做

1. 用瀏覽器到 flash.pingequa.com 或 bruce.computer/flasher，把 Bruce 韌體刷進去——這一步**不用寫程式**，先證明板子、線、Mac 三者都正常。
2. 在 Mac 裝開發環境：安裝 **VS Code ＋ PlatformIO**（嵌入式開發用的編譯燒錄外掛）。請 Claude Code 幫你一步步裝好、建立第一個 StickS3 專案。
3. 認識硬體地圖：ESP32-S3（大腦）、1.14 吋螢幕、正面按鈕、IMU、紅外線、Grove 孔、背面 Hat2 排針、麥克風喇叭。

#### 里程碑

Bruce 開機畫面出現；PlatformIO 能編譯並上傳一個空白專案到 StickS3 不報錯。

[ ] 完成這一關

1 第一支程式：Hello StickS3 螢幕顯示、讀按鈕、序列埠除錯收合展開

硬體

StickS3

程式觀念

setup/loop變數・函式數位輸入序列埠除錯

時間

約 1～2 晚

#### 動手做

1. 在螢幕印出文字，理解 setup()（開機跑一次）和 loop()（反覆跑）的差別。
2. 讀正面藍色按鈕，按一下讓螢幕計數 ＋1，並用序列埠（Serial Monitor）同步印出數字——學會「用印訊息來除錯」。
3. 把顏色、字體大小、位置玩一遍，熟悉座標。

```
// 用 M5Unified 函式庫，它會自動辨識 StickS3
#include <M5Unified.h>
int count = 0;
void setup() {
  M5.begin();                 // 初始化螢幕、按鈕、電源
  M5.Display.setTextSize(3);
  M5.Display.print("Hello StickS3");
}
void loop() {
  M5.update();                // 每圈更新按鈕狀態
  if (M5.BtnA.wasPressed()) { // 正面按鈕被按下
    count++;
    M5.Display.clear();
    M5.Display.setCursor(0, 0);
    M5.Display.printf("count: %d", count);
    Serial.printf("count=%d\n", count); // 送到序列埠除錯
  }
}
```

按鈕名稱、腳位依 StickS3 版本可能微調，交給 Claude Code 依規格確認。

#### 里程碑

按一下按鈕，螢幕和序列埠都看到數字 ＋1。你看得懂這段每一行在做什麼。

[ ] 完成這一關

2 感測器：讓機器「感覺」 用 IMU 偵測搖晃與傾斜收合展開

硬體

StickS3（內建 IMU）

程式觀念

浮點數條件判斷感測器讀值門檻/threshold

時間

約 1 晚

#### 動手做

1. 讀六軸 IMU 的加速度，印在螢幕上，邊搖邊看數字怎麼變。
2. 設一個「門檻值」：當加速度超過某值就判定為「搖晃」，螢幕顯示一個表情或換底色。
3. 做「螢幕朝下＝睡眠」：用 Z 軸判斷翻面，把背光關掉。

#### 里程碑

搖一搖螢幕有反應、翻過來會「睡著」。你學會把感測器的連續數值變成「事件」。

[ ] 完成這一關

3 接上 Scroll Unit：學會 I2C 外接裝置怎麼跟主機對話收合展開

硬體

StickS3 ＋ **Scroll Unit**（Grove）

程式觀念

I2C 匯流排裝置位址函式庫讀規格書

時間

約 2 晚

#### 動手做

1. 先跑一支「I2C 掃描器」，掃出接在 Grove 上的裝置位址——應該看到 Scroll Unit 的 0x40。這一步讓你真正理解「位址」是什麼。
2. 裝 M5 的 Scroll Unit 函式庫，讀滾輪的轉動值和按鍵狀態，印在螢幕上。
3. 把「轉滾輪＝數字增減、按下＝歸零」做出來。

```
// I2C 掃描器：找出 Grove 上接了哪些裝置
#include <Wire.h>
void setup() {
  Serial.begin(115200);
  Wire.begin();               // Grove 的 I2C 腳位讓 Claude Code 依 StickS3 補上
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0)
      Serial.printf("found 0x%02X\n", a); // 掃到就印位址
  }
}
void loop() {}
```

#### 里程碑

掃描器印出 0x40；轉滾輪時螢幕數字跟著動。你懂了「一條線上掛多個裝置、靠位址分辨」這個核心觀念。

[ ] 完成這一關

4 第一次訊號複製：紅外線 學會你家電視遙控器的碼，再發射出去收合展開

硬體

StickS3（內建紅外線收發）

程式觀念

紅外線協定時序/timing編碼解碼位元運算

時間

約 2～3 晚

#### 動手做

1. 用紅外線接收，把電視遙控器的碼解出來，螢幕顯示協定（如 NEC）和數值。
2. 把學到的碼存成變數，按按鈕時用紅外線發射出去，實際控制電視。
3. 理解「固定碼可複製、滾動碼不行」，並知道只玩自己的設備。

```
// 用 IRremoteESP8266：接收並印出遙控器的碼
#include <IRrecv.h>
#include <IRutils.h>
IRrecv irrecv(IR_RX_PIN);      // 腳位讓 Claude Code 依 StickS3 補上
decode_results r;
void setup() { Serial.begin(115200); irrecv.enableIRIn(); }
void loop() {
  if (irrecv.decode(&r)) {
    Serial.println(resultToHumanReadableBasic(&r)); // 協定＋碼
    irrecv.resume();           // 準備收下一個
  }
}
```

#### 里程碑

按你 StickS3 上的鍵，電視真的開關了。這是你的第一個「複製訊號控制家電」成果。

[ ] 完成這一關

5 接上 CardKB：文字輸入與選單 把操作介面做得好用收合展開

硬體

StickS3 ＋ **Scroll Unit** ＋ **CardKB**

程式觀念

狀態機陣列/清單字串處理事件迴圈

時間

約 2～3 晚

#### 動手做

1. 用 Scroll Unit 做一個可上下捲動、按下選取的選單（這是你之後所有功能的共用骨架）。
2. 接上 CardKB（I2C 0x5F），讀鍵盤輸入，讓你能幫每個項目命名、輸入數值。
3. 學「狀態機」：用一個變數記住現在在哪一頁（主選單／輸入中／執行中），畫面跟著切換。

#### 里程碑

一個滾輪捲、鍵盤打字的選單系統成形。你學會用狀態機組織一個互動程式。

[ ] 完成這一關

6 接上 RF Pack：Sub-GHz 與 SPI 433MHz 無線訊號的擷取與分析收合展開

硬體

StickS3 ＋ **RF Pack S3**（背面螺絲鎖）

程式觀念

SPI 匯流排調變 OOK/ASK固定碼 vs 滾動碼頻率

時間

約 3 晚

#### 動手做

1. 先用 Bruce 的 Sub-GHz 功能擷取一個 433MHz 裝置（你自己的無線插座／門鈴），看它的頻率與波形。
2. 進階：在自己的程式裡用 SPI 驅動 CC1101，接收 OOK 訊號，理解「SPI 和 I2C 的差別」。
3. 把固定碼存下來、重放，驗證能不能控制那個裝置。

#### 里程碑

擷取到自家無線插座的 433MHz 碼並成功重放。你現在同時掌握 I2C（週邊）和 SPI（高速模組）兩種匯流排。

[ ] 完成這一關

7 整合作品：自製統一遙控器 把前面學的全部串起來收合展開

硬體

四樣全上

程式觀念

資料持久化 NVS模組化專案架構

時間

約 1 週

#### 動手做

1. 做一個選單：滾輪選「IR 家電」或「RF 裝置」→ 進去選已存的鍵 → 發射。CardKB 用來新增、命名。
2. 學「資料持久化」：用 Preferences/NVS 把學到的碼存進快閃記憶體，關機也不會不見。
3. 把程式拆成幾個檔案（IR 一塊、RF 一塊、選單一塊），體會「模組化」為什麼重要。

#### 里程碑

一台你自己寫的統一遙控器：能學、能存、能發，重開機資料還在。這是你的第一個完整作品。

[ ] 完成這一關

8 連網與語音：走向 AI 助手 Wi-Fi、API、小智，與串接電腦收合展開

硬體

StickS3（麥克風喇叭）

程式觀念

Wi-Fi/網路JSONAPI 呼叫ESP-IDFMCP

時間

約 1～2 週

#### 動手做

1. 連 Wi-Fi，打一個 HTTP API、解析 JSON 回應，把結果顯示在螢幕——學會機器怎麼上網拿資料。
2. 刷官方「小智」韌體，體驗語音助手；再研究怎麼把麥克風音訊串到電腦／雲端 AI。
3. 進階終點：用 ESP-IDF 在小智裡加一個 MCP 工具，讓你「說一句話」就觸發第 7 關的紅外線／RF 發射——語音控制家電成形。

#### 里程碑

對著機器說「幫我開電視」，它就發射紅外線開電視。你走完了從點燈到 AI 語音整合的一整條路。

[ ] 完成這一關

## 走完之後，你會的

- C/C++ 嵌入式程式：變數、函式、條件、陣列、狀態機、模組化
- 硬體溝通三大介面：GPIO、I2C、SPI，以及怎麼讀規格書找位址與腳位
- 訊號知識：紅外線與 Sub-GHz 的編碼、調變、固定碼與滾動碼
- 連網與整合：Wi-Fi、API、JSON，以及 ESP-IDF 與 MCP 的 AI 整合
- 最重要的：把一個想法，一路做成一台能用的裝置

這份計畫以 M5StickS3、Scroll Unit、CardKB、RF Pack S3 四樣硬體設計。程式片段為示意骨架，確切腳位與函式庫版本請搭配 Claude Code 依你的實機補完。只分析與控制自有設備。