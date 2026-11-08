#!/usr/bin/env python3
"""
fru_gui.py -- web GUI to read and update the uFC FRU EEPROM over IPMI.

The MMC exposes raw FRU EEPROM access through the custom NetFn 0x32 commands
0x12 (write) / 0x13 (read).  This tool reads the 256-byte FRU image through a
NAT-MCH (double-bridged) and lets you edit the board/product fields in a
browser, then writes only the two changed areas back and verifies them.

Requires the firmware to be built with -DEEPROM_ACCESS=ON.

Usage:
    python3 fru_gui.py [--mch 192.168.1.41] [--target 0x7e] [--port 8099]

Open http://127.0.0.1:8099 in a browser.
"""

import argparse
import json
import os
import subprocess
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

# ---------------------------------------------------------------------------
# FRU helpers
# ---------------------------------------------------------------------------

EEPROM_SIZE = 256
FIELD_NAMES_BOARD = ["manufacturer", "product_name", "serial", "part_number", "fru_file_id"]
FIELD_NAMES_PRODUCT = ["manufacturer", "product_name", "part_number", "version",
                       "serial", "asset_tag", "fru_file_id"]


def ipmitool_cmd():
    """Use a bundled ipmitool next to the program when present."""
    name = "ipmitool.exe" if os.name == "nt" else "ipmitool"
    try:
        base = os.path.dirname(os.path.abspath(sys.argv[0]))
    except Exception:
        base = os.getcwd()
    local = os.path.join(base, name)
    return local if os.path.isfile(local) else "ipmitool"


class FruAccess:
    def __init__(self, mch, target, transit, bus):
        self.mch = mch
        self.bridge = ["-I", "lan", "-H", mch, "-A", "none",
                       "-T", transit, "-B", "0", "-t", target, "-b", bus]

    def _ipmi(self, *args):
        out = subprocess.run([ipmitool_cmd()] + self.bridge + ["raw"] + list(args),
                             capture_output=True, text=True, timeout=20)
        if out.returncode != 0:
            raise RuntimeError((out.stderr or out.stdout).strip() or "ipmitool failed")
        return bytes(int(x, 16) for x in out.stdout.split())

    def read(self, offset, length):
        data = bytearray()
        while length:
            n = min(31, length)  # 0x13 payload: completion code + data <= 32
            data += self._ipmi("0x32", "0x13", str(offset & 0xFF),
                               str((offset >> 8) & 0xFF), str(n))
            offset += n
            length -= n
        return bytes(data)

    def write(self, offset, data):
        while data:
            # The MMC slave RX accepts 6 header + data_len <= 31 bytes, so a
            # request carries at most 25 data bytes: 2 address bytes + 23 payload.
            n = min(23, len(data))
            chunk = data[:n]
            self._ipmi("0x32", "0x12", str(offset & 0xFF), str((offset >> 8) & 0xFF),
                       *["0x%02x" % b for b in chunk])
            offset += n
            data = data[n:]

    def dump(self):
        return self.read(0, EEPROM_SIZE)


def _checksum(area):
    return (-sum(area)) & 0xFF


def parse_fru(raw):
    """Return a dict with the board/product fields and their byte ranges."""
    if len(raw) < 8 or raw[0] != 0x01:
        raise ValueError("not a FRU image (bad format byte)")
    board_off = raw[3] * 8
    product_off = raw[4] * 8
    multi_off = raw[5] * 8

    result = {"raw": raw, "board_off": board_off, "product_off": product_off,
              "multi_off": multi_off, "board": {}, "product": {}}

    def area_fields(area_off, names, is_board):
        if area_off == 0:
            return None
        area = raw[area_off:]
        if area[0] not in (0x01, 0x02):
            raise ValueError("bad area type at 0x%02x" % area_off)
        length = area[1] * 8
        if length == 0 or len(area) < length:
            raise ValueError("bad area length at 0x%02x" % area_off)
        area = area[:length]
        fields = []
        pos = 3
        if is_board:             # board area has the 3-byte manufacturing time
            mfg_time = area[3] | (area[4] << 8) | (area[5] << 16)
            result["board"]["mfg_time"] = mfg_time
            pos = 6
        for name in names:
            if pos >= length - 1:
                break
            hdr = area[pos]
            n = hdr & 0x3F
            data = area[pos + 1: pos + 1 + n]
            text = data.rstrip(b"\x00").decode("latin-1")
            fields.append({"name": name, "offset": area_off + pos,
                           "len": n, "value": text})
            pos += 1 + n
        return {"offset": area_off, "length": length,
                "lang": area[2], "fields": fields,
                "checksum": area[-1]}

    result["board_area"] = area_fields(board_off, FIELD_NAMES_BOARD, True)
    result["product_area"] = area_fields(product_off, FIELD_NAMES_PRODUCT, False)
    return result


def build_areas(raw, parsed, values):
    """Apply the new string values in place and return the two updated areas."""
    updated = {}
    for key, area_key in (("board", "board_area"), ("product", "product_area")):
        area_info = parsed[area_key]
        if area_info is None:
            continue
        area = bytearray(raw[area_info["offset"]: area_info["offset"] + area_info["length"]])
        vals = values.get(key, {})
        for field in area_info["fields"]:
            name = field["name"]
            if name not in vals:
                continue
            text = vals[name]
            try:
                data = text.encode("latin-1")
            except UnicodeEncodeError:
                raise ValueError("%s: only latin-1 characters are allowed" % name)
            cap = field["len"]
            if len(data) + 1 > cap:
                raise ValueError("%s: max %d characters (got %d)" % (name, cap - 1, len(data)))
            rel = field["offset"] - area_info["offset"]
            area[rel + 1: rel + 1 + cap] = data + b"\x00" * (cap - len(data))
        if key == "board" and "mfg_time" in vals:
            t = int(vals["mfg_time"])
            if t < 0 or t > 0xFFFFFF:
                raise ValueError("mfg_time out of range")
            area[3], area[4], area[5] = t & 0xFF, (t >> 8) & 0xFF, (t >> 16) & 0xFF
        area[-1] = 0
        area[-1] = _checksum(area)
        updated[key] = (area_info["offset"], bytes(area))
    return updated


# ---------------------------------------------------------------------------
# HTTP server
# ---------------------------------------------------------------------------

PAGE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>uFC FRU editor</title>
<style>
 body { font-family: system-ui, sans-serif; margin: 2rem; max-width: 900px; }
 h1 { font-size: 1.3rem; }
 fieldset { border: 1px solid #ccc; border-radius: 6px; margin-bottom: 1rem; padding: .8rem 1rem; }
 legend { font-weight: 600; }
 label { display: inline-block; width: 11rem; }
 input[type=text], input[type=number] { width: 22rem; font-family: monospace; }
 .hint { color: #777; font-size: .85rem; }
 button { margin-right: .5rem; padding: .35rem .9rem; }
 #status { white-space: pre-wrap; background: #f6f6f6; padding: .6rem; border-radius: 4px;
           font-family: monospace; font-size: .85rem; min-height: 2rem; }
 #hex { font-family: monospace; font-size: .8rem; white-space: pre; }
</style>
</head>
<body>
<h1>uFC FRU EEPROM editor</h1>
<p class="hint">Reads/writes the 24AA025E48 FRU EEPROM through the MMC
(custom NetFn 0x32, cmds 0x12/0x13). Writing changes the live FRU &mdash;
download a backup first.</p>

<fieldset>
  <legend>Board info area</legend>
  <div id="board-fields"></div>
</fieldset>
<fieldset>
  <legend>Product info area</legend>
  <div id="product-fields"></div>
</fieldset>

<button onclick="load()">Read from MMC</button>
<button onclick="save()">Write to MMC</button>
<button onclick="backup()">Download backup (.bin)</button>

<div id="status"></div>
<fieldset><legend>Raw EEPROM</legend><div id="hex"></div></fieldset>

<script>
let state = null;

function fieldRow(name, cap, value, isTime) {
  return `<div><label>${name}</label>` +
    `<input data-name="${name}" type="${isTime ? 'number' : 'text'}" ` +
    `value="${value}" maxlength="${cap}"> <span class="hint">max ${cap}</span></div>`;
}

async function api(path, opts) {
  const r = await fetch(path, opts);
  const j = await r.json();
  if (!r.ok) throw new Error(j.error || r.statusText);
  return j;
}

async function load() {
  try {
    const j = await api('/api/read');
    state = j;
    const b = j.board_area, p = j.product_area;
    let html = '';
    if (b) {
      html += fieldRow('mfg_time', 8, b.board_mfg_time, true);
      for (const f of b.fields) html += fieldRow(f.name, f.len - 1, f.value, false);
    }
    document.getElementById('board-fields').innerHTML = html;
    html = '';
    if (p) for (const f of p.fields) html += fieldRow(f.name, f.len - 1, f.value, false);
    document.getElementById('product-fields').innerHTML = html;
    document.getElementById('hex').textContent = j.hex;
    setStatus('Read ' + j.raw_len + ' bytes; FRU parsed OK.');
  } catch (e) { setStatus('ERROR: ' + e.message); }
}

async function save() {
  if (!confirm('Write the edited fields to the FRU EEPROM?')) return;
  const vals = {board: {}, product: {}};
  document.querySelectorAll('#board-fields input').forEach(i => vals.board[i.dataset.name] = i.value);
  document.querySelectorAll('#product-fields input').forEach(i => vals.product[i.dataset.name] = i.value);
  try {
    const j = await api('/api/write', {method: 'POST', headers: {'Content-Type': 'application/json'},
                                       body: JSON.stringify(vals)});
    setStatus(j.message);
    await load();
  } catch (e) { setStatus('ERROR: ' + e.message); }
}

function backup() {
  const blob = new Blob([Uint8Array.from(state.raw)], {type: 'application/octet-stream'});
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = 'fru-backup.bin';
  a.click();
}

function setStatus(t) { document.getElementById('status').textContent = t; }
load();
</script>
</body>
</html>
"""


class Handler(BaseHTTPRequestHandler):
    access = None

    def log_message(self, fmt, *args):
        pass

    def _send(self, code, body, ctype="application/json"):
        if isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _json(self, obj, code=200):
        self._send(code, json.dumps(obj))

    def do_GET(self):
        path = urlparse(self.path).path
        try:
            if path == "/":
                self._send(200, PAGE, "text/html; charset=utf-8")
            elif path == "/api/read":
                raw = self.access.dump()
                parsed = parse_fru(raw)
                board = parsed["board_area"]
                product = parsed["product_area"]
                resp = {
                    "raw_len": len(raw),
                    "raw": list(raw),
                    "hex": "\n".join(" ".join("%02x" % b for b in raw[i:i + 16])
                                     for i in range(0, len(raw), 16)),
                    "board_area": None,
                    "product_area": None,
                }
                if board:
                    resp["board_area"] = {
                        "board_mfg_time": board_mfg_time(raw),
                        "fields": [{"name": f["name"], "len": f["len"], "value": f["value"]}
                                   for f in board["fields"]],
                    }
                if product:
                    resp["product_area"] = {
                        "fields": [{"name": f["name"], "len": f["len"], "value": f["value"]}
                                   for f in product["fields"]],
                    }
                self._json(resp)
            elif path == "/api/backup":
                self._send(200, self.access.dump(), "application/octet-stream")
            else:
                self._json({"error": "not found"}, 404)
        except Exception as e:
            self._json({"error": str(e)}, 500)

    def do_POST(self):
        path = urlparse(self.path).path
        if path != "/api/write":
            self._json({"error": "not found"}, 404)
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            values = json.loads(self.rfile.read(length) or b"{}")
            raw = self.access.dump()
            parsed = parse_fru(raw)
            areas = build_areas(raw, parsed, values)
            for key, (offset, data) in areas.items():
                self.access.write(offset, data)
            # verify
            new_raw = self.access.dump()
            check = parse_fru(new_raw)
            changed = []
            for key, area in (("board", "board_area"), ("product", "product_area")):
                info = check[area]
                if info is None:
                    continue
                for f in info["fields"]:
                    if f["name"] in values.get(key, {}) and f["value"] != values[key][f["name"]]:
                        changed.append("%s.%s" % (key, f["name"]))
            if changed:
                raise RuntimeError("verification failed for: " + ", ".join(changed))
            self._json({"message": "FRU updated and verified (board + product areas)."})
        except Exception as e:
            self._json({"error": str(e)}, 500)


def board_mfg_time(raw):
    off = raw[3] * 8
    if off + 6 > len(raw) or raw[off] != 0x01:
        return 0
    return raw[off + 3] | (raw[off + 4] << 8) | (raw[off + 5] << 16)


def main():
    ap = argparse.ArgumentParser(description="Web GUI for the uFC FRU EEPROM")
    ap.add_argument("--mch", default="192.168.1.41")
    ap.add_argument("--target", default="0x7e")
    ap.add_argument("--transit", default="0x82")
    ap.add_argument("--bus", default="7")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8099)
    args = ap.parse_args()

    Handler.access = FruAccess(args.mch, args.target, args.transit, args.bus)
    srv = ThreadingHTTPServer((args.host, args.port), Handler)
    print("FRU GUI on http://%s:%d  (MCH %s, target %s, bus %s)"
          % (args.host, args.port, args.mch, args.target, args.bus), flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
