# 本機角色替換（ui_persona）— 規格與交接說明

> 目的：Desktop 表情頁的「臉」可以換成一組自己的像素動畫，**只存在自己的電腦**，不進公開 repo。
> 公開 repo 裡沒有任何角色素材：沒有 `persona_assets.h` 時，韌體就是原本的合成波表情。
> 對話區、斷線圖示、交叉淡化、IMU 互動、語音狀態都沿用原本系統，角色只負責「臉的區域畫什麼」。

## 1. 檔案放哪裡

```
firmware/persona_local/          ← 素材（.gitignore，不會 commit）
  persona.json                   ← 選填：名字與各狀態播放速度
  idle/000.png 001.png …         ← 必要
  listening/ thinking/ speaking/ happy/ angry/ scared/ sleepy/ asleep/   ← 選填，缺的用 idle 代替
firmware/src/persona_assets.h    ← tools/sprite2h.py 產生（.gitignore，不會 commit）
```

## 2. 素材規格

| 項目 | 規格 |
| :-- | :-- |
| 狀態（資料夾名） | `idle` 待機、`listening` 聆聽（按住 A 說話）、`thinking` 等回覆、`speaking` Friday 說話、`happy` 被拍（開心）、`angry` 被拍（生氣）／睡著被吵醒、`scared` 用力搖、`sleepy` 靜置 6 秒、`asleep` 靜置 11 秒 |
| 幀 | PNG，檔名排序＝播放順序，每個狀態循環播放；建議 4～8 幀 |
| 尺寸 | **原生像素尺寸**（一格＝一個像素，不要先放大），所有幀相同，最大 64×64；建議 24～40 見方 |
| 放大 | 韌體自動整數倍放大置中：直式臉區約 135×146（對話區打開時變矮），橫式約 162×118 → 32×32 的圖直式 ×4、橫式 ×3 |
| 透明 | alpha < 128 視為透明（底色是黑） |
| 顏色 | 全部幀合計 ≤ 255 色（像素圖通常遠低於此；超過可加 `--quantize`） |
| 速度 | `persona.json` 的 `fps`，預設 8，上限 30 |
| 大小 | 工具會印出 KB 數；超過 1.5MB 會警告（app 分割區約 3.3MB） |

`persona.json` 範例：

```json
{ "name": "my-persona", "fps": { "idle": 4, "speaking": 12, "scared": 14 } }
```

## 3. 轉檔與燒錄（在 Mac、repo 根目錄）

```bash
python3 -m pip install --user pillow                 # 只要一次
python3 tools/sprite2h.py                            # 讀 firmware/persona_local/ → 寫 firmware/src/persona_assets.h
# 照平常：VS Code 開 firmware/ → PlatformIO「→」燒錄
```

- 先驗流程可以用測試素材（色塊＋幀號，不是角色）：`python3 tools/sprite2h.py --demo firmware/persona_local`
- 序列埠 `q0`／`q1`：同一支韌體在原本表情與角色之間切換對照。
- 回到原本表情：`python3 tools/sprite2h.py --clean` 再燒錄。
- 提交前確認：`git status` 不應出現 `persona_local/` 或 `persona_assets.h`。

## 4. 驗收

1. 開機進 Desktop，臉的區域是角色、四角有琥珀色取景框，`idle` 在循環。
2. `y0`～`y8` 逐一預覽 9 個狀態，缺的狀態顯示 `idle`。
3. 按住 A 說話：`listening` → `thinking` → `speaking`，對話區照常在 A 鍵側。
4. 直式、橫式 A 左、橫式 A 右都置中、不被對話區蓋住。
5. `j` 看迴圈時間仍 ≤ 25ms。

## 5. 交接 prompt（貼給負責準備素材或轉檔的人／AI）

```
你要幫 M5S3Pack（M5StickS3 桌面語音裝置）準備一組本機角色動畫。規格以 repo 的 docs/persona-swap.md 為準：

1. 素材：每個狀態一個資料夾，放在 firmware/persona_local/<狀態>/，PNG 依檔名排序播放。
   狀態：idle listening thinking speaking happy angry scared sleepy asleep（idle 必要，其他缺了會用 idle）。
   每個狀態 4～8 幀、循環播放；所有幀同一個原生像素尺寸（建議 32×32，最大 64×64），不要預先放大；
   背景透明；全部合計 ≤ 255 色。可選 firmware/persona_local/persona.json 設定 name 與各狀態 fps。
2. 轉檔：在 repo 根目錄執行 python3 tools/sprite2h.py，確認輸出的尺寸、色數、各狀態幀數、KB 數。
   出錯時依錯誤訊息修素材（尺寸不一致、超過 64、超過 255 色、缺 idle），不要改工具或韌體。
3. 燒錄：PlatformIO 編譯上傳 firmware/。序列埠 y0～y8 逐一預覽狀態、q0/q1 對照原本表情、j 確認迴圈 ≤ 25ms。
4. 不要 commit：firmware/persona_local/ 與 firmware/src/persona_assets.h 都在 .gitignore；
   提交前用 git status 確認沒有出現。
```
