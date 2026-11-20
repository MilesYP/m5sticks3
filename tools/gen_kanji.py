#!/usr/bin/env python3
# ─────────────────────────────────────────────────────────────────────────
#  gen_kanji.py：把 EVA 風介面用到的「大字」轉成 1-bit 點陣圖標頭檔（firmware/src/kanji_bitmaps.h）
#
#  為什麼：裝置上沒有粗明朝體，整套中文字庫又太大 → 只把用到的幾個詞預先畫成黑白點陣，存在 flash。
#  字型：Noto Serif CJK TC Black（SIL Open Font License 1.1，可自由使用與內嵌）。
#  格式：每列從最高位元（MSB）開始、每列補滿到整數個 byte——跟 M5GFX drawBitmap() 的格式相同。
#
#  用法（在 repo 根目錄）：
#    python3 -m pip install --user pillow
#    python3 tools/gen_kanji.py --font "/path/to/NotoSerifCJK-Black.ttc"
#  macOS 沒有內建這套字型：到 https://github.com/notofonts/noto-cjk/releases 下載 Serif 的 Black，
#  或直接用 repo 裡已產生好的 kanji_bitmaps.h（只有要加新詞時才需要重跑）。
# ─────────────────────────────────────────────────────────────────────────
import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

# (程式裡用的 key, 要畫的字, 字高上限 px, 寬度上限 px)
WORDS = [
    ("voice",  "語音",     36, 125),   # 模式 1 Desktop（Friday）
    ("hid",    "外部操作", 36, 125),   # 模式 2 HID 外掛
    ("rf",     "射頻",     36, 125),   # 模式 3 RF 工具
    ("air",    "體感",     36, 125),   # 模式 4 空中滑鼠
    ("kill",   "緊急停止", 30, 125),   # 急停頁（之後改 KILLED 畫面用）
    ("intpow", "內部",     11, 40),    # 資訊頁：電池供電
    ("extpow", "外部",     11, 40),    # 資訊頁：USB 供電
]

def render(font_path, text, max_h, max_w):
    size = max_h + 8
    while size > 6:
        f = ImageFont.truetype(font_path, size, index=0)
        l, t, r, b = f.getbbox(text)
        if b - t <= max_h and r - l <= max_w:
            break
        size -= 1
    img = Image.new("1", (r - l, b - t), 0)
    ImageDraw.Draw(img).text((-l, -t), text, font=f, fill=1)
    return img

def pack(img):
    w, h = img.size
    px = img.load()
    out = []
    for y in range(h):
        for bx in range(0, w, 8):
            v = 0
            for k in range(8):
                x = bx + k
                if x < w and px[x, y]:
                    v |= 0x80 >> k
            out.append(v)
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", required=True, help="Noto Serif CJK TC Black 的路徑（.ttc／.otf）")
    ap.add_argument("--out", default="firmware/src/kanji_bitmaps.h")
    a = ap.parse_args()
    lines = [
        "// 自動產生：tools/gen_kanji.py（不要手改）。字型 Noto Serif CJK TC Black，SIL OFL 1.1。",
        "// 1-bit、每列 MSB 先、補滿整數 byte（M5GFX drawBitmap 格式）。",
        "#pragma once",
        "#include <stdint.h>",
        "#include <string.h>",
        "",
        "namespace kanji {",
        "struct Bmp { const char* key; const char* text; uint16_t w, h; const uint8_t* bits; };",
        "",
    ]
    table = []
    for key, text, mh, mw in WORDS:
        img = render(a.font, text, mh, mw)
        data = pack(img)
        lines.append(f"// {text}  {img.size[0]}x{img.size[1]}")
        lines.append(f"static const uint8_t k_{key}[{len(data)}] = {{")
        for i in range(0, len(data), 16):
            lines.append("  " + ", ".join(f"0x{v:02X}" for v in data[i:i+16]) + ",")
        lines.append("};")
        table.append(f'  {{"{key}", "{text}", {img.size[0]}, {img.size[1]}, k_{key}}},')
    lines += ["", "static const Bmp kAll[] = {"] + table + ["};", "",
              "inline const Bmp* find(const char* key) {",
              "  for (const Bmp& b : kAll) if (strcmp(b.key, key) == 0) return &b;",
              "  return nullptr;",
              "}", "}  // namespace kanji", ""]
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    Path(a.out).write_text("\n".join(lines), encoding="utf-8")
    print(f"寫入 {a.out}（{len(WORDS)} 個詞）")

if __name__ == "__main__":
    main()
