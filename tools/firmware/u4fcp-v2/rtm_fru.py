#!/usr/bin/env python3
"""Read / verify the uRTM2.3 FRU EEPROM over IPMI (two mux stages).

Path: AMC TCA9548A 0x70 ch5 (RTM bus) -> RTM TCA9548A 0x71 ch7 -> 24C512 0x55.
usage: rtm_fru.py [--mch IP] [--target 0x88] [--bus 0] [--dev 0x55]
                  [--rtm-ch 7] [--dump FILE]
"""
import argparse
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "uFC_toolbox")))
from fru_core import FruAccess  # noqa: E402

MUX = [(0x70, 5), (0x71, 7)]


def checksum8(data):
    return (0x100 - (sum(data) & 0xFF)) & 0xFF


def parse_fru(raw):
    """very small FRU parser: common header, board and product areas"""
    if raw[0] != 0x01:
        return None
    info = {"internal": raw[1], "chassis": raw[2], "board": raw[3],
            "product": raw[4], "multirecord": raw[5]}
    out = {}
    if info["board"]:
        off = info["board"] * 8
        out["board"] = {"lang": raw[off + 2] & 0x3F,
                        "mfg_time": int.from_bytes(raw[off + 3:off + 6], "little")}
    if info["product"]:
        off = info["product"] * 8
        out["product"] = {"lang": raw[off + 2] & 0x3F}
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mch", default="192.168.1.41")
    ap.add_argument("--target", default="0x88")
    ap.add_argument("--bus", default="0")
    ap.add_argument("--dev", default="0x55", type=lambda x: int(x, 0))
    ap.add_argument("--rtm-ch", default=7, type=int,
                    help="uRTM TCA9548A channel of the 24C512 (0..7)")
    ap.add_argument("--dump", default="")
    args = ap.parse_args()

    acc = FruAccess(args.mch, args.target, "0x82", args.bus, i2c_addr=args.dev)
    mux = [(0x70, 5), (0x71, args.rtm_ch)]
    # the IPMI I2C transfer is limited to 31 read bytes: read in chunks
    raw = bytearray()
    for off in range(0, 512, 31):
        n = min(31, 512 - off)
        chunk = acc.xfer(1, mux, args.dev,
                         bytes([off >> 8, off & 0xFF]), n)
        raw += chunk
    raw = bytes(raw)
    if args.dump:
        open(args.dump, "wb").write(raw)
    if all(b == 0xFF for b in raw):
        print("RTM FRU EEPROM blank (all 0xFF) - not programmed")
        return 0
    hdr = raw[:8]
    ok = hdr[0] == 0x01 and checksum8(raw[:8]) == 0
    print("common header:", hdr.hex(), "checksum OK" if ok else "CHECKSUM BAD")
    info = parse_fru(raw)
    print("areas:", info)
    if not ok:
        return 1
    print("RTM FRU valid (%d bytes read)" % len(raw))
    return 0


if __name__ == "__main__":
    sys.exit(main())
