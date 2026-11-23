#!/usr/bin/env python3
#  用法（repo 根目錄）：python3 tools/gen_air_labels.py <NotoSerifTC-Black.otf> firmware/src/air_labels.h
#  共用狀態列：python3 tools/gen_air_labels.py --set status <NotoSerifTC-Black.otf> firmware/src/status_labels.h
#  字型：github.com/notofonts/noto-cjk 的 Serif/SubsetOTF/TC/NotoSerifTC-Black.otf（SIL OFL 1.1，不進 repo）。
#  11-23 Miles：只有標題、狀態字、六角鍵、底部斜紋帶用粗明體；裝置名稱與提示句用 efontTW_12（同表情頁對話，不加粗）。
# 體感畫面固定字（Noto Serif CJK TC Black，同 B 切模式過場）→ 1-bit 點陣圖，用文字本身查表。
# 每種字級固定字型大小、固定行高（同一字級基線對齊），寬度依文字。
import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

# 字級：名稱 → 字型 px
SIZES = {"key": 18, "head": 15, "item": 14, "body": 13, "cap": 12}
WORDS = {
  "key":  ["左", "捲", "右", "停"],
  "head": ["等待連線", "返回選單"],
  "item": ["已連線"],
  "body": ["選擇裝置", "關閉", "廣播中", "中斷連線", "按住 B",
           "A 連線", "A 按住 1 秒 連線", "B 按住 3 秒 回選單", "放開 取消"],
  "cap":  ["按輪", "轉輪", "A短", "A長", "B", "無"],
}

STATUS_WORDS = ["待機中", "聆聽中", "思考中", "說話中", "開心", "警報",
                "驚嚇", "想睡", "睡眠中", "充電中", "充飽", "體感"]
STATUS_PX = 11

def main(font_path, out, label_set):
    sizes = SIZES if label_set == "air" else {"status": STATUS_PX}
    words = WORDS if label_set == "air" else {"status": STATUS_WORDS}
    namespace = f"{label_set}_labels"
    lines = ["// 自動產生：tools/gen_air_labels.py（不要手改）。字型 Noto Serif CJK TC Black，SIL OFL 1.1。",
             "// 1-bit、每列 MSB 先、補滿整數 byte（M5GFX drawBitmap 格式）。同一字級高度相同、基線對齊。",
             "#pragma once", "#include <stdint.h>", "#include <string.h>", "", f"namespace {namespace} {{",
             "struct Bmp { const char* text; uint16_t w, h; const uint8_t* bits; };", ""]
    table, n = [], 0
    for level, px in sizes.items():
        f = ImageFont.truetype(font_path, px)
        _, top, _, bot = f.getbbox("國體")   # 這一級的上下界
        for text in words[level]:
            l, _, r, _ = f.getbbox(text)
            img = Image.new("1", (r - l, bot - top), 0)
            ImageDraw.Draw(img).text((-l, -top), text, font=f, fill=1)
            if label_set == "status":
                # 字型度量含空白列；保留共用基線並檢查最下方的筆畫。
                ink = img.getbbox()
                if not ink or ink[3] > STATUS_PX:
                    raise ValueError(f"狀態字超出 {STATUS_PX}px：{text}")
                img = img.crop((0, 0, ink[2], STATUS_PX))
            h = img.height
            w = img.size[0]; px_ = img.load(); data = []
            for y in range(h):
                for bx in range(0, w, 8):
                    v = 0
                    for k in range(8):
                        if bx + k < w and px_[bx + k, y]: v |= 0x80 >> k
                    data.append(v)
            lines.append(f"// {text}（{level} {px}px）{w}x{h}")
            lines.append(f"static const uint8_t k_{n}[{len(data)}] = {{")
            for i in range(0, len(data), 16):
                lines.append("  " + ", ".join(f"0x{v:02X}" for v in data[i:i+16]) + ",")
            lines.append("};")
            table.append(f'  {{"{text}", {w}, {h}, k_{n}}},')
            n += 1
    lines += ["", "static const Bmp kAll[] = {"] + table + ["};", "",
              "inline const Bmp* find(const char* text) {",
              "  for (const Bmp& b : kAll) if (strcmp(b.text, text) == 0) return &b;",
              "  return nullptr;", "}", f"}}  // namespace {namespace}", ""]
    Path(out).write_text("\n".join(lines), encoding="utf-8")
    print(f"寫入 {out}（{n} 個）")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--set", choices=("air", "status"), default="air")
    parser.add_argument("font_path")
    parser.add_argument("output")
    args = parser.parse_args()
    main(args.font_path, args.output, args.set)
