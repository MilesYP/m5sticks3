#!/bin/bash
# 上傳 Friday 的角色說明與使用者記憶到中控（不用重新部署）。
# 用法：cloud/persona.sh        上傳 persona/friday.md 與 persona/miles.local.md（沒有就略過）
#       cloud/persona.sh show   看中控目前存的長度
cd "$(dirname "$0")/.." || exit 1
H=$(sed -nE 's/^#define HUB_HOST[[:space:]]+"([^"]+)".*/\1/p' firmware/src/secrets.h)
T=$(sed -nE 's/^#define HUB_TOKEN[[:space:]]+"([^"]+)".*/\1/p' firmware/src/secrets.h)
[ -z "$H" ] || [ -z "$T" ] && { echo "secrets.h 裡找不到 HUB_HOST 或 HUB_TOKEN"; exit 1; }
if [ "$1" = "show" ]; then curl -sS "https://$H/persona?t=$T"; exit; fi
for n in friday miles; do
  f=cloud/persona/$n.md; [ $n = miles ] && f=cloud/persona/miles.local.md
  if [ -f "$f" ]; then printf '%s → ' "$f"; curl -sS -X PUT --data-binary @"$f" -H 'content-type: text/markdown; charset=utf-8' "https://$H/persona?t=$T&name=$n"; else echo "略過 $f（不存在）"; fi
done
