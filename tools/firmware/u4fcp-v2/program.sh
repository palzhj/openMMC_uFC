#!/bin/sh
# Program a brand-new u4FCP-v2 board end to end:
#   1. jlink_program.sh — newboot + application over SWD (fw_info cleared)
#   2. fru_program.sh   — FRU with the unique serial (+ optional clock store)
#
# usage: ./program.sh <serial> [options]
#   --slot N --mch IP --bus B --bin-dir DIR --mfg-time STR --clock-store FILE
#   --no-flash  (skip the J-Link step)   --no-fru  (skip the FRU step)
# See jlink_program.sh / fru_program.sh for details.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
SERIAL=${1:-}
[ -n "$SERIAL" ] || { echo "usage: $0 <serial> [options]" >&2; exit 1; }
shift

BIN_DIR=uart-debug; SLOT=12; MCH=192.168.1.41; BUS=0
MFG_TIME=""; CLOCK_STORE=""; VADJ=1.8; DO_FLASH=1; DO_FRU=1; DO_VADJ=1
while [ $# -gt 0 ]; do
    case "$1" in
        --slot)        SLOT=$2; shift 2 ;;
        --mch)         MCH=$2; shift 2 ;;
        --bus)         BUS=$2; shift 2 ;;
        --bin-dir)     BIN_DIR=$2; shift 2 ;;
        --mfg-time)    MFG_TIME=$2; shift 2 ;;
        --clock-store) CLOCK_STORE=$2; shift 2 ;;
        --vadj)        VADJ=$2; shift 2 ;;
        --no-flash)    DO_FLASH=0; shift ;;
        --no-fru)      DO_FRU=0; shift ;;
        --no-vadj)     DO_VADJ=0; shift ;;
        -h|--help)     sed -n '2,12p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

[ "$DO_FLASH" = 1 ] && "$HERE/jlink_program.sh" --bin-dir "$BIN_DIR"
if [ "$DO_FRU" = 1 ]; then
    set -- "$SERIAL" --slot "$SLOT" --mch "$MCH" --bus "$BUS"
    [ -n "$MFG_TIME" ] && set -- "$@" --mfg-time "$MFG_TIME"
    [ -n "$CLOCK_STORE" ] && set -- "$@" --clock-store "$CLOCK_STORE"
    "$HERE/fru_program.sh" "$@"
fi
if [ "$DO_VADJ" = 1 ]; then
    "$HERE/vadj_program.sh" --fmc0 "$VADJ" --fmc1 "$VADJ" --persist --slot "$SLOT"         --mch "$MCH" --bus "$BUS" --no-wait
fi
