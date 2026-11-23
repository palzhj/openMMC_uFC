#!/usr/bin/env python3
"""Watch the u4FCP-v2 RTM state while a module is pulled/inserted by hand.

Prints one line per change of the things that matter for the physical
mount/remove test:

  - RTM_PS# (PA8) via the RTM +12V rail current coming and going
  - the number of SDR records the MMC serves and how many RTM rows are live
  - the manager state, if the symbols are known (SWD, optional)

usage: rtm_watch.py [--mch IP] [--target 0x7c] [--seconds 300]

The MMC is the authority: the MCH keeps a cached copy of the SDR repository and
only re-reads it on a FRU activation (shutdown <fru> + fru_start <fru>).
"""
import argparse
import subprocess
import sys
import time

RTM_ROWS = ("RTM TEMP UCD", "FMC2", "FMC3")


def ipmi(mch, target, *args, tries=3):
    cmd = ["ipmitool", "-I", "lan", "-H", mch, "-A", "none", "-T", "0x82",
           "-B", "0", "-t", target, "-b", "0"] + list(args)
    for _ in range(tries):
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=25)
        if p.returncode == 0:
            return p.stdout
        time.sleep(0.4)
    return ""


def snapshot(mch, target):
    out = ipmi(mch, target, "sensor", "list")
    total = 0
    live = 0
    rtm_ok = []
    rtm_all = 0
    cur = None
    for line in out.splitlines():
        f = [x.strip() for x in line.split("|")]
        if len(f) < 4:
            continue
        name = f[0]
        if name == "HOTSWAP AMC":
            continue
        total += 1
        if name == "RTM +12V Curr":
            cur = f[1]
        if any(k in name for k in RTM_ROWS):
            rtm_all += 1
            if f[3] == "ok":
                live += 1
                rtm_ok.append(name)
    return total, cur, rtm_all, live


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mch", default="192.168.1.41")
    ap.add_argument("--target", default="0x7c")
    ap.add_argument("--seconds", type=int, default=300)
    a = ap.parse_args()

    t0 = time.time()
    prev = None
    print("watching %s via %s for %d s - pull / insert the RTM now"
          % (a.target, a.mch, a.seconds))
    while time.time() - t0 < a.seconds:
        try:
            total, cur, rtm_all, live = snapshot(a.mch, a.target)
        except subprocess.TimeoutExpired:
            continue
        key = (total, cur, rtm_all, live)
        if key != prev:
            print("[%6.1f] SDRs=%d  RTM +12V Curr=%s  RTM rows=%d (%d live)"
                  % (time.time() - t0, total, cur, rtm_all, live))
            sys.stdout.flush()
            prev = key
        time.sleep(0.5)


if __name__ == "__main__":
    main()
