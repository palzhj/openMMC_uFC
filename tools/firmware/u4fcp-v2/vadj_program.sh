#!/bin/sh
# Set / read the FMC VADJ of a u4FCP (TPL0102-100PWR digital pot, I2C 0x56).
#
# Factory default is 1.8 V (DAC 0). The value is written into the TPL0102
# non-volatile register with --persist, so it survives power cycles (the MMC
# firmware does not need to set it again at every boot).
#
# usage: ./vadj_program.sh [options]
#
#   --fmc0 V            FMC0 VADJ in volts (0.9 .. 1.8)
#   --fmc1 V            FMC1 VADJ in volts (0.9 .. 1.8)
#   --persist           store in the TPL0102 EEPROM (recommended for production)
#   --read              only print the current VADJ (default when no --fmc given)
#   --slot N            AMC slot of the board (target = 0x70+2*N)   default 12
#   --mch IP            MCH / BMC address                           default 192.168.1.41
#   --bus B             IPMB-L bus of the MCH bridge                default 0
#   --no-wait           do not wait for the MMC
#
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
SLOT=12
MCH=192.168.1.41
BUS=0
WAIT=1
ARGS=""

while [ $# -gt 0 ]; do
    case "$1" in
        --slot)    SLOT=$2; shift 2 ;;
        --mch)     MCH=$2; shift 2 ;;
        --bus)     BUS=$2; shift 2 ;;
        --no-wait) WAIT=0; shift ;;
        --fmc0|--fmc1|--i2c) ARGS="$ARGS $1 $2"; shift 2 ;;
        --persist|--read) ARGS="$ARGS $1"; shift ;;
        -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

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

echo "=== u4FCP FMC VADJ"
# shellcheck disable=SC2086
python3 "$HERE/program_vadj.py" $ARGS --mch "$MCH" --slot "$SLOT" --bus "$BUS"
