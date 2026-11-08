#!/usr/bin/env python3
"""
fru_core.py -- FRU EEPROM access backend (IPMI raw commands 0x32/0x12, 0x13).

The MMC exposes raw FRU EEPROM access through the custom NetFn 0x32 commands
0x12 (write) / 0x13 (read).  This module reads the 256-byte FRU image through a
NAT-MCH (double-bridged), parses the board/product areas and rebuilds them with
fresh checksums.  Used by the desktop editor (fru_gui_tk.py).

Requires the firmware to be built with -DEEPROM_ACCESS=ON.
"""

import datetime
import os
import subprocess
import sys

# ---------------------------------------------------------------------------
# FRU helpers
# ---------------------------------------------------------------------------

EEPROM_SIZE = 256

# IPMI FRU board-area manufacturing time: minutes since 1996-01-01 00:00 UTC.
FRU_EPOCH = datetime.datetime(1996, 1, 1, tzinfo=datetime.timezone.utc)


def mfg_minutes_to_str(minutes):
    """Format the FRU manufacturing time (minutes since 1996) as a UTC date."""
    try:
        dt = FRU_EPOCH + datetime.timedelta(minutes=int(minutes))
    except Exception:
        return ""
    return dt.strftime("%Y-%m-%d %H:%M")


def mfg_str_to_minutes(text):
    """Parse 'YYYY-MM-DD HH:MM' (UTC) into FRU minutes since 1996."""
    text = (text or "").strip()
    for fmt in ("%Y-%m-%d %H:%M", "%Y-%m-%d"):
        try:
            dt = datetime.datetime.strptime(text, fmt).replace(tzinfo=datetime.timezone.utc)
            minutes = int((dt - FRU_EPOCH).total_seconds() // 60)
            if 0 <= minutes <= 0xFFFFFF:
                return minutes
        except ValueError:
            pass
    raise ValueError("invalid manufacturing time %r (use YYYY-MM-DD HH:MM, UTC)" % text)
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
            try:
                t = int(vals["mfg_time"])
            except ValueError:
                t = mfg_str_to_minutes(vals["mfg_time"])
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
