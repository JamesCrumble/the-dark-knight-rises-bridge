#!/bin/sh
# Copies the phone's game log to logs/phone.log
P=${TDKR_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}
A="$P/tools/adb"
D=/sdcard/BridgeGames/com.jamescrumble.tdkr
[ -n "$($A shell ls $D/tdkr.log 2>/dev/null)" ] || D=/sdcard/Android/data/com.jamescrumble.tdkr/files
$A shell cat $D/tdkr.log | tr -d '\r' > "$P/logs/phone.log"
wc -l "$P/logs/phone.log"
