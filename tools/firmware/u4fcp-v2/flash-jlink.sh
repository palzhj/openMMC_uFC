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

TMP=$(mktemp /tmp/u4fcp-v2-flash.XXXXXX.jlink)
cat > "$TMP" <<SCRIPT
device STM32F103CB
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
# JLinkExe returns 0 in several failure modes; verify the connection in the log.
LOG=$(mktemp /tmp/u4fcp-v2-flash.XXXXXX.log)
RC=0
JLinkExe -NoGui 1 -ExitOnError 1 -CommanderScript "$TMP" > "$LOG" 2>&1 || RC=$?
if [ "$RC" -ne 0 ] || ! grep -q "Cortex-M3 identified" "$LOG" \
   || grep -qiE "(cannot connect to target|failed to connect|no target connected|could not connect|target voltage too low|error while (writing|verifying)|failed to (write|verify))" "$LOG"; then
    echo "ERROR: J-Link flashing FAILED (no usable target)" >&2
    grep -iE "error|cannot|no target|could not|failed|voltage|VTref|cortex|connect" "$LOG" \
        | grep -v "exit on Error" | head -15 >&2 || true
    echo "full log: $LOG" >&2
    rm -f "$TMP"
    exit 1
fi
tail -3 "$LOG"
rm -f "$TMP" "$LOG"
