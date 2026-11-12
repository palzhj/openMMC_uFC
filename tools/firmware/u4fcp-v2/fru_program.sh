#!/bin/sh
# Program the FRU (unique serial number) of a u4FCP-v2 MMC and optionally
# restore its clock configuration store.
#
# The board must be reachable through the MCH bridge: either already running
# in a crate, or flashed with jlink_program.sh and inserted.
#
# usage: ./fru_program.sh [options] --sn <serial>
#
#   --sn SERIAL         serial number of the board, use it last
#   --slot N            AMC slot of the board (target = 0x70+2*N)   default 12
#   --mch IP            MCH / BMC address                           default 192.168.1.41
#   --bus B             IPMB-L bus of the MCH bridge                default 0
#   --template FILE     FRU template image (default fru_template.bin)
#   --mfg-time STR      manufacturing time (default: now)
#   --clock-store FILE  clock store image captured with program_clock.py --dump
#   --dry-run           build the FRU image and print it, do not write it
#   --no-wait           do not wait for the MMC (assume it answers)
#
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
SERIAL=""

SLOT=12
MCH=192.168.1.41
BUS=0
MFG_TIME=""
CLOCK_STORE=""
TEMPLATE=""
DRY_RUN=0
WAIT=1

while [ $# -gt 0 ]; do
    case "$1" in
        --sn)          SERIAL=$2; shift 2 ;;
        --slot)        SLOT=$2; shift 2 ;;
        --mch)         MCH=$2; shift 2 ;;
        --bus)         BUS=$2; shift 2 ;;
        --template)    TEMPLATE=$2; shift 2 ;;
        --mfg-time)    MFG_TIME=$2; shift 2 ;;
        --clock-store) CLOCK_STORE=$2; shift 2 ;;
        --dry-run)     DRY_RUN=1; shift ;;
        --no-wait)     WAIT=0; shift ;;
        -h|--help)     sed -n '2,24p' "$0"; exit 0 ;;
        -*)            echo "unknown option: $1" >&2; exit 1 ;;
        *)             [ -z "$SERIAL" ] || { echo "serial given twice: $1" >&2; exit 1; }
                       SERIAL=$1; shift ;;
    esac
done

[ -n "$SERIAL" ] || { echo "missing --sn <serial>" >&2
                      echo "usage: $0 [options] --sn <serial>" >&2; exit 1; }

TARGET=$(printf '0x%02x' $((0x70 + 2 * SLOT)))
echo "=== u4FCP-v2 FRU programming: serial $SERIAL, AMC$SLOT (IPMB $TARGET), MCH $MCH"

if [ "$WAIT" = 1 ]; then
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
fi

echo "--- writing the FRU"
set -- "$SERIAL" --mch "$MCH" --slot "$SLOT" --bus "$BUS"
[ -n "$TEMPLATE" ] && set -- "$@" --template "$TEMPLATE"
[ "$DRY_RUN" = 1 ] && set -- "$@" --dry-run
[ -n "$MFG_TIME" ] && set -- "$@" --mfg-time "$MFG_TIME"
python3 "$HERE/program_fru.py" "$@"

if [ -n "$CLOCK_STORE" ]; then
    echo "--- restoring the clock store from $CLOCK_STORE"
    python3 "$HERE/program_clock.py" --upload "$CLOCK_STORE" \
        --mch "$MCH" --slot "$SLOT" --bus "$BUS"
fi

echo "=== done: $SERIAL programmed (AMC$SLOT, IPMB $TARGET)"
