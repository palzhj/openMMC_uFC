#!/usr/bin/env python3
"""Program the production FRU (unique serial number) into a u4FCP-v2 MMC.

The FRU image is built from fru_template.bin (header + board/product areas +
the 100-byte multi-record area with the point-to-point/clock/module-current
records), with the board and product serials replaced by the argument and the
manufacturing time set to now (or --mfg-time).

The image is written to the FRU EEPROM (CAT24C512 at 0x54) through the MMC's
IPMI EEPROM access commands, then read back and verified.

usage: program_fru.py SERIAL [options]

  --mch IP          MCH / BMC address            (default 192.168.1.41)
  --slot N          AMC slot (target = 0x70+2*N) (default 12)
  --bus B           IPMB-L bus of the MCH bridge (default 0)
  --target ADDR     override the target IPMB address (default: from --slot)
  --template FILE   FRU template image           (default fru_template.bin)
  --config FILE     field values JSON            (default fru_config.json)
  --mfg-time STR    manufacturing time ("YYYY-MM-DD HH:MM" or minutes) (default now)
  --dry-run         only build the image, print a summary, do not write
  --no-verify       skip the read-back verification
"""
import argparse
import datetime
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "uFC_toolbox")))
from fru_core import (FruAccess, build_image, image_for_eeprom, parse_fru,  # noqa: E402
                      mfg_minutes_to_str, mfg_str_to_minutes)


def now_minutes():
    delta = datetime.datetime.now() - datetime.datetime(1996, 1, 1)
    return int(delta.total_seconds() // 60)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("serial")
    ap.add_argument("--mch", default="192.168.1.41")
    ap.add_argument("--slot", type=int, default=12)
    ap.add_argument("--bus", default="0")
    ap.add_argument("--target", default=None)
    ap.add_argument("--template", default=os.path.join(HERE, "fru_template.bin"))
    ap.add_argument("--config", default=os.path.join(HERE, "fru_config.json"))
    ap.add_argument("--mfg-time", default=None)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--no-verify", action="store_true")
    args = ap.parse_args()

    serial = args.serial.strip()
    if not serial:
        sys.exit("serial number must not be empty")
    if len(serial) > 62:
        sys.exit("serial number is too long (max 62 chars)")

    template = open(args.template, "rb").read()
    parsed = parse_fru(template)
    cfg = json.load(open(args.config))

    if args.mfg_time:
        mfg = args.mfg_time if not args.mfg_time.isdigit() else int(args.mfg_time)
    else:
        mfg = now_minutes()

    values = {
        "board": dict(cfg.get("board", {}), serial=serial, mfg_time=mfg),
        "product": dict(cfg.get("product", {}), serial=serial),
    }
    if "module_current_ma" in cfg:
        # the FRU 'module current' record stores 100 mA units
        values["module_current"] = int(cfg["module_current_ma"]) // 100

    image = build_image(parsed, values, limit=512)
    image = image_for_eeprom(image, 512)
    print("FRU image: %d bytes, serial %r, mfg %s"
          % (len(image), serial, mfg_minutes_to_str(mfg) if isinstance(mfg, int) else mfg))

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
