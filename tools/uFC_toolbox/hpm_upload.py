#!/usr/bin/env python3
"""HPM.1 firmware upload to an MMC through the MCH IPMB bridge.

Usage:
    hpm_upload.py <image.bin> [--mch 192.168.1.41] [--target 0x88]
                  [--transit 0x82] [--bus 0] [--component 1] [--no-activate]

The openMMC HPM implementation runs on the PICMG group-extension netfn
(0x2C) with IPMI_PICMG_GRP_EXT (0x00) as the first data byte:
  capabilities  0x2E 00
  properties    0x2F 00 <comp> <selector>
  initiate      0x31 00 <mask> <action>       (action 1 = upload for upgrade)
  upload block  0x32 00 <block> <data...>     (<= 20 payload bytes over IPMB)
  finish        0x33 00 <0> <size le32>
  activate      0x35 00
"""
import argparse
import subprocess
import sys
import time

BLOCK = 20          # IPMB frame limit: 6 header + 2 netfn/cmd + 2 + payload <= 32


def ipmitool(bridge, args, retries=6, delay=0.3):
    last = ""
    for _ in range(retries):
        out = subprocess.run(["ipmitool"] + bridge + ["raw"] + list(args),
                             capture_output=True, text=True, timeout=20)
        if out.returncode == 0:
            return bytes(int(x, 16) for x in out.stdout.split())
        last = (out.stderr or out.stdout).strip()
        # 0xC0 "command in progress" is a normal answer for long-duration
        # commands (a block that flushed a flash page).
        if "rsp=0xc0" in last:
            return b""
        time.sleep(delay)
    raise RuntimeError(last)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--mch", default="192.168.1.41")
    ap.add_argument("--target", default="0x88")
    ap.add_argument("--transit", default="0x82")
    ap.add_argument("--bus", default="0")
    ap.add_argument("--component", type=int, default=1)   # 0 = bootloader, 1 = MMC
    ap.add_argument("--no-activate", action="store_true")
    a = ap.parse_args()

    bridge = ["-I", "lan", "-H", a.mch, "-A", "none",
              "-T", a.transit, "-B", "0", "-t", a.target, "-b", a.bus]
    mask = 1 << a.component
    img = open(a.image, "rb").read()
    size = len(img)
    print("image %s (%d bytes), component %d" % (a.image, size, a.component))

    caps = ipmitool(bridge, ["0x2c", "0x2e", "0x00"])
    print("capabilities:", caps.hex())
    props = ipmitool(bridge, ["0x2c", "0x2f", "0x00", "0x%02x" % a.component, "0x00"])
    print("component properties:", props.hex())

    print("initiate (prepare):", ipmitool(bridge, ["0x2c", "0x31", "0x00",
                                                   "0x%02x" % mask, "0x01"]).hex())
    blk = 0
    pos = 0
    t0 = time.time()
    while pos < size:
        chunk = img[pos:pos + BLOCK]
        args = ["0x2c", "0x32", "0x00", "0x%02x" % (blk & 0xFF)]
        args += ["0x%02x" % b for b in chunk]
        ipmitool(bridge, args)
        pos += len(chunk)
        blk += 1
        if blk % 200 == 0:
            print("  %d/%d bytes  %.1f s" % (pos, size, time.time() - t0), flush=True)
    print("uploaded %d bytes in %.1f s" % (pos, time.time() - t0))

    lo, l1, l2, l3 = size & 0xFF, (size >> 8) & 0xFF, (size >> 16) & 0xFF, (size >> 24) & 0xFF
    print("finish:", ipmitool(bridge, ["0x2c", "0x33", "0x00", "0x00",
                                       "0x%02x" % lo, "0x%02x" % l1,
                                       "0x%02x" % l2, "0x%02x" % l3]).hex())
    if not a.no_activate:
        print("activate:", ipmitool(bridge, ["0x2c", "0x35", "0x00"]).hex())
        print("the MMC reboots; the bootloader copies the staged image")


if __name__ == "__main__":
    sys.exit(main())
