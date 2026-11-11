#!/usr/bin/env python3
"""Read the uRTM2.3 rails from its UCD90124A (0x5B, 0x70 ch5 -> 0x71 ch7)."""
import sys, os
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "uFC_toolbox")))
from fru_core import FruAccess

PATH = [(0x70, 5), (0x71, 7)]
DEV = 0x5B

def lin(raw, exp):
    """PMBus linear format: value = raw * 2^exp (two's complement raw)."""
    v = raw if raw < 0x8000 else raw - 0x10000
    return v * (2.0 ** exp)

def main():
    acc = FruAccess("192.168.1.41", "0x88", "0x82", "0", i2c_addr=DEV)
    mode = acc.xfer(1, PATH, DEV, bytes([0x20]), 1)[0]
    exp = mode if mode < 0x10 else mode - 0x20
    print("VOUT_MODE=0x%02x -> exponent %d" % (mode, exp))
    for page in range(12):
        try:
            acc.xfer(1, PATH, DEV, bytes([0x00, page]), 0)
            v = acc.xfer(1, PATH, DEV, bytes([0x8B]), 2)
            i = acc.xfer(1, PATH, DEV, bytes([0x8C]), 2)
            t = acc.xfer(1, PATH, DEV, bytes([0x8D]), 2)
            vout = lin(v[1] << 8 | v[0], exp)
            # IOUT/TEMPERATURE use the UCD's own coefficients
            # (MFR_IOUT_COEFF etc. are not read here): print the raw values
            iraw = i[1] << 8 | i[0]
            traw = t[1] << 8 | t[0]
            if vout == 0:
                continue
            print("page %2d: VOUT=%7.3f V   IOUT=0x%04x   TEMP=0x%04x"
                  % (page, vout, iraw, traw))
        except Exception:
            pass
    return 0

if __name__ == "__main__":
    sys.exit(main())
