#!/bin/sh
# syms.sh <substr>... : game library symbols containing any substring (name, value, size), TinyXML filtered out.
P=${TDKR_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}
python3 -I $P/tools/elfsym.py $P/tdkrdata/lib/armeabi-v7a/libTDKR_neon.so "$@" \
  | grep -v TiXml | awk '{print $NF, $2, $3}' | sort -u | head -${N:-80}
