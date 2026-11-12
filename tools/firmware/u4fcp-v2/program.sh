#!/bin/sh
# Program a u4FCP-v2 board end to end, always with the LATEST firmware of this
# tree:
#
#   1. build newboot + application from the current commit and flash them over
#      SWD (jlink_program.sh, fw_info cleared)
#   2. write the FRU EEPROM with the given serial number (manufacturing time
#      "now" unless told otherwise)
#   3. write the clock preset (ADN4604 + Si5345 setup, clk-preset.bin) into the
#      MMC EEPROM and apply it
#   4. set FMC0/FMC1 VADJ to 1.8 V and persist it in the TPL0102 EEPROM
#   5. with --rtm: write the uRTM's own FRU EEPROM (0x55 behind 0x70 ch5 ->
#      0x71 ch7) using the same serial number
#
# The serial number is the only argument you have to give.
#
# usage: ./program.sh <serial> [options]
#
#   --slot N            AMC slot of the board (target = 0x70+2*N)  default 12
#   --mch IP            MCH / BMC address                          default 192.168.1.41
#   --bus B             IPMB-L bus of the MCH bridge               default 0
#   --template FILE     FRU template image        default fru_template.bin (here)
#   --mfg-time STR      manufacturing time in the FRU              default now
#   --clk-preset FILE   clock preset image       default clk-preset.bin (here)
#   --clock-store FILE  restore a clock store (program_clock.py --dump)
#   --rtm               also program the uRTM FRU, same serial as the AMC
#   --rtm-template FILE uRTM FRU template      default fru_template_rtm.bin
#   --bin-dir DIR       flash these images instead of building
#   --no-build          do not build, reuse the existing build tree
#   --dry-run           prepare the FRU image only (no flash, no write)
#   --no-flash / --no-fru / --no-clk / --no-vadj   skip a step
#
# Examples:
#   ./program.sh U4FCP2-0001                       # build + flash + FRU + clock + VADJ
#   ./program.sh U4FCP2-0001 --slot 3 --mch 10.0.0.5
#   ./program.sh U4FCP2-0001 --no-flash            # FRU/VADJ only (board in a crate)
#   ./program.sh U4FCP2-0001 --rtm                 # + the uRTM's FRU (same SN)
#   ./program.sh U4FCP2-0001 --dry-run             # just build the FRU image
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
BUILD_DIR=${BUILD_DIR:-$REPO/.hw2/u4fcp}

SERIAL=${1:-}
[ -n "$SERIAL" ] || { echo "usage: $0 <serial> [options]" >&2; exit 1; }
shift

SLOT=12
MCH=192.168.1.41
BUS=0
TEMPLATE="$HERE/fru_template.bin"
RTM_TEMPLATE="$HERE/fru_template_rtm.bin"
CLK_PRESET="$HERE/clk-preset.bin"
MFG_TIME=""
CLOCK_STORE=""
VADJ=1.8
BIN_DIR=""
DO_BUILD=1
DO_FLASH=1
DO_FRU=1
DO_CLK=1
DO_RTM=0
DO_VADJ=1
DRY_RUN=0

while [ $# -gt 0 ]; do
    case "$1" in
        --slot)        SLOT=$2; shift 2 ;;
        --mch)         MCH=$2; shift 2 ;;
        --bus)         BUS=$2; shift 2 ;;
        --template)    TEMPLATE=$2; shift 2 ;;
        --clk-preset)  CLK_PRESET=$2; shift 2 ;;
        --rtm)         DO_RTM=1; shift ;;
        --rtm-template) RTM_TEMPLATE=$2; DO_RTM=1; shift 2 ;;
        --no-rtm)      DO_RTM=0; shift ;;
        --mfg-time)    MFG_TIME=$2; shift 2 ;;
        --clock-store) CLOCK_STORE=$2; shift 2 ;;
        --vadj)        VADJ=$2; shift 2 ;;
        --bin-dir)     BIN_DIR=$2; DO_BUILD=0; shift 2 ;;
        --no-build)    DO_BUILD=0; shift ;;
        --dry-run)     DRY_RUN=1; DO_FLASH=0; DO_CLK=0; DO_RTM=0; DO_VADJ=0; shift ;;
        --no-flash)    DO_FLASH=0; shift ;;
        --no-fru)      DO_FRU=0; shift ;;
        --no-clk)      DO_CLK=0; shift ;;
        --no-vadj)     DO_VADJ=0; shift ;;
        -h|--help)     sed -n '2,40p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

if [ "$DO_BUILD" = 1 ]; then
    REV=$(git -C "$REPO" describe --tags --always 2>/dev/null || echo "unknown revision")
    echo "=== building the latest firmware ($REV)"
    cmake -S "$REPO" -B "$BUILD_DIR" -DBOARD=u4fcp-v2 \
          -DTARGET_CONTROLLER=STM32F103CBT6 -DCMAKE_BUILD_TYPE=Release >/dev/null
    JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
    make -C "$BUILD_DIR" newboot openMMC -j"$JOBS"
fi

OUT=${BIN_DIR:-$BUILD_DIR/out}
FW_VERSION=$(strings "$OUT/openMMC.bin" 2>/dev/null | grep -m1 '^v[0-9]' || true)
echo "=== u4FCP-v2 programming: serial $SERIAL, AMC$SLOT, MCH $MCH"
echo "    images       : $OUT"
[ -n "$FW_VERSION" ] && echo "    firmware     : $FW_VERSION"
[ "$DO_FRU" = 1 ] && echo "    FRU template : $TEMPLATE"
[ "$DO_CLK" = 1 ] && echo "    clock preset : $CLK_PRESET"
[ "$DO_RTM" = 1 ] && echo "    RTM FRU      : $RTM_TEMPLATE (serial $SERIAL)"

if [ "$DO_FLASH" = 1 ]; then
    echo "--- flashing (newboot + application over SWD)"
    "$HERE/jlink_program.sh" --bin-dir "$OUT"
fi

if [ "$DO_FRU" = 1 ]; then
    echo "--- writing the FRU (serial $SERIAL)"
    set -- "$SERIAL" --slot "$SLOT" --mch "$MCH" --bus "$BUS" --template "$TEMPLATE"
    [ -n "$MFG_TIME" ] && set -- "$@" --mfg-time "$MFG_TIME"
    [ -n "$CLOCK_STORE" ] && set -- "$@" --clock-store "$CLOCK_STORE"
    [ "$DRY_RUN" = 1 ] && set -- "$@" --dry-run
    "$HERE/fru_program.sh" "$@"
fi

if [ "$DO_CLK" = 1 ]; then
    echo "--- clock preset ($CLK_PRESET)"
    "$HERE/clk_program.sh" "$CLK_PRESET" --apply --slot "$SLOT" --mch "$MCH" --bus "$BUS"
fi

if [ "$DO_RTM" = 1 ]; then
    echo "--- writing the RTM FRU (serial $SERIAL, same as the AMC)"
    python3 "$HERE/rtm_fru.py" --write "$RTM_TEMPLATE" --serial "$SERIAL" \
        --mch "$MCH" --target "$(printf '0x%02x' $((0x70 + 2 * SLOT)))" --bus "$BUS"
fi

if [ "$DO_VADJ" = 1 ]; then
    echo "--- FMC VADJ $VADJ V (persisted)"
    "$HERE/vadj_program.sh" --fmc0 "$VADJ" --fmc1 "$VADJ" --persist \
        --slot "$SLOT" --mch "$MCH" --bus "$BUS"
fi

if [ "$DRY_RUN" = 1 ]; then
    echo "=== dry run: FRU image for $SERIAL prepared, nothing written"
else
    echo "=== done: $SERIAL programmed (AMC$SLOT)${FW_VERSION:+ with firmware $FW_VERSION}"
fi
