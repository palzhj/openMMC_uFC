#!/bin/sh
# Write (or capture) the clock configuration preset of a u4FCP-v2 MMC.
#
# The MMC keeps the clocking setup (ADN4604 switch matrix + Si5345 PLL
# registers) in its CAT24C512 EEPROM: pointer 0x0200, slot A 0x0300.
# A preset is a small image captured from a programmed board, so it can be
# replayed on every new board (clocks are programmed at boot, after the main
# power is up).
#
# usage:
#   ./clk_program.sh [preset.bin] [options]      write a preset to the EEPROM
#   ./clk_program.sh --dump <preset.bin> [options]  capture the current setup
#
# With no file, the preset next to this script (clk-preset.bin) is used.
#
#   --apply             apply the configuration immediately (0x32 0x19)
#   --slot N            AMC slot of the board (target = 0x70+2*N)   default 12
#   --mch IP            MCH / BMC address                           default 192.168.1.41
#   --bus B             IPMB-L bus of the MCH bridge                default 0
#   --no-wait           do not wait for the MMC (assume it answers)
#
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
FILE=""
DUMP=0
APPLY=0
SLOT=12
MCH=192.168.1.41
BUS=0
WAIT=1

while [ $# -gt 0 ]; do
    case "$1" in
        --dump)    DUMP=1; FILE=$2; shift 2 ;;
        --apply)   APPLY=1; shift ;;
        --slot)    SLOT=$2; shift 2 ;;
        --mch)     MCH=$2; shift 2 ;;
        --bus)     BUS=$2; shift 2 ;;
        --no-wait) WAIT=0; shift ;;
        -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
        -*)        echo "unknown option: $1" >&2; exit 1 ;;
        *)         FILE=$1; shift ;;
    esac
done
if [ -z "$FILE" ] && [ "$DUMP" = 0 ]; then
    FILE="$HERE/clk-preset.bin"
fi
[ -n "$FILE" ] || { echo "usage: $0 [preset.bin] | --dump <preset.bin> [options]" >&2; exit 1; }

TARGET=$(printf '0x%02x' $((0x70 + 2 * SLOT)))
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
fi

if [ "$DUMP" = 1 ]; then
    echo "=== capturing the clock configuration to $FILE"
    python3 "$HERE/program_clock.py" --dump "$FILE" \
        --mch "$MCH" --slot "$SLOT" --bus "$BUS"
else
    [ -f "$FILE" ] || { echo "missing $FILE" >&2; exit 1; }
    echo "=== writing the clock configuration from $FILE"
    set -- --upload "$FILE" --mch "$MCH" --slot "$SLOT" --bus "$BUS"
    [ "$APPLY" = 1 ] && set -- "$@" --apply
    python3 "$HERE/program_clock.py" "$@"
fi
