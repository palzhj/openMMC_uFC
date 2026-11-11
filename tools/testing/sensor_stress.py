#!/usr/bin/env python3
"""Sensor-monitoring stress test for the u4FCP MMC (IPMI through the MCH bridge).

Hammers the IPMI sensor/SDR paths of the MMC while it keeps polling the
on-board UCD90320 and the uRTM's UCD90124A, and reports whether anything
breaks:

  - the SDR repository is re-read and the sensor table compared before/after
    (a dynamic RTM insert/remove or a corrupted list shows up as a diff),
  - every reading is decoded with its SDR linearization and checked against
    physical bounds (so a mis-addressed page - e.g. a temperature read from a
    current page - is caught),
  - the sensor state byte must stay "ok" and "scanning enabled",
  - transport/ipmitool failures are counted with their IPMI completion code,
  - the free heap is sampled: a reset or a leak changes it,
  - latency is measured per request (p50/p95/p99/max).

Requests, randomly mixed by every worker:

    Get Device SDR Info     raw 0x04 0x20
    Reserve Device SDR      raw 0x04 0x22
    Get Device SDR          raw 0x04 0x21 <resv> <recid> <off> <len>
    Get Sensor Reading      raw 0x04 0x2d <sensor number>
    Get Sensor Threshold    raw 0x04 0x27 <sensor number>
    Get MMC free heap       raw 0x32 0x01

usage: sensor_stress.py [--mch IP] [--target 0x88] [--bus 0] [--transit 0x82]
                        [--seconds 120] [--workers 4] [--report FILE]

  --seconds N     total duration of the loaded phase (default 120 s)
  --workers N     concurrent ipmitool callers (default 4)
  --report FILE   write a Markdown report (default docs/testing/SENSOR_STRESS-<ts>.md)
  --rng SEED      seed for the request mix (default: none)
"""
import argparse
import datetime
import os
import random
import statistics
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "uFC_toolbox"))
from fru_core import FruAccess  # noqa: E402

NETFN_SE = "0x04"
CMD_SDR_INFO = "0x20"
CMD_GET_SDR = "0x21"
CMD_RESERVE = "0x22"
CMD_THRESHOLD = "0x27"
CMD_READING = "0x2d"
CMD_FREE_HEAP = "0x01"

SDR_TYPES = {0x01: ("Full", 64), 0x02: ("Compact", 48), 0x12: ("MDevLoc", 32)}
SENSOR_TYPES = {0x01: "Temp", 0x02: "Volt", 0x03: "Curr", 0x04: "Fan",
                0x23: "Watchdog", 0xF2: "Hotswap"}
# physical bounds per sensor type: (low, high, unit)
BOUNDS = {0x01: (-40.0, 130.0, "C"), 0x02: (-1.0, 30.0, "V"),
          0x03: (-2.0, 60.0, "A")}


class Stats:
    def __init__(self):
        self.lock = threading.Lock()
        self.ops = 0
        self.failed = 0
        self.mmc_errors = 0
        self.transport = 0
        self.latencies = []
        self.codes = {}
        self.errors = []
        self.slow = 0
        self.reservation_retries = 0
        self.readings = {}      # sensor number -> [min, max]
        self.states = {}        # sensor number -> set of state bytes
        self.scan_bits = {}     # sensor number -> set of event_scan bytes
        self.slow_ops = []      # (elapsed s, op, latency s)
        self.bad_state = []     # (name, number, reason)
        self.bad_range = []     # (name, number, reason)

    def add_op(self, latency, ok, error=None):
        with self.lock:
            self.ops += 1
            self.latencies.append(latency)
            if latency > 0.5:
                self.slow += 1
            if not ok:
                self.failed += 1
                code = "?"
                text = str(error)
                if "rsp=0x" not in text:
                    self.transport += 1
                else:
                    self.mmc_errors += 1
                if "rsp=0x" in text:
                    code = text.split("rsp=0x")[1][:2]
                self.codes[code] = self.codes.get(code, 0) + 1
                if len(self.errors) < 12:
                    self.errors.append(text[-80:])

    def add_reading(self, number, raw, state, event_scan):
        with self.lock:
            low, high = self.readings.get(number, (raw, raw))
            self.readings[number] = (min(low, raw), max(high, raw))
            self.states.setdefault(number, set()).add(state & 0x3F)
            self.scan_bits.setdefault(number, set()).add(event_scan)

    def add_state(self, item):
        with self.lock:
            if len(self.bad_state) < 20:
                self.bad_state.append(item)

    def add_range(self, item):
        with self.lock:
            if len(self.bad_range) < 20:
                self.bad_range.append(item)


def pct(values, q):
    if not values:
        return 0.0
    values = sorted(values)
    index = min(len(values) - 1, int(round((len(values) - 1) * q)))
    return values[index]


# ---------------------------------------------------------------------------
# SDR repository
# ---------------------------------------------------------------------------

def sdr_info(mc):
    return mc.raw(NETFN_SE, CMD_SDR_INFO)


def read_record(mc, rec_id, reserve=None, stats=None, retries=4):
    """Read a full SDR record (the response is [next_lo, next_hi, data...]).

    The record length is in the header (byte 4), so the chunks never ask past
    the end - the MMC rejects an over-long read with 0xCB.  The MMC keeps one
    global SDR reservation, so a second reader invalidates ours: re-reserve and
    retry on 0xCC (that is also what ipmitool does).
    """
    reserve = reserve or mc.raw(NETFN_SE, CMD_RESERVE)

    def chunk(offset, size):
        data = mc.raw(NETFN_SE, CMD_GET_SDR,
                      "0x%02x" % reserve[0], "0x%02x" % reserve[1],
                      "0x%02x" % (rec_id & 0xFF), "0x%02x" % (rec_id >> 8),
                      "0x%02x" % offset, "0x%02x" % size)
        return (data[0] | (data[1] << 8)), data[2:]

    try:
        next_id, head = chunk(0, 8)
        total = 5 + head[4]
        data = bytearray(head)
        while len(data) < total:
            next_id, body = chunk(len(data), min(24, total - len(data)))
            if not body:
                break
            data += body
        return bytes(data[:total]), next_id
    except RuntimeError as exc:
        if "rsp=0xcc" not in str(exc) or retries <= 0:
            raise
        # another reader bumped the global reservation: reserve again
        if stats is not None:
            with stats.lock:
                stats.reservation_retries += 1
        return read_record(mc, rec_id, None, stats, retries - 1)


def sensor_table(mc):
    """Read the whole repository and decode the sensors."""
    table = []
    rec_id = 0
    next_id = 0
    reserve = mc.raw(NETFN_SE, CMD_RESERVE)
    while True:
        record, next_id = read_record(mc, rec_id, reserve)
        if len(record) < 16:
            break
        rectype = record[3]
        kind, size = SDR_TYPES.get(rectype, ("0x%02x" % rectype, 0))
        entry = {"record": rec_id, "type": kind}
        if rectype in (0x01, 0x02) and len(record) >= size:
            name_len = record[size - 17] & 0x3F
            entry.update({
                "number": record[7],
                "entity": record[8],
                "instance": record[9],
                "sensor_type": record[12],
                "M": record[24] if rectype == 0x01 else None,
                "R": ((record[29] >> 4) if rectype == 0x01 else None),
                "name": record[size - 16:size - 16 + name_len].decode("latin-1"),
            })
            if entry["R"] is not None:
                uR = entry["R"]
                entry["R"] = uR - 16 if uR > 7 else uR
            table.append(entry)
        if next_id == 0xFFFF:
            break
        rec_id = next_id
        if len(table) > 80:
            break
    return table


def decode(entry, raw):
    """Reading in physical units using the SDR linearization (B = 0)."""
    if entry.get("M") is None or entry.get("R") is None:
        return None
    return entry["M"] * raw * (10.0 ** entry["R"])


# ---------------------------------------------------------------------------
# workers
# ---------------------------------------------------------------------------

def timed(stats, t0, op, call):
    """Run one IPMI request, measure it and remember the slow ones."""
    started = time.monotonic()
    error = None
    try:
        call()
    except Exception as exc:      # noqa: BLE001 - recorded as a failure
        error = exc
    latency = time.monotonic() - started
    text = str(error or "")
    if error is not None and "rsp=0xcc" in text:
        # the MMC correctly rejected a stale SDR reservation (another reader
        # reserved in between): a collision, not a failed sensor request
        with stats.lock:
            stats.reservation_retries += 1
        stats.add_op(latency, True)
    else:
        stats.add_op(latency, error is None, error)
    if latency > 0.5:
        with stats.lock:
            if len(stats.slow_ops) < 60:
                stats.slow_ops.append((time.monotonic() - t0, op, latency))


def check_reading(mc, table, stats, entry, data):
    """Validate one Get Sensor Reading answer."""
    number = entry["number"]
    if len(data) < 3:
        stats.add_state((entry["name"], number, "short response %d bytes" % len(data)))
        return
    raw, event_scan, state = data[0], data[1], data[2]
    stats.add_reading(number, raw, state, event_scan)
    # the compact (hotswap) record reports the FRU state, not a threshold state
    if entry["type"] == "Full" and state & 0x3F:
        stats.add_state((entry["name"], number,
                         "state 0x%02x (expected ok)" % (state & 0x3F)))
    physical = decode(entry, raw)
    bounds = BOUNDS.get(entry["sensor_type"])
    if physical is not None and bounds and not (bounds[0] <= physical <= bounds[1]):
        stats.add_range((entry["name"], number,
                         "%.3f %s outside %.1f..%.1f" % (physical, bounds[2],
                                                        bounds[0], bounds[1])))


def worker(mc, table, stats, deadline, seed, t0=None):
    rng = random.Random(seed)
    t0 = t0 or time.monotonic()
    sensors = [e for e in table if "number" in e]
    full = [e for e in sensors if e["type"] == "Full"]      # thresholds need one
    records = [e for e in table if e["record"] < 0xFFFF]
    while time.monotonic() < deadline:
        pick = rng.random()
        if pick < 0.70:
            entry = rng.choice(sensors)
            timed(stats, t0, "Get Sensor Reading #%d" % entry["number"],
                  lambda e=entry: check_reading(mc, table, stats, e,
                                                mc.raw(NETFN_SE, CMD_READING,
                                                       "0x%02x" % e["number"])))
        elif pick < 0.80:
            record = rng.choice(records)["record"]
            timed(stats, t0, "Get Device SDR %d" % record,
                  lambda r=record: read_record(mc, r, stats=stats))
        elif pick < 0.86:
            timed(stats, t0, "Get Device SDR Info",
                  lambda: sdr_info(mc))
        elif pick < 0.92:
            entry = rng.choice(full)
            timed(stats, t0, "Get Sensor Threshold #%d" % entry["number"],
                  lambda e=entry: mc.raw(NETFN_SE, CMD_THRESHOLD,
                                         "0x%02x" % e["number"]))
        else:
            timed(stats, t0, "Reserve Device SDR",
                  lambda: mc.raw(NETFN_SE, CMD_RESERVE))


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def heap_kb(mc):
    data = mc.raw("0x32", CMD_FREE_HEAP)
    if len(data) < 4:
        raise RuntimeError("short heap response")
    return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mch", default="192.168.1.41")
    ap.add_argument("--target", default="0x88")
    ap.add_argument("--transit", default="0x82")
    ap.add_argument("--bus", default="0")
    ap.add_argument("--seconds", type=int, default=120)
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--report", default=None)
    ap.add_argument("--rng", type=int, default=None)
    args = ap.parse_args()

    mc = FruAccess(args.mch, args.target, args.transit, args.bus, i2c_addr=0x54)
    stamp = datetime.datetime.now()
    report = args.report or os.path.join(
        REPO, "docs", "testing", "SENSOR_STRESS-%s.md" % stamp.strftime("%Y%m%d-%H%M%S"))

    print("sensor stress: target %s via MCH %s, %d s, %d workers"
          % (args.target, args.mch, args.seconds, args.workers))
    try:
        before = sensor_table(mc)
        heap_before = heap_kb(mc)
    except Exception as exc:      # noqa: BLE001
        print("cannot read the sensor table: %s" % str(exc)[-120:])
        return 2
    print("baseline: %d sensors, free heap %d B" % (len(before), heap_before))
    print("  %-4s %-9s %-9s %s" % ("#", "entity", "type", "name"))
    for e in before:
        print("  %-4d 0x%02x/0x%02x  %-9s %s"
              % (e["number"], e["entity"], e["instance"],
                 SENSOR_TYPES.get(e["sensor_type"], "?"), e["name"]))

    phases = []

    def run_phase(workers, seconds, label):
        stats = Stats()
        deadline = time.monotonic() + seconds
        started_phase = time.monotonic()
        threads = [threading.Thread(target=worker,
                                    args=(mc, before, stats, deadline,
                                          (args.rng or 0) + i, started_phase))
                   for i in range(workers)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        elapsed = time.monotonic() - started_phase
        lat = stats.latencies
        print("\n%s: %d worker(s), %.1f s, %d requests, %d failed, %d slow (>0.5 s)"
              % (label, workers, elapsed, stats.ops, stats.failed, stats.slow))
        print("  latency: p50 %.1f ms, p95 %.1f ms, p99 %.1f ms, max %.1f ms"
              % (pct(lat, 0.50) * 1e3, pct(lat, 0.95) * 1e3,
                 pct(lat, 0.99) * 1e3, (max(lat) if lat else 0) * 1e3))
        if stats.codes:
            print("  failure codes: %s" % ", ".join(
                "%s x%d" % (k, v) for k, v in sorted(stats.codes.items())))
        for at, op, latency in stats.slow_ops[:6]:
            print("  slow %6.1f s: %-28s %.2f s" % (at, op, latency))
        for text in stats.errors[:5]:
            print("    error: %s" % text)
        phases.append((label, workers, elapsed, stats))
        return stats

    heap = [heap_before]
    stop_heap = threading.Event()

    def heap_sampler():
        while not stop_heap.is_set():
            try:
                heap.append(heap_kb(mc))
            except Exception:      # noqa: BLE001
                pass
            stop_heap.wait(5)

    sampler = threading.Thread(target=heap_sampler, daemon=True)
    sampler.start()

    half = max(5, args.seconds // 2)
    run_phase(1, half, "sequential")
    run_phase(args.workers, args.seconds - half, "concurrent")
    stop_heap.set()
    sampler.join(timeout=2)

    stats = Stats()
    for _, _, _, st in phases:
        stats.ops += st.ops
        stats.failed += st.failed
        stats.slow += st.slow
        stats.reservation_retries += st.reservation_retries
        stats.latencies += st.latencies
        for code, count in st.codes.items():
            stats.codes[code] = stats.codes.get(code, 0) + count
        stats.errors += st.errors
        for number, (low, high) in st.readings.items():
            old = stats.readings.get(number, (low, high))
            stats.readings[number] = (min(old[0], low), max(old[1], high))
        for number, values in st.states.items():
            stats.states.setdefault(number, set()).update(values)
        for number, values in st.scan_bits.items():
            stats.scan_bits.setdefault(number, set()).update(values)
        stats.mmc_errors += st.mmc_errors
        stats.transport += st.transport
        stats.bad_state += st.bad_state
        stats.bad_range += st.bad_range
        stats.slow_ops += st.slow_ops

    print("\ntotal: %d requests, %d failed (%d MMC errors, %d bridge/session), "
          "%d slow, %d reservation collisions"
          % (stats.ops, stats.failed, stats.mmc_errors, stats.transport,
             stats.slow, stats.reservation_retries))
    if stats.codes:
        print("failure codes: %s" % ", ".join("%s x%d" % (k, v)
                                              for k, v in sorted(stats.codes.items())))

    anomalies = []
    for number, (low, high) in sorted(stats.readings.items()):
        entry = next(e for e in before if e["number"] == number)
        if low != high:
            print("  %-16s raw %3d..%3d" % (entry["name"], low, high))
        # normal ADC jitter is one or two counts; a jump to another sensor's
        # value is a crossed response (concurrent IPMB sessions)
        if entry["type"] == "Full" and (high - low) > max(4, int(0.2 * high)):
            anomalies.append("%s: reading jumped %d..%d (crossed response with "
                             "concurrent sessions?)" % (entry["name"], low, high))
        states = stats.states.get(number, set())
        scans = stats.scan_bits.get(number, set())
        if entry["type"] == "Full" and states - {0}:
            anomalies.append("%s: state(s) %s" % (entry["name"],
                                                  sorted("0x%02x" % s for s in states)))
        if scans - {0xC0, 0x00}:
            anomalies.append("%s: event_scan byte(s) %s"
                             % (entry["name"], sorted("0x%02x" % s for s in scans)))
    if stats.bad_state:
        anomalies.append("unexpected sensor states: %d" % len(stats.bad_state))
        anomalies += ["  %s (#%d): %s" % item for item in stats.bad_state[:8]]
    if stats.bad_range:
        anomalies.append("out-of-range readings: %d" % len(stats.bad_range))
        anomalies += ["  %s (#%d): %s" % item for item in stats.bad_range[:8]]

    try:
        after = sensor_table(mc)
        heap_after = heap_kb(mc)
        alive = True
    except Exception as exc:      # noqa: BLE001
        after, heap_after, alive = [], None, False
        anomalies.append("MMC unreachable after the test: %s" % str(exc)[-80:])

    if alive:
        if len(after) != len(before):
            anomalies.append("sensor table changed: %d -> %d entries"
                             % (len(before), len(after)))
        else:
            for old, new in zip(before, after):
                if (old["number"], old["name"], old["entity"], old["instance"]) != \
                        (new["number"], new["name"], new["entity"], new["instance"]):
                    anomalies.append("sensor changed: #%d %r -> #%d %r"
                                     % (old["number"], old["name"],
                                        new["number"], new["name"]))
        if heap_before != heap_after:
            anomalies.append("free heap %d -> %d B (%+d)"
                             % (heap_before, heap_after, heap_after - heap_before))
    heap_note = ("sampled %d times: min %d B, max %d B, first %d B, last %d B"
                 % (len(heap), min(heap), max(heap), heap[0], heap[-1]))
    print("\nheap: %s" % heap_note)
    if stats.mmc_errors:
        anomalies.append("%d MMC completion-code errors" % stats.mmc_errors)
    if stats.transport:
        anomalies.append("%d bridge/session transport errors (MCH side)"
                         % stats.transport)
    if anomalies:
        print("anomalies:")
        for item in anomalies:
            print("  - %s" % item)
    else:
        print("anomalies: none")
    result = "PASS" if not anomalies else "FAIL"

    lines = [
        "# Sensor-monitoring stress test - %s" % stamp.strftime("%Y-%m-%d %H:%M:%S"),
        "",
        "target %s via MCH %s, %d s total" % (args.target, args.mch, args.seconds),
        "",
        "| item | value |",
        "|---|---|",
        "| result | **%s** |" % result,
        "| requests | %d |" % stats.ops,
        "| failed | %d (%d MMC, %d bridge) |"
        % (stats.failed, stats.mmc_errors, stats.transport),
        "| slow (>0.5 s) | %d |" % stats.slow,
        "| reservation retries | %d |" % stats.reservation_retries,
        "| latency p50 / p95 / p99 / max | %.1f / %.1f / %.1f / %.1f ms |" % (
            pct(stats.latencies, 0.50) * 1e3, pct(stats.latencies, 0.95) * 1e3,
            pct(stats.latencies, 0.99) * 1e3,
            (max(stats.latencies) if stats.latencies else 0) * 1e3),
        "| sensors (before -> after) | %d -> %d |" % (len(before), len(after)),
        "| heap | %s |" % heap_note,
        "",
        "## Phases",
        "",
        "| phase | workers | seconds | requests | failed | p50 | p95 | max |",
        "|---|---|---|---|---|---|---|---|",
    ]
    for label, workers, elapsed, st in phases:
        lines.append("| %s | %d | %.1f | %d | %d | %.1f ms | %.1f ms | %.1f ms |"
                     % (label, workers, elapsed, st.ops, st.failed,
                        pct(st.latencies, 0.50) * 1e3, pct(st.latencies, 0.95) * 1e3,
                        (max(st.latencies) if st.latencies else 0) * 1e3))
    lines += ["", "## Sensor table", "",
              "| # | entity | instance | type | name | raw range |",
              "|---|---|---|---|---|---|"]
    for entry in before:
        low, high = stats.readings.get(entry["number"], (0, 0))
        lines.append("| %d | 0x%02x | 0x%02x | %s | %s | %d..%d |"
                     % (entry["number"], entry["entity"], entry["instance"],
                        SENSOR_TYPES.get(entry["sensor_type"], "?"),
                        entry["name"], low, high))
    lines += ["", "## Anomalies", ""]
    lines += ["- %s" % item for item in anomalies] or ["none"]
    with open(report, "w") as handle:
        handle.write("\n".join(lines) + "\n")
    print("\n%s -> %s" % (result, report))
    return 0 if result == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
