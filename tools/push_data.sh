#!/bin/sh
# Copies the player's game files to the phone: the game's APK from gamebin/ and the unpacked cache
# from tdkrdata/files, then opens the folders adb created to the app (they belong to the shell user).
# About 1.9 GB. Usage: tools/push_data.sh
P=${TDKR_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}
A=$P/tools/adb
D=/sdcard/Android/data/com.jamescrumble.tdkr/files
G=com.gameloft.android.AMAZ.GloftKRAS
APK=$(ls "$P"/gamebin/*.apk 2>/dev/null | head -1)
[ -n "$APK" ] || { echo "no game APK in gamebin/"; exit 1; }
[ -f "$P/tdkrdata/files/data/game_config.gla" ] || { echo "no unpacked cache in tdkrdata/files"; exit 1; }
$A shell mkdir -p "$D/$G" || exit 1
$A push "$(wslpath -w "$APK")" "$D/tdkr-1.1.6.apk" || exit 1
$A push "$(wslpath -w "$P/tdkrdata/files")" "$D/$G/" || exit 1
$A shell "find $D/$G -type d -exec chmod 2777 {} +"
$A shell "du -sh $D"
