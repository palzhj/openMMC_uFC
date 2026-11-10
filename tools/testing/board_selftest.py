#!/usr/bin/env python3
"""Comprehensive board self-test for the u4FCP / uFC MMC.

Runs through IPMI (the MCH bridge), the I2C transfer command and the
generic/FRU commands.  Every test prints PASS/FAIL with the observed data and
a Markdown report is written for the run.

usage: board_selftest.py [--mch IP] [--target 0x88] [--slot 12] [--soak 600]
                        [--report FILE]
"""
import argparse
import datetime
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "uFC_toolbox")))
from fru_core import FruAccess  # noqa: E402

RESULTS = []


def record(phase, name, ok, detail):
    RESULTS.append((phase, name, bool(ok), detail))
    print("%-5s %-45s %s" % ("PASS" if ok else "FAIL", name, detail))


def run(acc, mch, slot, soak):
    target = "0x88"
    # ---------------- phase 2: peripherals --------------------------------
    # FRU EEPROM: normal read + absent device
    try:
        raw = acc.read(0, 512)
        ok = raw[0] == 0x01 and len(raw) == 512
        record("2", "FRU EEPROM read (0x54, 512 B)", ok,
               "header %s" % raw[:8].hex())
    except Exception as e:
        record("2", "FRU EEPROM read (0x54, 512 B)", False, str(e)[-60:])
    record_fault(acc, "2", "FRU EEPROM absent addr 0x55",
                 lambda: acc.xfer(1, [(0x70, 7)], 0x55, bytes([0]), 1))
    # clock store status
    try:
        st = acc.raw("0x32", "0x18")
        record("2", "clock store status (0x32 0x18)", len(st) >= 8,
               "valid=%d slot=%d len=%d crc=0x%08x" %
               (st[0], st[1], st[2] | (st[3] << 8),
                st[4] | (st[5] << 8) | (st[6] << 16) | (st[7] << 24)))
    except Exception as e:
        record("2", "clock store status (0x32 0x18)", False, str(e)[-60:])
    # UCD90320: device ID + a bad PMBus command
    try:
        v = acc.xfer(1, [(0x70, 7)], 0x11, bytes([0x99]), 1)
        record("2", "UCD90320 PMBUS_ID (0x99)", len(v) == 1, v.hex())
    except Exception as e:
        record("2", "UCD90320 PMBUS_ID (0x99)", False, str(e)[-60:])
    record_fault(acc, "2", "UCD90320 absent addr 0x12",
                 lambda: acc.xfer(1, [(0x70, 7)], 0x12, bytes([0]), 1))
    # ADN4604 (u4FCP-v2 only): map/status read + absent
    for dev, name in ((0x48, "ADN4604"), (0x68, "Si5345-0"), (0x69, "Si5345-1")):
        try:
            v = acc.xfer(1, [(0x70, 2)], dev, bytes([0x90]) if dev == 0x48
                         else bytes([0x01, 0x0C]), 8 if dev == 0x48 else 2)
            record("2", "%s read" % name, True, v.hex())
        except Exception as e:
            record("2", "%s read" % name, False, str(e)[-60:])
    record_fault(acc, "2", "ADN4604 absent addr 0x49",
                 lambda: acc.xfer(1, [(0x70, 2)], 0x49, bytes([0x00]), 1))
    # TPL0102 VADJ + absent
    try:
        v = acc.xfer(1, [(0x70, 7)], 0x56, bytes([0x00]), 1)
        record("2", "TPL0102 wiper read", True, v.hex())
    except Exception as e:
        record("2", "TPL0102 wiper read", False, str(e)[-60:])
    record_fault(acc, "2", "TPL0102 absent addr 0x57",
                 lambda: acc.xfer(1, [(0x70, 7)], 0x57, bytes([0x00]), 1))
    # TCA9554 JTAG expander + absent
    try:
        v = acc.xfer(1, [(0x70, 4)], 0x39, bytes([0x01]), 1)
        record("2", "TCA9554 output reg read", True, v.hex())
    except Exception as e:
        record("2", "TCA9554 output reg read", False, str(e)[-60:])
    record_fault(acc, "2", "TCA9554 absent addr 0x3A",
                 lambda: acc.xfer(1, [(0x70, 4)], 0x3A, bytes([0x00]), 1))
    # mux: a channel without a device
    record_fault(acc, "2", "mux ch3 (no device)",
                 lambda: acc.xfer(1, [(0x70, 3)], 0x50, bytes([0]), 1))

    # ---------------- phase 3: communication ------------------------------
    try:
        v = acc.raw("0x06", "0x01")
        record("3", "IPMB Get Device ID", len(v) >= 11,
               "dev_id=0x%02x%02x fw=%d.%02x ipmi=%d.%d mfg=0x%02x%02x%02x"
               % (v[1], v[0], v[2], v[3], v[4] & 0x0F, v[4] >> 4,
                  v[8], v[7], v[6]) if len(v) >= 11 else "short")
    except Exception as e:
        record("3", "IPMB Get Device ID", False, str(e)[-60:])
    # unsupported command -> 0xC1
    try:
        acc.raw("0x06", "0x7F")
        record("3", "unsupported command rejected", False, "no error returned")
    except Exception as e:
        msg = str(e)
        record("3", "unsupported command rejected", "rsp=0xc1" in msg or "0xc1" in msg,
               msg[-60:])
    # invalid data length for Get FRU Info -> 0xC7/0xCC
    try:
        acc.raw("0x0a", "0x10")
        record("3", "short Get FRU Info rejected", False, "no error")
    except Exception as e:
        msg = str(e)
        record("3", "short Get FRU Info rejected", "0xc7" in msg or "0xcc" in msg,
               msg[-60:])
    # I2C transfer boundaries: read lengths 1, 20, 31, 32, 33
    for n in (1, 20, 31, 32, 33):
        try:
            v = acc.xfer(1, [(0x70, 7)], 0x54, bytes([0x00]), n)
            record("3", "I2C read %d B" % n, len(v) == n, "got %d" % len(v))
        except Exception as e:
            record("3", "I2C read %d B" % n, n >= 32, str(e)[-50:])
    # zero-length write
    try:
        acc.xfer(1, [(0x70, 7)], 0x54, bytes([0x00]), 0)
        record("3", "I2C zero-length write", True, "accepted")
    except Exception as e:
        record("3", "I2C zero-length write", False, str(e)[-50:])

    # ---------------- phase 5: boundaries ---------------------------------
    for name, args in (
            ("EEPROM 0x01F0 (FRU end)", (1, [(0x70, 7)], 0x54, bytes([0x01, 0xF0]), 16)),
            ("EEPROM 0x0200 (pointer)", (1, [(0x70, 7)], 0x54, bytes([0x02, 0x00]), 8)),
            ("EEPROM 0x0280 (pointer copy 2)", (1, [(0x70, 7)], 0x54, bytes([0x02, 0x80]), 8)),
            ("EEPROM 0x0300 (slot A)", (1, [(0x70, 7)], 0x54, bytes([0x03, 0x00]), 8)),
            ("EEPROM 0x1AFF (slot A end)", (1, [(0x70, 7)], 0x54, bytes([0x1A, 0xFF]), 4)),
            ("EEPROM 0x1B00 (slot B)", (1, [(0x70, 7)], 0x54, bytes([0x1B, 0x00]), 8)),
            ("EEPROM 0x32FF (slot B end)", (1, [(0x70, 7)], 0x54, bytes([0x32, 0xFF]), 4)),
            ("EEPROM 0xFFFC (end of part)", (1, [(0x70, 7)], 0x54, bytes([0xFF, 0xFC]), 4))):
        try:
            v = acc.xfer(*args)
            record("5", name, len(v) > 0, v.hex())
        except Exception as e:
            record("5", name, False, str(e)[-50:])
    # FRU size
    try:
        v = acc.read(0, 512)
        record("5", "FRU 512 B read", len(v) == 512, "%d B" % len(v))
    except Exception as e:
        record("5", "FRU 512 B read", False, str(e)[-50:])

    # ---------------- phase 4: upgrade / store failure recovery -----------
    img = b"\x00" * 200
    try:
        acc.raw("0x2c", "0x31", "0x5a", "0x02", "0x01")          # HPM prepare
        for i in range(3):
            acc.raw("0x2c", "0x32", "0x5a", "0x%02x" % i,
                    *["0x%02x" % b for b in img[i * 20:(i + 1) * 20]])
        acc.raw("0x2c", "0x33", "0x5a", "0x00", "0xff", "0x00", "0x00", "0x00")
        record("4", "HPM finish with wrong length", False, "accepted")
    except Exception as e:
        record("4", "HPM finish with wrong length", True, str(e)[-50:])
    try:
        v = acc.raw("0x06", "0x01")
        record("4", "board alive after the HPM abort", len(v) >= 11, v.hex())
    except Exception as e:
        record("4", "board alive after the HPM abort", False, str(e)[-50:])
    try:
        before = acc.raw("0x32", "0x18")
        acc.raw("0x32", "0x1a", "0xff", "0x20", "0x00", "0x00", "0x00", "0x00", "0x00")
        acc.raw("0x32", "0x1b", "0x00", "0x00", *["0x%02x" % b for b in bytes(8)])
        after = acc.raw("0x32", "0x18")
        record("4", "interrupted clock-store write keeps the old slot",
               before[0] == 1 and after[:2] == before[:2],
               "valid=%d len=%d" % (after[0], after[2] | (after[3] << 8)))
    except Exception as e:
        record("4", "interrupted clock-store write keeps the old slot", False,
               str(e)[-50:])

    # ---------------- phase 5: soak / long duration -----------------------
    t0 = time.time()
    n = 0
    fails = 0
    while time.time() - t0 < soak:
        try:
            acc.raw("0x06", "0x01")
            acc.raw("0x04", "0x2d", "0x1a")
            n += 1
        except Exception:
            fails += 1
        time.sleep(1.0)
    heap = None
    try:
        h = acc.raw("0x32", "0x01")
        heap = h.hex()
    except Exception:
        pass
    record("5", "soak %d s" % soak, fails == 0,
           "%d polls, %d errors, heap=%s" % (n, fails, heap))


def record_fault(acc, phase, name, fn):
    try:
        fn()
        record(phase, name, False, "no error (unexpected)")
    except Exception as e:
        record(phase, name, True, str(e)[-60:])


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mch", default="192.168.1.41")
    ap.add_argument("--target", default="0x88")
    ap.add_argument("--slot", type=int, default=12)
    ap.add_argument("--bus", default="0")
    ap.add_argument("--soak", type=int, default=300)
    ap.add_argument("--report", default="")
    args = ap.parse_args()

    acc = FruAccess(args.mch, args.target, "0x82", args.bus, i2c_addr=0x54)
    run(acc, args.mch, args.slot, args.soak)

    ok = sum(1 for r in RESULTS if r[2])
    report = args.report or os.path.join(
        HERE, "..", "..", "docs", "testing",
        "TEST_REPORT-%s.md" % datetime.datetime.now().strftime("%Y%m%d-%H%M"))
    with open(report, "w") as f:
        f.write("# u4FCP/uFC board self-test — %s\n\n" % datetime.datetime.now())
        f.write("target %s via MCH %s (bridge 0x82)\n\n" % (args.target, args.mch))
        f.write("| phase | test | result | detail |\n|---|---|---|---|\n")
        for ph, name, okk, detail in RESULTS:
            f.write("| %s | %s | %s | %s |\n" %
                    (ph, name, "PASS" if okk else "FAIL", detail.replace("|", "\\|")))
        f.write("\n**%d/%d passed**\n" % (ok, len(RESULTS)))
    print("\n%d/%d passed -> %s" % (ok, len(RESULTS), os.path.normpath(report)))
    return 0 if ok == len(RESULTS) else 1


if __name__ == "__main__":
    sys.exit(main())
