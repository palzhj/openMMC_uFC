#!/usr/bin/env python3
"""Read the uRTM2.3 rails from its UCD90124A (0x5B, 0x70 ch5 -> 0x71 ch7).

Every PMBus page has its own VOUT_MODE exponent: the 12 V pages (0, 2, 5)
use 0x16 = x2^-10, the 3.3 V / VADJ pages 0x14 = x2^-12.  The exponent is
read from the selected page, so READ_VOUT is decoded correctly.

The AMC MMC polls the same UCD once per second (rtm_sensors.c) and each
read here is a separate IPMI I2C-transfer command, so a page switch from the
MMC can land between two commands.  The page is therefore verified against
its VOUT_MODE fingerprint after the readings and retried on mismatch.

READ_VOUT already has the VOUT_SCALE_MONITOR divider applied by the UCD (the
reported value is the rail voltage); the monitor-pin voltage is therefore
VOUT x scale.
"""
import sys, os
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "uFC_toolbox")))
from fru_core import FruAccess

PATH = [(0x70, 5), (0x71, 7)]
DEV = 0x5B

# page map from uRTMv2.3_with_current_limitation.tifsp (RailDefinitions) and
# VOUT_SCALE_MONITOR (0x2A) from the same project: RTM_12V uses the 5.1k + 1k
# divider on MON9: 1k/(5.1k+1k) = 0.164; the other rails use 0.5.
RAILS = {0: ("RTM_12V", 0.164), 1: ("MP_3V3", 0.5),
         2: ("FMC2_12V_CURR", 0.164), 3: ("FMC2_VADJ", 0.5),
         4: ("FMC2_3V3_CURR", 0.5), 5: ("FMC3_12V_CURR", 0.164),
         6: ("FMC3_VADJ", 0.5), 7: ("FMC3_3V3_CURR", 0.5),
         8: ("IO_3V3", 0.5)}

# page fingerprint (VOUT_MODE, VOUT_COMMAND raw): the pages sharing a rail
# family are indistinguishable, but it catches a MMC page switch under us
EXPECTED_PAGE = {0: (0x16, 0x3000), 1: (0x14, 0x34cd), 2: (0x16, 0x3000),
                 3: (0x14, 0x22b8), 4: (0x14, 0x34cd), 5: (0x16, 0x3000),
                 6: (0x14, 0x22b8), 7: (0x14, 0x34cd), 8: (0x14, 0x34cd)}


def linear11(raw):
    """PMBus LINEAR11: R = Y*2^X, Y = low 11 bits signed, X = high 5 bits signed."""
    y = raw & 0x7FF
    if y & 0x400:
        y -= 0x800
    x = raw >> 11
    if x & 0x10:
        x -= 0x20
    return y * (2.0 ** x)


def vout_mode_exp(mode):
    """VOUT_MODE: [7:5] mode (0 = LINEAR), [4:0] two's complement exponent."""
    e = mode & 0x1F
    return e - 0x20 if e & 0x10 else e


def main():
    acc = FruAccess("192.168.1.41", "0x88", "0x82", "0", i2c_addr=DEV)

    print("%-14s %-5s %-6s %10s %10s %9s %7s" %
          ("rail", "page", "VOUT_MODE", "VOUT(rail)", "pin=VOUT*S", "IOUT", "TEMP"))
    for page in sorted(RAILS):
        name, scale = RAILS[page]
        mode = None
        for _attempt in range(5):
            try:
                acc.xfer(1, PATH, DEV, bytes([0x00, page]), 0)
                mode = acc.xfer(1, PATH, DEV, bytes([0x20]), 1)[0]
                cmd = acc.xfer(1, PATH, DEV, bytes([0x21]), 2)
                v = acc.xfer(1, PATH, DEV, bytes([0x8B]), 2)
                i = acc.xfer(1, PATH, DEV, bytes([0x8C]), 2)
                t = acc.xfer(1, PATH, DEV, bytes([0x8D]), 2)
                mode_after = acc.xfer(1, PATH, DEV, bytes([0x20]), 1)[0]
                cmd_after = acc.xfer(1, PATH, DEV, bytes([0x21]), 2)
            except Exception as e:
                print("%-14s %-5d %s" % (name, page, str(e)[-50:]))
                mode = None
                break
            if ((mode, cmd[1] << 8 | cmd[0]) == EXPECTED_PAGE[page] and
                    (mode_after, cmd_after[1] << 8 | cmd_after[0]) == EXPECTED_PAGE[page]):
                break
            mode = None          # page switched under us: retry
        if mode is None:
            print("%-14s %-5d page could not be confirmed (MMC is polling)" % (name, page))
            continue
        exp = vout_mode_exp(mode)

        vout = (v[1] << 8 | v[0]) * (2.0 ** exp)
        amps = linear11(i[1] << 8 | i[0])
        degc = linear11(t[1] << 8 | t[0])
        print("%-14s %-5d 0x%02x    %8.3f V %8.3f V %7.3f A %5.1f C" %
              (name, page, mode, vout, vout * scale, amps, degc))
    return 0


if __name__ == "__main__":
    sys.exit(main())
