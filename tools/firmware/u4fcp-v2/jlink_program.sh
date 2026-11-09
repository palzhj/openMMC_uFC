#!/bin/sh
# Flash a u4FCP-v2 board over J-Link/SWD:
#   newboot (first stage) -> 0x08000000
#   application           -> 0x08002000
#   fw_info               -> cleared (0xFF), so the bootloader starts the app
#   reset and run
#
# usage: ./jlink_program.sh [options]
#
#   --bin-dir DIR   directory with newboot.bin / openMMC.bin  (default uart-debug)
#   --no-run        do not reset/run after programming
#
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
BIN_DIR=""
DO_RUN=1

while [ $# -gt 0 ]; do
    case "$1" in
        --bin-dir) BIN_DIR=$2; shift 2 ;;
        --no-run)  DO_RUN=0; shift ;;
        -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

command -v JLinkExe >/dev/null || { echo "JLinkExe not found" >&2; exit 1; }

# Default: the packaged images next to this script, else the local build tree
if [ -z "$BIN_DIR" ]; then
    for cand in "$HERE/uart-debug" "$HERE/normal" "$REPO/.hw2/u4fcp/out"; do
        if [ -f "$cand/newboot.bin" ] && [ -f "$cand/openMMC.bin" ]; then
            BIN_DIR=$cand
            break
        fi
    done
fi
[ -n "$BIN_DIR" ] || {
    echo "newboot.bin/openMMC.bin not found." >&2
    echo "Build them first:" >&2
    echo "  cmake -S '$REPO' -B '$REPO/.hw2/u4fcp' -DBOARD=u4fcp-v2 \\" >&2
    echo "        -DTARGET_CONTROLLER=STM32F103CBT6 -DCMAKE_BUILD_TYPE=Release" >&2
    echo "  make -C '$REPO/.hw2/u4fcp' openMMC -j" >&2
    echo "or pass --bin-dir DIR" >&2
    exit 1
}

case "$BIN_DIR" in
    /*) ROOT=$BIN_DIR ;;
    *)  ROOT="$HERE/$BIN_DIR" ;;
esac
BOOT="$ROOT/newboot.bin"
APP="$ROOT/openMMC.bin"
[ -f "$BOOT" ] || { echo "missing $BOOT" >&2; exit 1; }
[ -f "$APP" ]  || { echo "missing $APP" >&2; exit 1; }

JLINK=$(mktemp /tmp/u4fcp-jlink.XXXXXX.jlink)
cat > "$JLINK" <<SCRIPT
device STM32F103CB
si SWD
speed 1000
VTREF 3300
connect
loadfile $BOOT 0x08000000
loadfile $APP 0x08002000
w4 0x0801FC00 0xFFFFFFFF
w4 0x0801FC04 0xFFFFFFFF
$( [ "$DO_RUN" = 1 ] && echo "r
g" )
q
SCRIPT

echo "=== u4FCP-v2 J-Link programming"
echo "    images: $ROOT"
echo "    newboot.bin -> 0x08000000, openMMC.bin -> 0x08002000, fw_info cleared"
JLinkExe -NoGui 1 -ExitOnError 1 -CommanderScript "$JLINK" | tail -4
rm -f "$JLINK"
[ "$DO_RUN" = 1 ] && echo "=== done, board reset and running"
