#!/bin/bash
# B3c 先導測試：從 firmware/src/secrets.h 讀中控位址與 token（不印出）呼叫 /lab，報告存到 docs/lab/
# 用法：cloud/lab.sh info      看最近一段錄音與已鎖定的測試素材（不花錢）
#       cloud/lab.sh pin       把最近一段錄音鎖定成測試素材（之後裝置再錄也不會換掉）
#       cloud/lab.sh all       依序跑 A、B、C 各 2 次（約 2～3 分鐘、約 $0.1～0.2；GPT-Live 照連線時間計費）
#       cloud/lab.sh A|B|C|M [次數]       M＝停頓時 mute（C 會搶話時的對策）
#       cloud/lab.sh more      補測：M 與 A 各 2 次
cd "$(dirname "$0")/.." || exit 1
H=$(sed -nE 's/^#define HUB_HOST[[:space:]]+"([^"]+)".*/\1/p' firmware/src/secrets.h)
T=$(sed -nE 's/^#define HUB_TOKEN[[:space:]]+"([^"]+)".*/\1/p' firmware/src/secrets.h)
[ -z "$H" ] || [ -z "$T" ] && { echo "secrets.h 裡找不到 HUB_HOST 或 HUB_TOKEN"; exit 1; }
mkdir -p docs/lab
run() {
  local out="docs/lab/b3c-$1-$(date +%m%d-%H%M%S).txt"
  echo "▶ test=$1（跑完才會印出，請稍候）"
  curl -sS --max-time 900 "https://$H/lab?t=$T&test=$1&n=${2:-2}" > "$out"
  cat "$out"; echo "→ 已存 $out"
}
if [ "$1" = "all" ]; then for x in A B C; do run $x 2; done
elif [ "$1" = "more" ]; then for x in M A; do run $x 2; done; else run "${1:-info}" "$2"; fi
