#!/usr/bin/env python3
"""Capture / restore the u4FCP clock configuration store.

The MMC keeps the clocking setup (ADN4604 switch matrix + Si5345 PLL records)
in its CAT24C512 EEPROM: pointer at 0x0200, slot A at 0x0300, slot B at
0x1B00. A brand-new board has no valid slot, so the stored configuration has
to be copied from a programmed board (or created with the toolbox's Clock tab).

usage:
  program_clock.py --dump  FILE [--mch IP] [--slot N] [--bus B]
  program_clock.py --upload FILE [--mch IP] [--slot N] [--bus B]

  --slot N   AMC slot (target = 0x70+2*N)   default 12
  --bus B    IPMB-L bus of the MCH bridge   default 0

The file is a raw copy of the clock store: 256 bytes of pointer area
(0x0200-0x02FF) followed by the used slot bytes (slot A at 0x0300).
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "uFC_toolbox")))
from fru_core import FruAccess  # noqa: E402

PTR_OFF = 0x0200
PTR_LEN = 256
SLOT_A = 0x0300


def access_for(args):
    target = "0x%02x" % (0x70 + 2 * args.slot)
    return FruAccess(args.mch, target, "0x82", str(args.bus), i2c_addr=0x54)


def clock_status(access):
    rsp = access.raw("0x32", "0x18")
    if not rsp:
        raise RuntimeError("clock store status failed")
    valid, slot, length = rsp[0], rsp[1], rsp[2] | (rsp[3] << 8)
    crc = rsp[4] | (rsp[5] << 8) | (rsp[6] << 16) | (rsp[7] << 24)
    return valid, slot, length, crc


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dump", metavar="FILE")
    ap.add_argument("--upload", metavar="FILE")
    ap.add_argument("--mch", default="192.168.1.41")
    ap.add_argument("--slot", type=int, default=12)
    ap.add_argument("--bus", default="0")
    args = ap.parse_args()
    if bool(args.dump) == bool(args.upload):
        ap.error("use exactly one of --dump or --upload")

    access = access_for(args)
    valid, slot_idx, length, crc = clock_status(access)
    print("clock store: valid=%d slot=%d len=%d crc=0x%08x"
          % (valid, slot_idx, length, crc))

    if args.dump:
        if not valid:
            sys.exit("no valid slot to dump")
        ptr = access.read(PTR_OFF, PTR_LEN)
        base = SLOT_A + slot_idx * 0x1800
        slot = access.read(base, length)
        open(args.dump, "wb").write(ptr + slot)
        print("dumped %d bytes (ptr %d + slot %d) to %s"
              % (len(ptr) + len(slot), len(ptr), len(slot), args.dump))
        return

    blob = open(args.upload, "rb").read()
    if len(blob) < PTR_LEN or len(blob) > PTR_LEN + 6144:
        sys.exit("bad clock store file (%d bytes)" % len(blob))
    ptr, slot = blob[:PTR_LEN], blob[PTR_LEN:]
    access.write(PTR_OFF, ptr)
    access.write(SLOT_A if slot_idx in (0, 0xFF) else SLOT_A + slot_idx * 0x1800, slot)
    valid, slot_idx, length, crc = clock_status(access)
    print("restored: valid=%d slot=%d len=%d crc=0x%08x"
          % (valid, slot_idx, length, crc))
    if not valid:
        sys.exit("restored clock store is not valid")


if __name__ == "__main__":
    main()
