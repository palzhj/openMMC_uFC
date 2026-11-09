#!/bin/sh
# Program a brand-new u4FCP-v2 board end to end.
#
#   1. newboot (0x08000000) + application (0x08002000) over J-Link/SWD
#   2. clear the firmware-update record (fw_info) so the bootloader starts the app
#   3. write the FRU with a unique serial number through the MMC's IPMI EEPROM
#      interface (board + product serials, manufacturing time = now, verified)
#   4. optionally restore the clock configuration store captured from a
#      programmed board (--clock-store FILE)
#
# usage: ./program.sh <serial> [options]
#
#   --slot N            AMC slot of the board (target = 0x70+2*N)   default 12
#   --mch IP            MCH / BMC address                           default 192.168.1.41
#   --bus B             IPMB-L bus of the MCH bridge                default 0
#   --bin-dir DIR       directory with newboot.bin/openMMC.bin      default uart-debug
#   --mfg-time STR      manufacturing time, e.g. "2026-11-10 01:30" (default: now)
#   --clock-store FILE  clock store image captured with program_clock.py --dump
#   --no-flash          skip the J-Link step (board already flashed)
#   --no-fru            skip the FRU step
#
# examples:
#   ./program.sh U4FCP2-0001
#   ./program.sh U4FCP2-0001 --slot 7 --mch 192.168.1.41
#   ./program.sh U4FCP2-0001 --no-flash --clock-store clock-slot.bin
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
SERIAL=${1:-}
[ -n "$SERIAL" ] || { echo "usage: $0 <serial> [options]" >&2; exit 1; }
shift

SLOT=12
MCH=192.168.1.41
BUS=0
BIN_DIR=uart-debug
MFG_TIME=""
CLOCK_STORE=""
DO_FLASH=1
DO_FRU=1

while [ $# -gt 0 ]; do
    case "$1" in
        --slot)        SLOT=$2; shift 2 ;;
        --mch)         MCH=$2; shift 2 ;;
        --bus)         BUS=$2; shift 2 ;;
        --bin-dir)     BIN_DIR=$2; shift 2 ;;
        --mfg-time)    MFG_TIME=$2; shift 2 ;;
        --clock-store) CLOCK_STORE=$2; shift 2 ;;
        --no-flash)    DO_FLASH=0; shift ;;
        --no-fru)      DO_FRU=0; shift ;;
        -h|--help)     sed -n '2,30p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

TARGET=$(printf '0x%02x' $((0x70 + 2 * SLOT)))
echo "=== u4FCP-v2 programming: serial $SERIAL, AMC$SLOT (IPMB $TARGET), MCH $MCH"
[ -n "$MFG_TIME" ] && echo "    manufacturing time: $MFG_TIME"

# ------------------------------------------------------------------ 1) flash
if [ "$DO_FLASH" = 1 ]; then
    command -v JLinkExe >/dev/null || { echo "JLinkExe not found" >&2; exit 1; }
    BOOT="$HERE/$BIN_DIR/newboot.bin"
    APP="$HERE/$BIN_DIR/openMMC.bin"
    [ -f "$BOOT" ] || { echo "missing $BOOT" >&2; exit 1; }
    [ -f "$APP" ]  || { echo "missing $APP" >&2; exit 1; }

    JLINK=$(mktemp /tmp/u4fcp-program.XXXXXX.jlink)
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
r
g
q
SCRIPT
    echo "--- flashing $BIN_DIR/newboot.bin + $BIN_DIR/openMMC.bin (J-Link)"
    JLinkExe -NoGui 1 -ExitOnError 1 -CommanderScript "$JLINK" | tail -4
    rm -f "$JLINK"
else
    echo "--- skipping the J-Link step"
fi

# ------------------------------------------------------- 2) wait for the MMC
echo "--- waiting for the MMC at $TARGET"
i=0
while [ $i -lt 30 ]; do
    if ipmitool -I lan -H "$MCH" -A none -T 0x82 -B 0 -t "$TARGET" -b "$BUS" \
           raw 0x06 0x01 >/dev/null 2>&1; then
        break
    fi
    i=$((i + 1))
    sleep 2
done
[ $i -lt 30 ] || { echo "MMC did not answer at $TARGET" >&2; exit 1; }
echo "    MMC answered"

# ---------------------------------------------------------------- 3) FRU
if [ "$DO_FRU" = 1 ]; then
    echo "--- writing the FRU"
    set -- "$SERIAL" --mch "$MCH" --slot "$SLOT" --bus "$BUS"
    [ -n "$MFG_TIME" ] && set -- "$@" --mfg-time "$MFG_TIME"
    python3 "$HERE/program_fru.py" "$@"
fi

# ------------------------------------------------------- 4) clock store
if [ -n "$CLOCK_STORE" ]; then
    echo "--- restoring the clock store from $CLOCK_STORE"
    python3 "$HERE/program_clock.py" --upload "$CLOCK_STORE" \
        --mch "$MCH" --slot "$SLOT" --bus "$BUS"
fi

echo "=== done: $SERIAL programmed (AMC$SLOT, IPMB $TARGET)"
