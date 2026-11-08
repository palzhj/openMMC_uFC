#!/usr/bin/env python3
"""Loader / validator for ufc-clocking.json files.

The file describes clocking points (device, name, pin, direction) and the
connection relationships between them, plus the mutable 8V54816A routing.

Usage:
    clocking.py check  ufc-clocking.json
    clocking.py matrix ufc-clocking.json
    clocking.py dot    ufc-clocking.json   > clocking.dot
"""
import json
import sys

DIRS = ("in", "out", "inout")
MUX_DEVICE = "mux"


def load(path):
    with open(path) as f:
        return json.load(f)


def validate(doc):
    """Return (errors, warnings)."""
    errors, warnings = [], []
    points = {}
    devices = doc.get("devices", {})
    boards = doc.get("boards", {})
    for did, dev in devices.items():
        if dev.get("parent") not in boards and dev.get("parent") not in devices:
            errors.append("device %s: unknown parent %r" % (did, dev.get("parent")))
        if dev.get("box") and len(dev["box"]) != 4:
            errors.append("device %s: bad box %r" % (did, dev["box"]))
    for p in doc.get("points", []):
        pid = p.get("id")
        if not pid:
            errors.append("point without id: %r" % (p,))
            continue
        if pid in points:
            errors.append("duplicate point id %s" % pid)
        points[pid] = p
        if p.get("dir") not in DIRS:
            errors.append("%s: bad dir %r" % (pid, p.get("dir")))
        if p.get("parent") not in devices:
            errors.append("%s: unknown parent device %r" % (pid, p.get("parent")))
        cap = p.get("capability")
        if cap is not None:
            for key in ("in", "out", "tristate"):
                if not isinstance(cap.get(key), bool):
                    errors.append("%s: capability.%s must be bool" % (pid, key))
            d = p.get("dir")
            if d == "in" and not cap.get("in"):
                errors.append("%s: dir in but capability.in is false" % pid)
            if d == "out" and not cap.get("out"):
                errors.append("%s: dir out but capability.out is false" % pid)
            if d == "inout" and not (cap.get("in") and cap.get("out")):
                errors.append("%s: dir inout needs capability.in and .out" % pid)
            if cap.get("default") and cap["default"] not in ("in", "out", "tristate"):
                errors.append("%s: bad capability.default %r" % (pid, cap["default"]))

    for link in doc.get("links", []):
        a, b = link.get("a"), link.get("b")
        for pid in (a, b):
            if pid not in points:
                errors.append("link endpoint %r does not exist" % (pid,))
        if a in points and b in points:
            da, db = points[a]["dir"], points[b]["dir"]
            if da == db:
                if da == "out":
                    warnings.append("link %s-%s: both ends are outputs" % (a, b))
                elif da == "in":
                    warnings.append("link %s-%s: no driver (both inputs)" % (a, b))
        if link.get("verified") is False:
            warnings.append("unverified link %s-%s" % (a, b))

    mux_ids = {did for did, dev in devices.items()
               if did.upper() == "MUX" or str(dev.get("part", "")).startswith("8V54816A")}
    mux_ports = [pid for pid, p in points.items() if p.get("parent") in mux_ids]
    for pid, route in doc.get("routing", {}).items():
        if pid not in mux_ports:
            errors.append("routing: %s is not a mux port" % pid)
        src = route.get("source")
        if src is not None and src not in mux_ports:
            errors.append("routing: %s source %r is not a mux port" % (pid, src))
    return errors, warnings


def link_direction(points, link):
    """Return 'a2b', 'b2a', 'both' or None, derived from the points' dir."""
    a, b = points.get(link["a"]), points.get(link["b"])
    if not a or not b:
        return None
    da, db = a["dir"], b["dir"]
    if da == "out" and db == "in":
        return "a2b"
    if da == "in" and db == "out":
        return "b2a"
    if da == "inout" or db == "inout":
        return "both"
    return None


def groups(doc):
    """Chips of a board with their points.

    Hierarchy: board -> chip/connector/oscillator -> port (point).
    """
    boards = doc.get("boards", {})
    devices = doc.get("devices", {})
    result = []
    for bid in boards:
        chips = [did for did, dev in devices.items() if dev.get("parent") == bid]
        chips.sort(key=lambda d: (devices[d].get("box", [0, 0, 0, 0])[1],
                                  devices[d].get("box", [0, 0, 0, 0])[0], d))
        entry = {"board": bid, "label": boards[bid].get("label", bid), "chips": []}
        for did in chips:
            dev = devices[did]
            pts = [p for p in doc.get("points", []) if p.get("parent") == did]
            entry["chips"].append({"id": did, "label": dev.get("label", did),
                                   "part": dev.get("part"), "class": dev.get("class"),
                                   "points": pts})
        result.append(entry)
    return result


def matrix(doc):
    """Return the 16-byte 8V54816A configuration for the routing section."""
    data = bytearray(16)
    for port in range(16):
        pid = next((k for k in doc.get("routing", {})
                    if k.split(".")[-1].upper() == "CLK%d" % port), None)
        route = doc.get("routing", {}).get(pid) if pid else None
        if port < 9:
            data[port] = 0
            continue
        if not route or not route.get("source"):
            data[port] = 0
            continue
        src = int(route["source"].split("CLK")[-1])
        byte = 0x80 | (src & 0x0F) | (0x40 if route.get("term") else 0)
        if not route.get("inverted", False):
            byte |= 0x20
        data[port] = byte
    return bytes(data)


def dot(doc):
    """Render the file as a Graphviz diagram, one cluster per parent."""
    points = {p["id"]: p for p in doc["points"]}
    devices = doc.get("devices", {})
    mux_ids = {did for did, dev in devices.items()
               if did.upper() == "MUX" or str(dev.get("part", "")).startswith("8V54816A")}
    out = ["digraph clocking {", '  rankdir=LR;',
           '  node [shape=box, style="rounded,filled", fillcolor="#FFFFFF",',
           '        color="#E4DED2", fontname="Helvetica", fontsize=8];',
           '  edge [fontname="Helvetica", fontsize=7];']
    for board in groups(doc):
        out.append('  subgraph "cluster_board_%s" { label="board %s"; fontsize=10; color="#EDEAE2";'
                   % (board["board"], board["label"]))
        for chip in board["chips"]:
            out.append('    subgraph "cluster_%s" { label="%s"; fontsize=9; color="#FAF9F5";'
                       % (chip["id"], chip["label"]))
            for p in chip["points"]:
                pin = p.get("pin")
                label = "%s %s" % (p["name"], p["dir"])
                if pin:
                    label += "\\npin %s" % pin
                shape = ", shape=note" if p.get("parent") in mux_ids else ""
                out.append('      "%s" [label="%s"%s];' % (p["id"], label, shape))
            out.append("    }")
        out.append("  }")
    for link in doc.get("links", []):
        a, b = link["a"], link["b"]
        way = link_direction(points, link)
        if way == "b2a":
            a, b = b, a
        color = "#0E6B5E" if way == "both" else "#8A8A80"
        style = ", dir=both" if way == "both" else \
                ", arrowhead=none" if way is None else ""
        dashed = ", style=dashed" if link.get("verified") is False else ""
        out.append('  "%s" -> "%s" [color="%s"%s%s];' % (a, b, color, style, dashed))
    for pid, route in doc.get("routing", {}).items():
        if route.get("source"):
            out.append('  "%s" -> "%s" [label="routing", color="#B07A3A", penwidth=2];'
                       % (route["source"], pid))
    out.append("}")
    return "\n".join(out) + "\n"


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    cmd, path = sys.argv[1], sys.argv[2]
    doc = load(path)
    if cmd == "check":
        errors, warnings = validate(doc)
        for w in warnings:
            print("warning:", w)
        for e in errors:
            print("error:  ", e)
        print("%d points, %d links, %d errors, %d warnings"
              % (len(doc.get("points", [])), len(doc.get("links", [])), len(errors), len(warnings)))
        return 1 if errors else 0
    if cmd == "matrix":
        print(" ".join("%02x" % b for b in matrix(doc)))
        return 0
    if cmd == "dot":
        sys.stdout.write(dot(doc))
        return 0
    if cmd == "tree":
        for board in groups(doc):
            print("board %s — %s" % (board["board"], board["label"]))
            for chip in board["chips"]:
                print("  %-8s %-22s %-10s %d ports"
                      % (chip["id"], chip["label"] or chip["part"] or "",
                         chip["class"] or "", len(chip["points"])))
                for p in chip["points"]:
                    cap = p.get("capability") or {}
                    ways = "/".join(k for k in ("in", "out", "tristate") if cap.get(k))
                    print("      %-22s %-5s %s" % (p["name"], p["dir"], ways))
        return 0
    print("unknown command", cmd)
    return 2


if __name__ == "__main__":
    sys.exit(main())
