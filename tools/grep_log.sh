#!/bin/sh
# grep -E "$1" over the phone's game log (last $2 lines, default 40).
P=${TDKR_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}
A=$P/tools/adb
D=/sdcard/BridgeGames/com.jamescrumble.tdkr
[ -n "$($A shell ls $D/tdkr.log 2>/dev/null)" ] || D=/sdcard/Android/data/com.jamescrumble.tdkr/files
F=$D/tdkr.log
$A shell "grep -aE '$1' $F | tail -${2:-40}"
