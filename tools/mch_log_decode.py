#!/usr/bin/env python3
"""Decode the NAT-MCH console monitor log.

The MCH prints housekeeping traffic on its console/serial port in three shapes:

    SEL: 1970/01/02 19:36:40 alive
    ipmiMsgSender(51): REQ(I2C=0xc4) failed on bus 13, result -9
    ipmiMsgSender(61): REQ(I2C=0x16) failed on bus 17 - no ACK
    R(61,17,1,36)

`ipmiMsgSender(<n>)` is the firmware source line in the MCH, `I2C=` is the
8-bit I2C target address, and `bus` is one of the MCH's own I2C bus ids.  The
target addresses are decoded with the NAT-MCH User's Manual v1.41:

    Table 43  hub/clock modules : site 1 clock 0x14, hub 0x16
                                  site 2 clock 0x18, hub 0x1a
    Table 44  FRU devices       : AMC n = 0x70 + 2n, CU1/2 = 0xa8/0xaa,
                                  PM1..4 = 0xc2/0xc4/0xc6/0xc8

Not documented in that manual (so only reported, never invented here): the
bus-id numbering, the numeric `result -N` codes, and the `R(...)` fields.

A SEL timestamp at 1970 means the MCH clock was never set (no NTP, no rdate):
every entry then reads 1970/01/01 + uptime, so this tool converts the stamp to
seconds-since-epoch and reports the spacing between entries instead of trusting
the date.

usage:
    mch_log_decode.py LOG            # decode a captured console log
    mch_log_decode.py -              # decode stdin (paste, then ^D)
    mch_log_decode.py --demo         # the sample from the 2026-11-13 session

    --json          machine-readable output instead of the text report
    --no-summary    only the per-line blocks
"""
import argparse
import datetime
import json
import re
import sys

EPOCH = datetime.datetime(1970, 1, 1)

# NAT-MCH User's Manual v1.41, Appendix B 1 (Tables 43/44).  8-bit addresses,
# i.e. what the firmware puts on the wire; the 7-bit form is shown for reference.
TARGETS = {
    0x14: "Clock Module, MCH site 1",
    0x16: "Hub Module, MCH site 1",
    0x18: "Clock Module, MCH site 2",
    0x1A: "Hub Module, MCH site 2",
    0xA4: "backplane FRU EEPROM (UM Appendix D)",
    0xA8: "Cooling Unit 1",
    0xAA: "Cooling Unit 2",
    0xC2: "Power Module 1",
    0xC4: "Power Module 2",
    0xC6: "Power Module 3",
    0xC8: "Power Module 4",
}
for _n in range(1, 13):
    TARGETS[0x70 + 2 * _n] = f"AMC{_n} MMC"

# The manual lists no code table.  -9 shows up on REQ, -8 on RSP in other
# NAT-MCH logs; both are driver-level failures, so the distinction the firmware
# makes is: numeric result == transport error, "no ACK" == nobody answered.
RESULT_HINT = {
    -9: "driver-level I2C error: timeout or busy bus",
    -8: "driver-level I2C error: seen on RSP lines in other NAT-MCH logs",
}

RE_SEL = re.compile(r"^SEL:\s*(\d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2})\s*(.*)$")
RE_MSG = re.compile(
    r"^ipmiMsgSender\((\d+)\):\s*(REQ|RSP)\(I2C=(0x[0-9a-fA-F]+)\)\s+failed"
    r"\s+on\s+bus\s+(\d+)\s*(?:(?:,\s*|\s+)result\s+(-?\d+)|-\s*no\s+ACK)\s*$",
    re.IGNORECASE,
)
RE_RETRY = re.compile(r"^R\(([^)]*)\)")


def parse_sel(stamp):
    t = datetime.datetime.strptime(stamp, "%Y/%m/%d %H:%M:%S")
    return t, int((t - EPOCH).total_seconds())


def decode(line):
    """Return a dict describing one console line."""
    line = line.rstrip("\r\n")
    if not line.strip():
        return None

    m = RE_SEL.match(line)
    if m:
        when, since = parse_sel(m.group(1))
        return {
            "kind": "sel",
            "raw": line,
            "time": when.isoformat(sep=" "),
            "since_epoch": since,
            "clock_unset": when.year < 2000,
            "text": m.group(2).strip(),
        }

    m = RE_MSG.match(line)
    if m:
        src, qtype, addr, bus = int(m.group(1)), m.group(2).upper(), int(m.group(3), 16), int(m.group(4))
        result = m.group(5)
        dev = TARGETS.get(addr)
        return {
            "kind": "i2c-fail",
            "raw": line,
            "sender_line": src,
            "type": qtype,
            "addr8": addr,
            "addr7": addr >> 1,
            "target": dev or "unknown target (not in UM v1.41 Tables 43/44)",
            "target_known": dev is not None,
            "bus": bus,
            "result": None if result is None else int(result),
            "no_ack": result is None,
        }

    m = RE_RETRY.match(line)
    if m:
        fields = [f.strip() for f in m.group(1).split(",")]
        return {
            "kind": "retry",
            "raw": line,
            "fields": fields,
            "tail": line[m.end():].strip(),
        }

    return {"kind": "other", "raw": line}


def describe(rec):
    """Text block for one decoded line."""
    k = rec["kind"]
    if k == "sel":
        note = "  (clock unset: value is seconds since 1970-01-01)" if rec["clock_unset"] else ""
        return [f"SEL      {rec['time']}  t+{rec['since_epoch']} s  '{rec['text']}'{note}"]
    if k == "i2c-fail":
        if rec["no_ack"]:
            why = "no ACK (no device answered)"
        else:
            why = f"result {rec['result']} ({RESULT_HINT.get(rec['result'], 'undocumented code')})"
        return [
            f"I2C FAIL {rec['type']} from ipmiMsgSender:{rec['sender_line']}",
            f"         target 0x{rec['addr8']:02x} (8-bit) = 0x{rec['addr7']:02x} (7-bit) -> {rec['target']}",
            f"         bus {rec['bus']}  {why}",
        ]
    if k == "retry":
        return [
            f"RETRY    R({','.join(rec['fields'])})"
            "  [fields not documented; same sender line/bus as the failure above]",
        ]
    return [f"OTHER    {rec['raw']}"]


def summarise(recs):
    out = []
    failures = [r for r in recs if r["kind"] == "i2c-fail"]
    if failures:
        out.append("I2C failures by target:")
        seen = {}
        for r in failures:
            key = (r["addr8"], r["bus"], r["sender_line"])
            seen.setdefault(key, []).append(r)
        for (addr, bus, src), rs in seen.items():
            how = "no ACK" if rs[0]["no_ack"] else f"result {rs[0]['result']}"
            out.append(
                f"  x{len(rs):<3d} 0x{addr:02x} {TARGETS.get(addr, "unknown"):<44s}"
                f" bus {bus:<3d} sender:{src:<3d} {how}"
            )

    stamps = [r["since_epoch"] for r in recs if r["kind"] == "sel"]
    if len(stamps) >= 2:
        deltas = [b - a for a, b in zip(stamps, stamps[1:])]
        out.append(
            "SEL heartbeats: "
            f"{len(stamps)} entries, t+{stamps[0]} .. t+{stamps[-1]} s, "
            f"spacings {', '.join(str(d) for d in deltas[:8])} s"
            + (f" (period {deltas[0]} s)" if len(set(deltas)) == 1 else "")
        )
        if all(s % 1000 == 0 for s in stamps):
            out.append("  all stamps land on whole 1000 s steps -> periodic heartbeat, not events")
    clock_unset = any(r.get("clock_unset") for r in recs)
    if clock_unset:
        out.append(
            "MCH clock is unset (1970): set it with `rdate <server>`, the NTP menu,"
            " or `ipmitool -I lan -H <mch> -A none sel time set \"now\"`;"
            " the NAT-MCH SEL is RAM-only and is lost on an MCH reboot."
        )
    out.append("Note: bus ids, `result -N` codes and R(...) fields are not in UM v1.41.")
    return out


DEMO = """\
SEL: 1970/01/02 19:36:40 alive
ipmiMsgSender(51): REQ(I2C=0xc4) failed on bus 13, result -9
ipmiMsgSender(61): REQ(I2C=0x16) failed on bus 17 - no ACK
R(61,17,1,36)SEL: 1970/01/02 19:53:20 alive
"""


def main():
    ap = argparse.ArgumentParser(description="Decode NAT-MCH console monitor lines.")
    ap.add_argument("log", nargs="?", help="log file, or '-' for stdin")
    ap.add_argument("--demo", action="store_true", help="decode the built-in sample")
    ap.add_argument("--json", action="store_true", help="emit JSON")
    ap.add_argument("--no-summary", action="store_true")
    args = ap.parse_args()

    if args.demo:
        text = DEMO
    elif args.log == "-":
        text = sys.stdin.read()
    elif args.log:
        with open(args.log, encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    else:
        ap.error("give a log file (or '-' / --demo)")

    # A log line can carry a trailing fragment of the previous one ("R(61,...)SEL: ...").
    recs = []
    for line in text.splitlines():
        rec = decode(line)
        if rec is None:
            continue
        if rec["kind"] == "retry" and "SEL:" in rec["tail"]:
            i = rec["tail"].index("SEL:")
            recs.append(rec)
            rec = decode(rec["tail"][i:])
        recs.append(rec)

    if args.json:
        print(json.dumps([r for r in recs if r], indent=2))
        return 0

    for rec in recs:
        for ln in describe(rec):
            print(ln)
    if not args.no_summary:
        print()
        for ln in summarise(recs):
            print(ln)
    return 0


if __name__ == "__main__":
    sys.exit(main())
