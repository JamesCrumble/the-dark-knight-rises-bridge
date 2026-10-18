#!/bin/sh
# set_gpu.sh "<renderer name>" | set_gpu.sh ""  -> writes/removes gpu_name.txt next to the game log.
P=${TDKR_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}
A=$P/tools/adb
D=/sdcard/BridgeGames/com.jamescrumble.tdkr
$A shell "[ -d $D ]" >/dev/null 2>&1 || D=/sdcard/Android/data/com.jamescrumble.tdkr/files
if [ -n "$1" ]; then
  printf '%s' "$1" > /tmp/gpu_name.txt
  $A push /tmp/gpu_name.txt "$D/gpu_name.txt" >/dev/null && echo "gpu_name.txt = $1"
else
  $A shell rm -f "$D/gpu_name.txt" && echo "gpu_name.txt removed"
fi
