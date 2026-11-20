# 畫面模擬器（tools/sim）

在電腦上用**真的 M5GFX 繪圖核心**（`-DLGFX_LINUX_FB`）跑 `firmware/src/ui_expression.cpp`／`ui_info.cpp`，把每張畫面存成 PPM 圖檔，不用燒錄就能看版面、抓越界（AddressSanitizer）、量相對效能。2026-11-20～21 A5-2／A7 全程用它和 Miles 逐版定案。

```bash
cd ~/Developer/m5sticks3        # 先在 firmware/ 跑過一次 pio run（要有 .pio/libdeps/M5GFX）
bash tools/sim/build.sh face    # 9 種表情 × 3 種拿法，每種存幾張 → tools/sim/.out/ex/q0_<P|AL|AR>_<表情>_<幀>.ppm
bash tools/sim/build.sh info    # 資訊頁直式／兩種橫式、滿資料／空資料／離線 → .out/out/*.ppm（印「超出磚塊回報 N 次」）
bash tools/sim/build.sh anim    # 驚嚇→開心的連續幀 → .out/an/
bash tools/sim/build.sh lowbat  # 對話框＋低電量閃爍 → .out/lb/
bash tools/sim/build.sh bench   # 每幀繪圖微秒數（電腦上的相對值，只拿來比前後版本）
```

- 轉 PNG：`python3 -c "from PIL import Image; Image.open('x.ppm').save('x.png')"`；放大看像素用 `Image.NEAREST`。
- `mock/`：`Arduino.h`（含 `RX`／`TX` 常數）、`M5Unified.h`（`M5Canvas` 是 `m5gfx::M5Canvas` 的別名，**型別結構要跟真函式庫一樣**，見 `CLAUDE.md` 鐵則九第 8 點）、`WiFi.h`。
- 模擬器不跑 `ui_fx`（交叉淡化、部分更新），`present()` 由測試檔自己存圖。
- 表情的表情序列在同一個行程裡連續切換，上一個表情的符號會殘留淡出——看圖時要知道。
- `.out/` 已在 `.gitignore`。
