#!/bin/sh
# Program newboot (first stage) + application over J-Link/SWD.
# newboot is linked at 0x08000000.
#
# usage: ./flash-jlink.sh [image-dir]      (default: uart-debug)
set -eu
DIR="${1:-uart-debug}"
HERE=$(cd "$(dirname "$0")" && pwd)
BIN="$HERE/$DIR/newboot.bin"
APP="$HERE/$DIR/openMMC.bin"

[ -f "$BIN" ] || { echo "missing $BIN"; exit 1; }
[ -f "$APP" ] || { echo "missing $APP"; exit 1; }

TMP=$(mktemp /tmp/ufc-flash.XXXXXX.jlink)
cat > "$TMP" <<SCRIPT
device STM32F100CB
si SWD
speed 1000
VTREF 3300
connect
loadfile $BIN 0x08000000
loadfile $APP 0x08002000
r
g
q
SCRIPT

echo "flashing $DIR/newboot.bin (0x08000000) + $DIR/openMMC.bin (0x08002000) via J-Link"
JLinkExe -NoGui 1 -ExitOnError 1 -CommanderScript "$TMP"
rm -f "$TMP"
