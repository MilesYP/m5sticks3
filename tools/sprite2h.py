#!/usr/bin/env python3
# ─────────────────────────────────────────────────────────────────────────
#  sprite2h.py：把本機角色素材（PNG 序列）轉成韌體標頭檔 firmware/src/persona_assets.h
#
#  素材規格見 docs/persona-swap.md。重點：
#    - 每個狀態一個資料夾：idle listening thinking speaking happy angry scared sleepy asleep
#      （跟韌體 Mood 同順序；缺的狀態會用 idle 代替，idle 必須有）
#    - 資料夾內 PNG 依檔名排序就是播放順序（000.png、001.png …）
#    - 所有幀同一個「原生像素尺寸」，最大 64×64；韌體自動整數倍放大（直式約 ×4、橫式約 ×3）
#    - 透明（alpha < 128）＝不畫；全部幀合計最多 255 種顏色（超過可加 --quantize 自動減色）
#    - 可選 persona.json：{"name": "...", "fps": {"idle": 6, "speaking": 10, ...}}，沒寫的狀態 8 fps
#
#  用法（在 repo 根目錄）：
#    python3 -m pip install --user pillow
#    python3 tools/sprite2h.py                       # 讀 firmware/persona_local/，寫 firmware/src/persona_assets.h
#    python3 tools/sprite2h.py --clean               # 刪掉 persona_assets.h → 韌體回到原本的表情
#    python3 tools/sprite2h.py --demo /tmp/demo      # 產生測試用素材（色塊＋幀號，不是角色），驗證流程用
#  persona_local/ 與 persona_assets.h 都在 .gitignore 裡，不會進公開 repo。
# ─────────────────────────────────────────────────────────────────────────
import argparse, json, sys
from pathlib import Path
from PIL import Image, ImageDraw

STATES = ["idle", "listening", "thinking", "speaking", "happy", "angry", "scared", "sleepy", "asleep"]
MAX_SIDE = 64
WARN_BYTES = 1_500_000   # 超過就警告：會吃掉不少 app 分割區（default_8MB.csv 約 3.3MB）

def pixels(im):
    return im.get_flattened_data() if hasattr(im, "get_flattened_data") else im.getdata()

def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def load_frames(root):
    frames = {}
    for st in STATES:
        d = root / st
        if d.is_dir():
            files = sorted(p for p in d.iterdir() if p.suffix.lower() == ".png")
            if files:
                frames[st] = [Image.open(p).convert("RGBA") for p in files]
    if "idle" not in frames:
        sys.exit(f"找不到 {root}/idle/*.png：idle 狀態一定要有")
    return frames

def build(root, out, quantize):
    frames = load_frames(root)
    cfg = {}
    if (root / "persona.json").exists():
        cfg = json.loads((root / "persona.json").read_text(encoding="utf-8"))
    W, H = frames["idle"][0].size
    for st, fl in frames.items():
        for i, im in enumerate(fl):
            if im.size != (W, H):
                sys.exit(f"{st} 第 {i} 幀尺寸 {im.size} 跟 idle 的 {(W, H)} 不同：所有幀要同一個尺寸")
    if W > MAX_SIDE or H > MAX_SIDE:
        sys.exit(f"原生尺寸 {W}x{H} 超過 {MAX_SIDE}x{MAX_SIDE}：請給「放大前」的像素圖（一格＝一個像素）")

    # 調色盤：index 0＝透明
    colors = {}
    def collect(im):
        for r, g, b, a in pixels(im):
            if a >= 128:
                colors.setdefault(rgb565(r, g, b), None)
    for fl in frames.values():
        for im in fl:
            collect(im)
    if len(colors) > 255:
        if not quantize:
            sys.exit(f"共 {len(colors)} 種顏色，超過 255：加 --quantize 自動減色，或先在繪圖軟體減色")
        print(f"顏色 {len(colors)} 種 → 減成 255 色")
        for st in frames:
            q = []
            for im in frames[st]:
                alpha = im.getchannel("A")
                rgb = im.convert("RGB").quantize(colors=255, method=Image.Quantize.MEDIANCUT).convert("RGB")
                rgb.putalpha(alpha)
                q.append(rgb)
            frames[st] = q
        colors = {}
        for fl in frames.values():
            for im in fl:
                collect(im)
        if len(colors) > 255:
            sys.exit("減色後仍超過 255 色：請先在繪圖軟體統一調色盤")
    palette = list(colors.keys())
    index = {c: i + 1 for i, c in enumerate(palette)}

    data, table = [], []
    for st in STATES:
        fl = frames.get(st, [])
        fps = int(cfg.get("fps", {}).get(st, 8))
        table.append((len(data) // (W * H), len(fl), max(1, min(fps, 30))))
        for im in fl:
            for r, g, b, a in pixels(im):
                data.append(index[rgb565(r, g, b)] if a >= 128 else 0)

    total = len(data) + len(palette) * 2
    name = str(cfg.get("name", "persona")).replace('"', "'")
    L = ["// 自動產生：tools/sprite2h.py（不要手改、不要 commit——已在 .gitignore）",
         "#pragma once", "#include <stdint.h>", "", "namespace persona_assets {",
         f'constexpr const char* NAME = "{name}";',
         f"constexpr uint16_t W = {W}, H = {H};",
         f"constexpr uint16_t PALETTE_N = {len(palette) + 1};",
         "static const uint16_t PALETTE[PALETTE_N] = {0x0000, " + ", ".join(f"0x{c:04X}" for c in palette) + "};",
         "struct State { uint16_t first, count, fps; };",
         "// 順序跟 Mood 相同：" + " ".join(STATES),
         "static const State STATES[9] = {" + ", ".join(f"{{{a}, {b}, {c}}}" for a, b, c in table) + "};",
         f"static const uint8_t FRAMES[{len(data)}] = {{"]
    for i in range(0, len(data), 32):
        L.append("  " + ",".join(str(v) for v in data[i:i + 32]) + ",")
    L += ["};", "}  // namespace persona_assets", ""]
    out.write_text("\n".join(L), encoding="utf-8")
    nframes = sum(len(v) for v in frames.values())
    print(f"寫入 {out}：{name}，{W}x{H}，{len(palette)} 色，{nframes} 幀，約 {total / 1024:.0f} KB")
    print("各狀態幀數：" + "  ".join(f"{s}={len(frames.get(s, []))}" for s in STATES))
    if total > WARN_BYTES:
        print("警告：素材超過 1.5MB，可能讓韌體放不進 app 分割區。減少幀數或縮小原生尺寸。")

def demo(dst):
    # 測試素材：每個狀態一種顏色的方塊＋會動的幀號點，只用來驗證轉檔與播放流程（不是角色）
    cols = [(255,138,0),(57,255,20),(0,200,255),(255,196,0),(255,105,180),(232,20,28),(255,255,255),(120,90,255),(80,80,80)]
    for si, st in enumerate(STATES):
        d = Path(dst) / st
        d.mkdir(parents=True, exist_ok=True)
        for f in range(4):
            im = Image.new("RGBA", (28, 28), (0, 0, 0, 0))
            dr = ImageDraw.Draw(im)
            dr.rectangle([2, 2, 25, 25], outline=cols[si] + (255,), width=2)
            dr.rectangle([6 + f * 4, 12, 9 + f * 4, 15], fill=cols[si] + (255,))
            for k in range(si + 1):          # 底下小點數＝狀態編號＋1
                dr.point((5 + k * 2, 21), fill=(255, 255, 255, 255))
            im.save(d / f"{f:03d}.png")
    (Path(dst) / "persona.json").write_text(json.dumps({"name": "demo", "fps": {"speaking": 12}}), encoding="utf-8")
    print(f"測試素材寫到 {dst}")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="src", default="firmware/persona_local")
    ap.add_argument("--out", default="firmware/src/persona_assets.h")
    ap.add_argument("--quantize", action="store_true")
    ap.add_argument("--clean", action="store_true")
    ap.add_argument("--demo", metavar="DIR")
    a = ap.parse_args()
    if a.demo:
        demo(a.demo); return
    out = Path(a.out)
    if a.clean:
        if out.exists(): out.unlink()
        print(f"已移除 {out}：下次燒錄回到原本的表情"); return
    build(Path(a.src), out, a.quantize)

if __name__ == "__main__":
    main()
