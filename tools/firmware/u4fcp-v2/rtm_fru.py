#!/usr/bin/env python3
"""Read / verify / write the uRTM2.3 FRU EEPROM over IPMI (two mux stages).

Path: AMC TCA9548A 0x70 ch5 (RTM bus) -> RTM TCA9548A 0x71 ch7 -> 24C512 0x55.

usage: rtm_fru.py [--mch IP] [--target 0x88] [--bus 0] [--dev 0x55]
                  [--rtm-ch 7] [--dump FILE]
       rtm_fru.py --write TEMPLATE --serial SN     write the FRU (same SN as AMC)
"""
import argparse
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "uFC_toolbox")))
from fru_core import FruAccess, parse_fru as parse_fru_full  # noqa: E402

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


def read_image(acc, mux, dev, size):
    """Read size bytes of the 24C512 (the I2C transfer returns 31 at most)."""
    raw = bytearray()
    for off in range(0, size, 31):
        n = min(31, size - off)
        raw += acc.xfer(1, mux, dev, bytes([off >> 8, off & 0xFF]), n)
    return bytes(raw)


def write_image(acc, mux, dev, image):
    """Write a FRU image into the 24C512, 14 bytes at a time and never across
    a 128-byte page boundary (the transfer command takes 16 bytes incl. the
    16-bit offset, and an EEPROM page write wraps inside the page)."""
    pos = 0
    while pos < len(image):
        n = min(14, 128 - (pos % 128), len(image) - pos)
        acc.xfer(1, mux, dev, bytes([pos >> 8, pos & 0xFF]) + image[pos:pos + n], 0)
        pos += n


def make_image(template_path, serial):
    """Load the template and put the serial into board and product areas.

    fru_template.py does the editing: it keeps every other field as it is and
    re-pads the image (build_image() on its own would rebuild the areas from
    just the fields it is given).
    """
    if not serial:
        raise SystemExit("--serial is required with --write")
    if len(serial) > 62:
        raise SystemExit("serial number is too long (max 62 chars)")
    out = tempfile.mktemp(suffix=".bin")
    try:
        subprocess.run([sys.executable, os.path.join(HERE, "fru_template.py"),
                        "set", template_path, "--serial", serial,
                        "-o", out], check=True, stdout=subprocess.DEVNULL)
        return open(out, "rb").read()
    finally:
        if os.path.exists(out):
            os.remove(out)


def parsed_serials(raw):
    """(board serial, product serial) of a FRU image, using the toolbox parser."""
    parsed = parse_fru_full(raw)
    def field(area, name):
        return {f["name"]: f["value"]
                for f in ((parsed.get(area) or {}).get("fields") or [])}.get(name)
    return field("board_area", "serial"), field("product_area", "serial")


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
    ap.add_argument("--write", default="", metavar="TEMPLATE",
                    help="write this FRU template into the uRTM EEPROM")
    ap.add_argument("--serial", default="",
                    help="serial number to put into both areas (with --write)")
    args = ap.parse_args()

    acc = FruAccess(args.mch, args.target, "0x82", args.bus, i2c_addr=args.dev)
    mux = [(0x70, 5), (0x71, args.rtm_ch)]

    if args.write:
        image = make_image(args.write, args.serial)
        write_image(acc, mux, args.dev, image)
        print("wrote %d bytes to 0x%02x, reading back ..." % (len(image), args.dev))
        back = read_image(acc, mux, args.dev, len(image))
        if back != image:
            print("VERIFY FAILED: read back differs from the image")
            return 1
        parsed = parsed_serials(back)
        if parsed != (args.serial, args.serial):
            print("VERIFY FAILED: serials %r" % (parsed,))
            return 1
        print("verified: board serial %r, product serial %r" % parsed)
        return 0
    raw = read_image(acc, mux, args.dev, 512)
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
