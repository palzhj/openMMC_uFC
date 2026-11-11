#!/usr/bin/env python3
"""Probe the uRTM2.3 I2C devices (two mux stages: 0x70 ch5 -> 0x71 chN)."""
import sys, os
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "uFC_toolbox")))
from fru_core import FruAccess

KNOWN = {0: [(0x52, "FMC2 EEPROM")], 1: [(0x53, "FMC3 EEPROM")],
         2: [(0x49, "CLK MUX"), (0x6A, "PLL2")],
         3: [(0x72, "FPGA IIC MUX"), (0x3F, "ReDriverIO"), (0x3D, "JTAG IO")],
         7: [(0x55, "24C512"), (0x5B, "PMBus"), (0x57, "potentiometer")]}

def main():
    acc = FruAccess("192.168.1.41", "0x88", "0x82", "0", i2c_addr=0x55)
    print("== direct on the RTM bus (0x70 ch5) ==")
    for dev, name in ((0x3C, "?"), (0x3E, "RTM IO")):
        try:
            v = acc.xfer(1, [(0x70, 5)], dev, bytes([0]), 1)
            print("  0x%02X %-8s ACK %s" % (dev, name, v.hex()))
        except Exception:
            print("  0x%02X %-8s NACK" % (dev, name))
    print("== behind the RTM FPGA mux 0x72 (0x71 ch3 -> 0x72 ch0..2) ==")
    for ch in range(3):
        found = []
        for dev in range(0x08, 0x78):
            try:
                acc.xfer(1, [(0x70, 5), (0x71, 3), (0x72, ch)], dev, bytes([0]), 1)
                found.append("0x%02X" % dev)
            except Exception:
                pass
        print("  0x72 ch%d: %s" % (ch, " ".join(found) or "(nothing)"))
    for ch in range(8):
        for dev, name in KNOWN.get(ch, []):
            try:
                v = acc.xfer(1, [(0x70, 5), (0x71, ch)], dev, bytes([0]), 1)
                print("  ch%d 0x%02X %-14s ACK %s" % (ch, dev, name, v.hex()))
            except Exception:
                print("  ch%d 0x%02X %-14s NACK" % (ch, dev, name))
    return 0

if __name__ == "__main__":
    sys.exit(main())
