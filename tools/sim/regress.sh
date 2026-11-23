#!/usr/bin/env bash
# 全頁回歸比對：把表情、資訊、體感、切模式的模擬圖全部輸出到一個資料夾。
# 用法（repo 根目錄）：
#   bash tools/sim/regress.sh /tmp/before     # 改之前
#   （改程式）
#   bash tools/sim/regress.sh /tmp/after      # 改之後
#   diff -rq /tmp/before /tmp/after           # 預期不變就必須沒有輸出
# 需求同 build.sh（先在 firmware/ 跑過 pio run、先跑過一次 build.sh 產生 .out/obj）。11-23 ui_kit 抽出時 1441 張、0 差異。
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/../.." && pwd)"; SRC="$ROOT/firmware/src"
LIB="$ROOT/firmware/.pio/libdeps/m5sticks3/M5GFX/src"; OBJ="$HERE/.out/obj"
[ -d "$OBJ" ] || { echo "先跑一次 bash tools/sim/build.sh face 產生 $OBJ"; exit 1; }
F="-std=gnu++17 -O1 -g -fsanitize=address -I$LIB -I/opt/homebrew/include -w"
L="$OBJ/*.o -lpthread -L/opt/homebrew/lib -lSDL2"
OUT="${1:?用法：regress.sh <輸出資料夾>}"; rm -rf "$OUT"; mkdir -p "$OUT"; cd "$OUT"; mkdir -p air ex an lb mo out ms
K="$SRC/ui_kit.cpp"
g++ $F -I$HERE/mock -I$SRC $HERE/sim_face.cpp $SRC/ui_expression.cpp $K $L -o sim_face && ./sim_face >/dev/null
g++ $F -I$HERE/mock -I$SRC $HERE/sim_anim.cpp $SRC/ui_expression.cpp $K $L -o sim_anim && ./sim_anim >/dev/null
g++ $F -I$HERE/mock -I$SRC $HERE/sim_lowbat.cpp $SRC/ui_expression.cpp $K $L -o sim_lowbat && ./sim_lowbat >/dev/null
g++ $F -I$HERE/mock -I$SRC $HERE/sim_motion.cpp $SRC/ui_expression.cpp $K $L -o sim_motion && ./sim_motion 0 >/dev/null && ./sim_motion 2 >/dev/null
g++ $F -I$HERE/mock -I$SRC -DINFO_SIM $HERE/sim_info.cpp $SRC/ui_info.cpp $SRC/info_data.cpp $K $HERE/sim_time.cpp $L -o sim_info && ./sim_info >/dev/null
g++ $F -I$HERE/mock_air -I$HERE/mock -I$SRC $HERE/sim_airmenu.cpp $K $L -o sim_air && ./sim_air >/dev/null
(cd ms && g++ $F -I$HERE/mock_air -I$HERE/mock -I$SRC $HERE/sim_modeswitch.cpp $K $L -o ../sim_ms && ../sim_ms >/dev/null)
rm -rf sim_*
echo "輸出 $(find . -name '*.ppm' | wc -l | tr -d ' ') 張到 $OUT"
