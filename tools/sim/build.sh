#!/usr/bin/env bash
# 畫面模擬器：用真的 M5GFX 核心（-DLGFX_LINUX_FB）在電腦上畫表情頁／資訊頁，輸出 PPM 圖檔。
# 需求：Linux 或 macOS 的 g++/gcc（有 AddressSanitizer）、先在 firmware/ 跑過一次 pio（才有 .pio/libdeps/M5GFX）。
# 用法：tools/sim/build.sh [face|info|anim|lowbat|motion|bench|air|ms|all]   產物在 tools/sim/.out/，圖在 tools/sim/.out/ex|an|lb|out
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SRC="$ROOT/firmware/src"
LIB="$ROOT/firmware/.pio/libdeps/m5sticks3/M5GFX/src"
[ -d "$LIB" ] || { echo "找不到 $LIB：先在 firmware/ 跑一次 pio run"; exit 1; }
OUT="$HERE/.out"; OBJ="$OUT/obj"; mkdir -p "$OBJ"
F="-O1 -g -fsanitize=address -DLGFX_LINUX_FB -I$LIB"
# macOS 沒有 Linux framebuffer：改走 SDL 標頭分支（只需標頭，不連結 SDL）
[ "$(uname)" = Darwin ] && F="-O1 -g -fsanitize=address -I$LIB -I/opt/homebrew/include"; LDX="-L/opt/homebrew/lib -lSDL2"
# 1) M5GFX 核心（只編一次）
for c in "$LIB"/lgfx/utility/*.c "$LIB"/lgfx/Fonts/efont/*.c "$LIB"/lgfx/Fonts/IPA/*.c; do
  o="$OBJ/$(basename "$c" .c).o"; [ -f "$o" ] || gcc $F -c "$c" -o "$o" &
done; wait
for c in "$LIB"/lgfx/v1/*.cpp; do
  o="$OBJ/$(basename "$c" .cpp).o"; [ -f "$o" ] || g++ -std=gnu++17 $F -c "$c" -o "$o" &
done; wait
CF="-std=gnu++17 $F -I$HERE/mock -I$SRC -w"
cd "$OUT"; mkdir -p ex an lb out
build() { g++ $CF "$HERE/$1" "${@:2}" "$OBJ"/*.o -o "$OUT/${1%.cpp}" -lpthread; echo "built ${1%.cpp}"; }
case "${1:-all}" in
  face)   build sim_face.cpp "$SRC/ui_expression.cpp" "$SRC/ui_kit.cpp" && ./sim_face ;;
  anim)   build sim_anim.cpp "$SRC/ui_expression.cpp" "$SRC/ui_kit.cpp" && mkdir -p ex && ./sim_anim ;;
  lowbat) build sim_lowbat.cpp "$SRC/ui_expression.cpp" "$SRC/ui_kit.cpp" && mkdir -p ex && ./sim_lowbat ;;
  motion) build sim_motion.cpp "$SRC/ui_expression.cpp" "$SRC/ui_kit.cpp" && mkdir -p mo && ./sim_motion 0 && ./sim_motion 2 ;;
  bench)  g++ -std=gnu++17 -O2 -fsanitize=address -DLGFX_LINUX_FB -I$LIB -I$HERE/mock -I$SRC -w "$HERE/bench_face.cpp" "$SRC/ui_expression.cpp" "$SRC/ui_kit.cpp" "$OBJ"/*.o -o bench_face -lpthread && ./bench_face ;;
  info)   g++ $CF -DINFO_SIM "$HERE/sim_info.cpp" "$SRC/ui_info.cpp" "$SRC/info_data.cpp" "$SRC/ui_kit.cpp" "$OBJ"/*.o -o sim_info -lpthread $LDX && mkdir -p out && ./sim_info ;;
  air)    g++ -std=gnu++17 $F -I$HERE/mock_air -I$HERE/mock -I$SRC -w ${AIRMENU_SRC:+-DAIRMENU_SRC=\"$AIRMENU_SRC\"} "$HERE/sim_airmenu.cpp" "$SRC/ui_kit.cpp" "$OBJ"/*.o -o sim_airmenu -lpthread $LDX && mkdir -p air && ./sim_airmenu ;;
  ms)     g++ -std=gnu++17 $F -I$HERE/mock_air -I$HERE/mock -I$SRC -w "$HERE/sim_modeswitch.cpp" "$SRC/ui_kit.cpp" "$OBJ"/*.o -o sim_modeswitch -lpthread $LDX && mkdir -p ms && (cd ms && ../sim_modeswitch) ;;
  all)    "$0" face; "$0" info; "$0" air; "$0" ms ;;
esac
