#!/usr/bin/env python3
"""Build the production clock store preset for the u4FCP from the firmware's
compiled clocking (payload.c matrix + default_0/1-Registers.h PLL tables),
upload it through the MMC clock-store commands and capture it as the preset.

usage: build_clk_preset.py [--upload] [--capture FILE] [--write-local FILE]
                           [--mch IP] [--slot N] [--bus B]
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
BOARD = os.path.join(REPO, "port", "board", "u4fcp-v2")
sys.path.insert(0, os.path.join(REPO, "tools", "uFC_toolbox"))
from fru_core import FruAccess, build_clock_slot, upload_clock_slot  # noqa: E402


def parse_matrix():
    """clock_config[] from payload.c (ADN4604 input select + enable)."""
    src = open(os.path.join(BOARD, "payload.c")).read()
    m = re.search(r"clock_config\[0\]\s*=\s*([^;]+);", src)
    if not m:
        raise SystemExit("clock_config[] not found in payload.c")
    defs = {"ADN4604_IN_PLL1_OUT": 8, "ADN4604_OUTPUT_EN": 0x80}
    matrix = []
    for line in re.findall(r"clock_config\[(\d+)\]\s*=\s*([^;]+);", src):
        idx, expr = int(line[0]), line[1]
        val = 0
        for part in re.split(r"[|+]", expr):
            part = part.strip()
            if part in defs:
                val |= defs[part]
            elif part.isdigit():
                val |= int(part)
            else:
                raise SystemExit("unknown clock_config term %r" % part)
        matrix.append((idx, val))
    matrix.sort()
    return bytes(v for _, v in matrix)


def parse_regs(path):
    src = open(path).read()
    regs = []
    for addr, data in re.findall(r"\{\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)\s*\}", src):
        regs.append((int(addr, 16), int(data, 16)))
    return regs


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--upload", action="store_true", help="upload to the board")
    ap.add_argument("--capture", metavar="FILE", help="dump the store to FILE after upload")
    ap.add_argument("--write-local", metavar="FILE", help="write the built slot to FILE")
    ap.add_argument("--mch", default="192.168.1.41")
    ap.add_argument("--slot", type=int, default=12)
    ap.add_argument("--bus", default="0")
    args = ap.parse_args()

    matrix = parse_matrix()
    regs0 = parse_regs(os.path.join(BOARD, "default_0-Registers.h"))
    regs1 = parse_regs(os.path.join(BOARD, "default_1-Registers.h"))
    print("matrix:", " ".join("%02x" % b for b in matrix))
    print("PLL0: %d registers, PLL1: %d registers" % (len(regs0), len(regs1)))

    slot = build_clock_slot(matrix, si_tables=[(0x68, {"pre": [], "body": regs0, "post": []}),
                                               (0x69, {"pre": [], "body": regs1, "post": []})],
                            switch_addr=0x48, seq=1)
    print("slot: %d bytes" % len(slot))
    if args.write_local:
        open(args.write_local, "wb").write(slot)
        print("slot written to", args.write_local)

    if not (args.upload or args.capture):
        return
    acc = FruAccess(args.mch, "0x%02x" % (0x70 + 2 * args.slot), "0x82", str(args.bus),
                    i2c_addr=0x54)
    if args.upload:
        rsp = upload_clock_slot(acc, slot, progress=lambda n: print("  uploaded %d B" % n))
        print("finish:", bytes(rsp).hex())
    if args.capture:
        ptr = acc.read(0x200, 256)
        valid, slot_idx, length, crc = (lambda r: (r[0], r[1], r[2] | (r[3] << 8),
                                                   r[4] | (r[5] << 8) | (r[6] << 16) | (r[7] << 24)))(
            acc.raw("0x32", "0x18"))
        print("store: valid=%d slot=%d len=%d crc=0x%08x" % (valid, slot_idx, length, crc))
        if valid:
            data = acc.read(0x300 + slot_idx * 0x1800, length)
            open(args.capture, "wb").write(ptr + data)
            print("captured %d bytes to %s" % (len(ptr) + len(data), args.capture))


if __name__ == "__main__":
    main()
