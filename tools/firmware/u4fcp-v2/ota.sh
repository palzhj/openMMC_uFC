#!/bin/sh
# OTA (HPM.1) update of a u4FCP-v2 MMC over IPMI through the MCH.
#
# The application image is uploaded into the staging area (0x08010E00,
# 60928 B); `activate` makes newboot copy it over 0x08002000 and reboot.  The
# upload has no read-back verification, so this script checks the image before
# it starts and the running SHA1 afterwards.
#
# usage: ./ota.sh [options] [image]
#
# With no image, the tree is built (unless --no-build) and
# $BUILD_DIR/out/openMMC.bin is used.
#
#   --mch IP        MCH / BMC address                      default 192.168.1.41
#   --slot N        AMC slot of the board (target 0x70+2N)  default 12
#   --bus B         IPMB-L bus through the MCH              default 0
#   --transit 0x..  transit (MCH) address                   default 0x82
#   --component N   HPM.1 component (1 = MMC, 0 = newboot)  default 1
#   --bin-dir DIR   take openMMC.bin from DIR instead of the build tree
#   --no-build      reuse the existing build tree
#   --no-activate   upload and finish only (the module keeps the old image)
#   --no-verify     skip the post-update checks
#   --check         pre-flight only, do not upload
#   --timeout S     seconds to wait for the MMC after activation  default 60
#
# Examples:
#   ./ota.sh --slot 6                       # build, upload, activate, verify
#   ./ota.sh --slot 6 --no-build            # reuse .hw2/u4fcp/out
#   ./ota.sh --slot 6 --check               # image + target sanity check
#   ./ota.sh --slot 6 --no-activate         # stage only
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
BUILD_DIR=${BUILD_DIR:-$REPO/.hw2/u4fcp}
UPLOADER=$REPO/tools/uFC_toolbox/hpm_upload.py

MCH=192.168.1.41
SLOT=12
BUS=0
TRANSIT=0x82
COMPONENT=1
BIN_DIR=""
DO_BUILD=1
DO_ACTIVATE=1
DO_VERIFY=1
CHECK_ONLY=0
TIMEOUT=60
IMAGE=""

while [ $# -gt 0 ]; do
    case "$1" in
        --mch)         MCH=$2; shift 2 ;;
        --slot)        SLOT=$2; shift 2 ;;
        --bus)         BUS=$2; shift 2 ;;
        --transit)     TRANSIT=$2; shift 2 ;;
        --component)   COMPONENT=$2; shift 2 ;;
        --bin-dir)     BIN_DIR=$2; DO_BUILD=0; shift 2 ;;
        --no-build)    DO_BUILD=0; shift ;;
        --no-activate) DO_ACTIVATE=0; shift ;;
        --no-verify)   DO_VERIFY=0; shift ;;
        --check)       CHECK_ONLY=1; shift ;;
        --timeout)     TIMEOUT=$2; shift 2 ;;
        -h|--help)     sed -n '2,30p' "$0"; exit 0 ;;
        -*)            echo "unknown option: $1" >&2; exit 1 ;;
        *)             IMAGE=$1; shift ;;
    esac
done

TARGET=$(printf '0x%02x' $((0x70 + 2 * SLOT)))
B="-I lan -H $MCH -A none -T $TRANSIT -B 0 -t $TARGET -b $BUS"

say() { printf '%s\n' "$*"; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

# ipmitool through the MCH, with retries: right after a long HPM session (3000
# block commands) the MCH can refuse a new LAN session for a few seconds.
mch_raw() {
    n=0
    while [ "$n" -lt 5 ]; do
        if OUT=$(ipmitool $B raw "$@" 2>&1); then
            printf '%s' "$OUT"
            return 0
        fi
        n=$((n + 1))
        sleep 2
    done
    printf '%s' "$OUT" >&2
    return 1
}

command -v ipmitool >/dev/null || die "ipmitool not found (needed for the IPMB bridge)"
[ -f "$UPLOADER" ] || die "missing $UPLOADER"

# ---------------------------------------------------------------- image ----

if [ -z "$IMAGE" ]; then
    [ -n "$BIN_DIR" ] || BIN_DIR=$BUILD_DIR/out
    IMAGE=$BIN_DIR/openMMC.bin
    if [ "$DO_BUILD" = 1 ]; then
        command -v cmake >/dev/null || die "cmake not found"
        command -v arm-none-eabi-gcc >/dev/null || die "arm-none-eabi-gcc not found"
        say "=== building the application"
        if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
            cmake -S "$REPO" -B "$BUILD_DIR" -DBOARD=u4fcp-v2 \
                  -DTARGET_CONTROLLER=STM32F103CBT6 -DCMAKE_BUILD_TYPE=Release \
                  >/dev/null 2>&1 || die "cmake configure failed (see $BUILD_DIR)"
        fi
        make -C "$BUILD_DIR" openMMC -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" \
            >/dev/null 2>&1 || die "build failed (make -C $BUILD_DIR openMMC)"
    fi
fi

[ -f "$IMAGE" ] || die "image not found: $IMAGE"
SIZE=$(wc -c < "$IMAGE" | tr -d ' ')
STAGE_MAX=60928
[ "$SIZE" -le "$STAGE_MAX" ] || die "image is $SIZE B, the staging area holds $STAGE_MAX B"

# the SHA the image will report as raw 0x32 0x02 (empty for images built
# without git metadata, e.g. from a plain `git archive` tree)
IMG_SHA=$(strings "$IMAGE" | grep -m1 -E '^[0-9a-f]{40}$' || true)
HEAD_SHA=$(git -C "$REPO" rev-parse HEAD 2>/dev/null || true)
if [ -n "$IMG_SHA" ] && [ -n "$HEAD_SHA" ] && [ "$IMG_SHA" != "$HEAD_SHA" ]; then
    say "WARNING: image SHA $IMG_SHA does not match HEAD $HEAD_SHA"
elif [ -z "$IMG_SHA" ]; then
    say "WARNING: image carries no git SHA (built outside a git checkout?)"
fi
if [ -n "$(git -C "$REPO" status --porcelain 2>/dev/null)" ]; then
    say "WARNING: the tree has uncommitted changes"
fi

say "=== OTA plan"
say "    image      $(basename "$IMAGE") ($SIZE B, sha ${IMG_SHA:-none})"
say "    MCH        $MCH   target $TARGET (slot $SLOT)   transit $TRANSIT bus $BUS"
say "    component  $COMPONENT (1 = MMC)   activate=$DO_ACTIVATE verify=$DO_VERIFY"
say "    expected   ~5 min for a 60 KB image (20-byte HPM blocks)"

# ------------------------------------------------------------- pre-flight --

say "=== checking that the module answers"
mch_raw 0x06 0x01 >/dev/null \
    || die "the MMC does not answer at $TARGET (Get Device ID failed)"
BEFORE=$(mch_raw 0x32 0x02 | tr -d ' \n')
[ -n "$BEFORE" ] || die "the MMC does not answer the git SHA1 command"
say "    running    ${BEFORE}"

if [ "$CHECK_ONLY" = 1 ]; then
    say "=== pre-flight only (--check), nothing uploaded"
    exit 0
fi

# ---------------------------------------------------------------- upload ---

say "=== uploading"
if [ "$DO_ACTIVATE" = 1 ]; then
    python3 "$UPLOADER" "$IMAGE" --mch "$MCH" --target "$TARGET" \
            --transit "$TRANSIT" --bus "$BUS" --component "$COMPONENT" \
        || die "upload failed (the old image keeps running)"
else
    python3 "$UPLOADER" "$IMAGE" --mch "$MCH" --target "$TARGET" \
            --transit "$TRANSIT" --bus "$BUS" --component "$COMPONENT" --no-activate \
        || die "upload failed (the old image keeps running)"
    say "=== staged only (--no-activate); the module still runs $BEFORE"
    exit 0
fi

[ "$DO_VERIFY" = 1 ] || exit 0

# ---------------------------------------------------------------- verify ---

say "=== waiting for the MMC to come back"
i=0
AFTER=""
while [ "$i" -lt "$TIMEOUT" ]; do
    sleep 2
    i=$((i + 2))
    AFTER=$(mch_raw 0x32 0x02 2>/dev/null | tr -d ' \n' || true)
    [ -n "$AFTER" ] && break
done
[ -n "$AFTER" ] || die "the MMC did not answer within ${TIMEOUT}s - recover over SWD (jlink_program.sh)"

say "    running    ${AFTER}"
if [ -n "$IMG_SHA" ] && [ "$AFTER" != "$IMG_SHA" ]; then
    die "the module reports $AFTER, expected $IMG_SHA - the staged image was not applied"
fi

SENSORS=$(ipmitool $B sensor list 2>/dev/null | wc -l | tr -d ' ')
BAD=$(ipmitool $B sensor list 2>/dev/null | awk -F'|' '{gsub(/ /,"",$4); if ($4!="ok" && $4!="na" && $1!~/HOTSWAP/) print $1}' | tr '\n' ' ')
say "    sensors    $SENSORS${BAD:+  (not ok: $BAD)}"
say "    FRU        $(ipmitool $B fru print 2>/dev/null | awk -F': ' '/Board Serial/{print $2; exit}')"
say "=== done"
