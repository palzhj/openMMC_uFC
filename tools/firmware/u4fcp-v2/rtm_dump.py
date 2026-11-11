#!/usr/bin/env python3
"""Record the current register config of every responsive uRTM2.3 I2C device."""
import sys, os, json
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "uFC_toolbox")))
from fru_core import FruAccess

TREE = [("bus", [(0x3C, ""), (0x3E, "RTM IO")]),
        ("0x71 ch2", [(0x49, "CLK MUX"), (0x6A, "PLL2")]),
        ("0x71 ch3", [(0x3D, "JTAG IO"), (0x72, "FPGA mux")]),
        ("0x71 ch7", [(0x55, "24C512 FRU"), (0x57, "pot"), (0x5B, "PMBus")]),
        ("0x72 ch0", [(0x3F, "ReDriverIO"), (0x76, "ReDriver B"), (0x7E, "ReDriver A")]),
        ("0x72 ch1", [(0x3F, "ReDriverIO"), (0x76, "ReDriver B"), (0x7E, "ReDriver A")]),
        ("0x72 ch2", [(0x3D, "JTAG IO")])]

def main():
    acc = FruAccess("192.168.1.41", "0x88", "0x82", "0", i2c_addr=0x55)
    paths = {"bus": [(0x70, 5)], "0x71 ch2": [(0x70,5),(0x71,2)],
             "0x71 ch3": [(0x70,5),(0x71,3)], "0x71 ch7": [(0x70,5),(0x71,7)],
             "0x72 ch0": [(0x70,5),(0x71,3),(0x72,0)],
             "0x72 ch1": [(0x70,5),(0x71,3),(0x72,1)],
             "0x72 ch2": [(0x70,5),(0x71,3),(0x72,2)]}
    out = {}
    for label, devs in TREE:
        for dev, name in devs:
            try:
                b = bytearray()
                for off in (0x00, 0x08, 0x10, 0x18):
                    b += acc.xfer(1, paths[label], dev, bytes([off]), 8)
                out["%s/0x%02X" % (label, dev)] = {"name": name, "regs": b.hex()}
                print("%-9s 0x%02X %-12s %s" % (label, dev, name, b.hex()))
            except Exception:
                print("%-9s 0x%02X %-12s NACK" % (label, dev, name))
    json.dump(out, open("docs/rtm_i2c_baseline.json", "w"), indent=1)
    print("saved docs/rtm_i2c_baseline.json")
    return 0

if __name__ == "__main__":
    sys.exit(main())
