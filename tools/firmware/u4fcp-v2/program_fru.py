#!/usr/bin/env python3
"""Program the production FRU (unique serial number) into a u4FCP-v2 MMC.

The script loads the default FRU image (fru_template.bin: header + board and
product areas + the 100-byte multi-record area with the point-to-point, clock
and module-current records), puts the serial number into the board and product
areas and stamps the manufacturing time with "now" (the moment the script
runs, as a production flow wants); every other field and the multi-record area
stay exactly as they are in the template.  --mfg-time forces a different time.

The image is written to the FRU EEPROM (CAT24C512 at 0x54) through the MMC's
IPMI EEPROM access commands, then read back and verified.

usage: program_fru.py [options] --sn SERIAL

  --mch IP          MCH / BMC address            (default 192.168.1.41)
  --slot N          AMC slot (target = 0x70+2*N) (default 12)
  --bus B           IPMB-L bus of the MCH bridge (default 0)
  --target ADDR     override the target IPMB address (default: from --slot)
  --template FILE   FRU template image           (default fru_template.bin)
  --mfg-time STR    manufacturing time ("YYYY-MM-DD HH:MM" or minutes);
                    default: the time the script runs
  --dry-run         only build the image, print a summary, do not write
  --no-verify       skip the read-back verification
"""
import argparse
import datetime
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "uFC_toolbox")))
from fru_core import (FruAccess, parse_fru,  # noqa: E402
                      mfg_minutes_to_str)

import fru_template  # noqa: E402


def now_minutes():
    """Minutes since 1996-01-01 00:00 UTC, i.e. the FRU manufacturing time."""
    now = datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None)
    return int((now - datetime.datetime(1996, 1, 1)).total_seconds() // 60)


def build_from_template(template_path, serial, mfg_time=None):
    """Load the template and change only the serial (fru_template.py set).

    Returns the 512-byte EEPROM image; everything except the board/product
    serial (and the manufacturing time when mfg_time is given) is untouched,
    including the multi-record area.
    """
    if mfg_time is None:                 # no explicit time: stamp "now"
        mfg_time = now_minutes()
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "fru.bin")
        ns = argparse.Namespace(file=template_path, board=[], product=[],
                                serial=serial, module_current_ma=None,
                                output=out, in_place=False,
                                mfg_time=mfg_time, size=512, dry_run=False)
        fru_template.cmd_set(ns)
        if os.path.exists(out):
            return open(out, "rb").read()
        # "no changes requested": the serial already matches the template, so
        # the template itself is the image to program
        raw = open(template_path, "rb").read()[:512]
        return raw + b"\xff" * (512 - len(raw))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("serial", nargs="?", default=None,
                    help="serial number (same as --sn; use --sn and put it last)")
    ap.add_argument("--sn", default=None, metavar="SERIAL",
                    help="serial number for the board and product areas")
    ap.add_argument("--mch", default="192.168.1.41")
    ap.add_argument("--slot", type=int, default=12)
    ap.add_argument("--bus", default="0")
    ap.add_argument("--target", default=None)
    ap.add_argument("--template", default=os.path.join(HERE, "fru_template.bin"))
    ap.add_argument("--mfg-time", default=None)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--no-verify", action="store_true")
    args = ap.parse_args()

    serial = (args.sn or args.serial or "").strip()
    if not serial:
        ap.error("missing serial number: use --sn SERIAL (put it last)")
    if not serial:
        sys.exit("serial number must not be empty")
    if len(serial) > 62:
        sys.exit("serial number is too long (max 62 chars)")

    image = build_from_template(args.template, serial, args.mfg_time)
    parsed = parse_fru(image)
    print("FRU image: %d bytes from %s, serial %r, mfg %s UTC"
          % (len(image), os.path.basename(args.template), serial,
             mfg_minutes_to_str(parsed["board"].get("mfg_time", 0))))

    if args.dry_run:
        out = "/tmp/u4fcp-fru-%s.bin" % serial.replace("/", "_")
        open(out, "wb").write(image)
        print("dry run: image written to", out)
        return

    target = args.target
    if target is None:
        target = "0x%02x" % (0x70 + 2 * args.slot)
    access = FruAccess(args.mch, target, "0x82", str(args.bus), i2c_addr=0x54)
    print("writing to %s via %s, target %s, bus %s ..."
          % ("0x54", args.mch, target, args.bus))
    access.write(0, image)
    print("write done")

    if args.no_verify:
        return
    raw = access.read(0, 512)
    parsed = parse_fru(raw)
    board = {f["name"]: f["value"] for f in (parsed["board_area"] or {}).get("fields", [])}
    product = {f["name"]: f["value"] for f in (parsed["product_area"] or {}).get("fields", [])}
    checks = [
        ("board serial", board.get("serial") == serial),
        ("product serial", product.get("serial") == serial),
        ("board checksum", (parsed["board_area"] or {}).get("checksum") is not None),
        ("product checksum", (parsed["product_area"] or {}).get("checksum") is not None),
    ]
    for name, ok in checks:
        print("  %-18s %s" % (name, "OK" if ok else "FAIL"))
    if not all(ok for _, ok in checks):
        sys.exit(1)
    print("verified: board %s | product %s | mfg %s"
          % (board.get("product_name"), product.get("product_name"),
             mfg_minutes_to_str(parsed["board"].get("mfg_time", 0))))


if __name__ == "__main__":
    main()
