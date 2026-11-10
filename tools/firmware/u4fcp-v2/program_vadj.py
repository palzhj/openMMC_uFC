#!/usr/bin/env python3
"""Set / read the FMC VADJ of a u4FCP through the TPL0102 digital pot.

The FMC0/FMC1 VADJ regulators are trimmed by a TPL0102-100PWR (I2C 0x56 on
the power bus, mux 0x70 channel 7). Same conversion as the MMC firmware
(port/board/u4fcp-v2/payload.c):

    DAC  = (60.4/(V/0.6 - 1) - 30.2) * 256/100     V in 0.9 .. 1.8 V
    V    = 0.6 * (60.4/(30.2 + DAC*100/256) + 1)

DAC 0 = 1.8 V (factory default), larger = lower voltage.
Writing the non-volatile (IVR) register makes the value survive power
cycles; the volatile (WR) one only lasts until the board is powered off.

usage: program_vadj.py [--fmc0 V] [--fmc1 V] [--persist] [--read]
                       [--mch IP] [--slot N] [--bus B] [--i2c ADDR]
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "uFC_toolbox")))
from fru_core import FruAccess  # noqa: E402

ACR = 0x10
IVR_A, IVR_B = 0x00, 0x01
ACR_VOL, ACR_ENABLE = 0x80, 0x40


def volts_to_dac(volts):
    volts = max(0.9, min(1.8, volts))
    dac = int((60.4 / (volts / 0.6 - 1.0) - 30.2) * 256 / 100)
    return max(0, min(255, dac))


def dac_to_volts(dac):
    return 0.6 * (60.4 / (30.2 + dac * 100.0 / 256.0) + 1.0)


class Tpl:
    def __init__(self, args):
        self.i2c = args.i2c
        self.acc = FruAccess(args.mch, "0x%02x" % (0x70 + 2 * args.slot),
                             "0x82", str(args.bus), i2c_addr=0x54)
        self.mux = [(0x70, 7)]

    def write(self, reg, val):
        self.acc.xfer(1, self.mux, self.i2c, bytes([reg, val]), 0)

    def read(self, reg):
        return self.acc.xfer(1, self.mux, self.i2c, bytes([reg]), 1)[0]

    def select(self, volatile):
        self.write(ACR, (ACR_VOL if volatile else 0) | ACR_ENABLE)

    def set_dac(self, chn, dac, persist):
        self.select(volatile=not persist)
        self.write(IVR_A if chn == 0 else IVR_B, dac)
        self.select(volatile=True)          # back to the normal WR access

    def get_dac(self, chn):
        self.select(volatile=True)
        return self.read(IVR_A if chn == 0 else IVR_B)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fmc0", type=float, default=None, help="FMC0 VADJ in volts (0.9..1.8)")
    ap.add_argument("--fmc1", type=float, default=None, help="FMC1 VADJ in volts (0.9..1.8)")
    ap.add_argument("--persist", action="store_true",
                    help="store in the TPL0102 non-volatile register (survives power off)")
    ap.add_argument("--read", action="store_true", help="only read and print the VADJ")
    ap.add_argument("--mch", default="192.168.1.41")
    ap.add_argument("--slot", type=int, default=12)
    ap.add_argument("--bus", default="0")
    ap.add_argument("--i2c", default="0x56")
    args = ap.parse_args()
    args.i2c = int(args.i2c, 0)

    tpl = Tpl(args)
    if args.read or (args.fmc0 is None and args.fmc1 is None):
        for chn, name in ((0, "FMC0"), (1, "FMC1")):
            dac = tpl.get_dac(chn)
            print("%s: DAC %3d  -> %.3f V" % (name, dac, dac_to_volts(dac)))
        return

    for chn, name, volts in ((0, "FMC0", args.fmc0), (1, "FMC1", args.fmc1)):
        if volts is None:
            continue
        dac = volts_to_dac(volts)
        tpl.set_dac(chn, dac, args.persist)
        rb = tpl.get_dac(chn)
        print("%s: %.3f V (DAC %d, %s) -> read back DAC %d = %.3f V"
              % (name, volts, dac, "IVR/persistent" if args.persist else "WR/volatile",
                 rb, dac_to_volts(rb)))


if __name__ == "__main__":
    main()
