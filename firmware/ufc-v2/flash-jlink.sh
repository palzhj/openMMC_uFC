#!/bin/sh
# Program newboot + application over J-Link/SWD, including the OpenBLT
# vector-table checksum fix-up for newboot.
#
# usage: ./flash-jlink.sh [image-dir]      (default: uart-debug)
set -eu
DIR="${1:-uart-debug}"
HERE=$(cd "$(dirname "$0")" && pwd)
BIN="$HERE/$DIR/newboot.bin"
APP="$HERE/$DIR/openMMC.bin"

[ -f "$BIN" ] || { echo "missing $BIN"; exit 1; }
[ -f "$APP" ] || { echo "missing $APP"; exit 1; }

CHK=$(python3 - "$BIN" <<'PY'
import struct, sys
b = open(sys.argv[1], 'rb').read()
s = sum(struct.unpack_from('<I', b, o)[0] for o in (0, 4, 8, 0xC, 0x10, 0x14, 0x18))
print('0x%08X' % ((-s) & 0xFFFFFFFF))
PY
)
echo "newboot OpenBLT checksum word: $CHK"

TMP=$(mktemp /tmp/ufc-flash.XXXXXX.jlink)
cat > "$TMP" <<SCRIPT
device STM32F100CB
si SWD
speed 1000
VTREF 3300
connect
loadfile $BIN 0x08002000
loadfile $APP 0x08004000
w4 0x080021D0 $CHK
r
g
q
SCRIPT

echo "flashing $DIR/newboot.bin + $DIR/openMMC.bin via J-Link"
JLinkExe -NoGui 1 -ExitOnError 1 -CommanderScript "$TMP"
rm -f "$TMP"
