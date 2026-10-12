#!/bin/bash
# Runs the port on this PC under the system qemu-arm with software GL (llvmpipe), no window.
# Usage: tools/run_pc.sh [frames]      (default 600; log in logs/pc_run.log)
# A screenshot while it runs:  echo "screenshot NAME" >> ctl/commands   -> ctl/screenshots/NAME.png
P=${TDKR_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}
mkdir -p "$P/ctl" "$P/logs"
: > "$P/ctl/commands"
cd "$P/port" || exit 1
LOADER_TRACE=1 TDKR_CONTROL_DIR="$P/ctl" SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless \
SDL_AUDIODRIVER=dummy TDKR_WIDTH=${TDKR_WIDTH:-960} TDKR_HEIGHT=${TDKR_HEIGHT:-540} TDKR_AUTOPILOT=${TDKR_AUTOPILOT:-0} \
TDKR_FRAME_LIMIT="${1:-600}" \
    qemu-arm -L android/build/sysroot build/tdkr "$P/tdkrdata" > "$P/logs/pc_run.log" 2>&1
echo "exit code $?"
grep -E "summary|run finished|FATAL|unresolved" "$P/logs/pc_run.log"
