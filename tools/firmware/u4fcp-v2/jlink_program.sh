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

# JLinkExe returns 0 in several failure modes (wrong device, unpowered target),
# so judge the run by the output as well.
LOG=$(mktemp /tmp/u4fcp-jlink.XXXXXX.log)
RC=0
JLinkExe -NoGui 1 -ExitOnError 1 -CommanderScript "$JLINK" > "$LOG" 2>&1 || RC=$?

if [ "$RC" -ne 0 ] || ! grep -q "Cortex-M3 identified" "$LOG" \
   || grep -qiE "(cannot connect to target|failed to connect|no target connected|could not connect|target voltage too low|error while (writing|verifying)|failed to (write|verify))" "$LOG"; then
    echo "=== ERROR: J-Link programming FAILED (no usable target)" >&2
    echo "--- relevant output ---" >&2
    grep -iE "error|cannot|no target|could not|failed|voltage|VTref|cortex|connect" "$LOG" \
        | grep -v "exit on Error" | head -15 >&2 || true
    echo "--- full log: $LOG ---" >&2
    echo "Hints:" >&2
    echo "  * board powered and the SWD header connected (VTref 3.3 V)" >&2
    echo "  * only one J-Link attached (a second one can answer instead)" >&2
    echo "  * 'Target voltage too low' = VTref not wired / board unpowered" >&2
    rm -f "$JLINK"
    exit 1
fi

tail -6 "$LOG"
rm -f "$JLINK" "$LOG"
[ "$DO_RUN" = 1 ] && echo "=== done, board reset and running"
