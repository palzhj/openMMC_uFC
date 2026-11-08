#!/usr/bin/env python3
"""
fru_core.py -- FRU EEPROM access backend (IPMI raw commands 0x32/0x12, 0x13).

The MMC exposes raw FRU EEPROM access through the custom NetFn 0x32 commands
0x12 (write) / 0x13 (read).  This module reads the FRU image through a NAT-MCH
(double-bridged), parses the board/product areas, and rebuilds the whole FRU
(fields resized to any legal length, multirecord preserved) within the
writable 128-byte half of the 24AA025E48.

Field length limit per the IPMI FRU specification: the type/length byte has a
6-bit length (max 63 bytes including the terminating NUL) -> 62 characters.
The area size must stay within the EEPROM's writable area.

Requires the firmware to be built with -DEEPROM_ACCESS=ON.
"""

import datetime
import os
import subprocess
import sys

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

EEPROM_SIZE = 256
FRU_MAX_SIZE = 128          # 24AA025E48 default: lower 128 bytes writable
MAX_EEPROM_SIZE = 0x10000   # 16-bit address in the raw IPMI commands (64 KB)
FRU_MAX_STRING = 62         # 6-bit field length, including the NUL

# IPMI FRU board-area manufacturing time: minutes since 1996-01-01 00:00 UTC.
FRU_EPOCH = datetime.datetime(1996, 1, 1, tzinfo=datetime.timezone.utc)

FIELD_NAMES_BOARD = ["manufacturer", "product_name", "serial", "part_number", "fru_file_id"]
FIELD_NAMES_PRODUCT = ["manufacturer", "product_name", "part_number", "version",
                       "serial", "asset_tag", "fru_file_id"]


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


# ---------------------------------------------------------------------------
# IPMI transport
# ---------------------------------------------------------------------------

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
    """IPMI transport.

    bridge='double': MCH -> transit (-T/-B) -> target (-t/-b)
    bridge='single': MCH -> target (-t/-b)
    bridge='direct': talk to the MCH/BMC itself (no bridging)
    """

    def __init__(self, mch, target=None, transit=None, bus=None, eeprom_size=EEPROM_SIZE,
                 transit_ch=0, bridge="double", i2c_addr=0x50):
        self.mch = mch
        self.eeprom_size = eeprom_size
        self.i2c_addr = i2c_addr
        self.bridge = ["-I", "lan", "-H", mch, "-A", "none"]
        if bridge == "double":
            if not (transit and target and bus is not None):
                raise ValueError("double bridge needs transit, target and bus")
            self.bridge += ["-T", transit, "-B", str(transit_ch), "-t", target, "-b", str(bus)]
        elif bridge == "single":
            if not (target and bus is not None):
                raise ValueError("single bridge needs target and bus")
            self.bridge += ["-t", target, "-b", str(bus)]
        elif bridge != "direct":
            raise ValueError("unknown bridge mode %r" % bridge)

    def _ipmi(self, *args):
        out = subprocess.run([ipmitool_cmd()] + self.bridge + ["raw"] + list(args),
                             capture_output=True, text=True, timeout=20)
        if out.returncode != 0:
            raise RuntimeError((out.stderr or out.stdout).strip() or "ipmitool failed")
        return bytes(int(x, 16) for x in out.stdout.split())

    def _addr_cmd(self):
        return ("0x12", "0x13") if self.i2c_addr == 0x50 else ("0x14", "0x15")

    def read(self, offset, length):
        _wr, rd_cmd = self._addr_cmd()
        data = bytearray()
        while length:
            n = min(31, length)  # response: completion code + data <= 32
            args = ["0x32", rd_cmd]
            if rd_cmd == "0x15":
                args.append("0x%02x" % self.i2c_addr)
            args += [str(offset & 0xFF), str((offset >> 8) & 0xFF), str(n)]
            data += self._ipmi(*args)
            offset += n
            length -= n
        return bytes(data)

    def write(self, offset, data):
        while data:
            # The MMC slave RX accepts 6 header + data_len <= 31 bytes, so a
            # request carries at most 25 data bytes: 2 address bytes + 23 payload.
            n = min(23, len(data))
            chunk = data[:n]
            wr_cmd = self._addr_cmd()[0]
            args = ["0x32", wr_cmd]
            if wr_cmd == "0x14":
                args.append("0x%02x" % self.i2c_addr)
            args += [str(offset & 0xFF), str((offset >> 8) & 0xFF),
                     *["0x%02x" % b for b in chunk]]
            self._ipmi(*args)
            offset += n
            data = data[n:]

    def dump(self):
        return self.read(0, self.eeprom_size)

    def hpm_command(self, args):
        """ipmitool 'hpm ...' command line using the configured bridge."""
        return [ipmitool_cmd()] + self.bridge + ["hpm"] + list(args)

    def xfer(self, bus, muxes, dev, wdata=b"", rlen=0):
        """Generic I2C transfer (custom command 0x20).

        bus   : physical interface (0 = I2C1, 1 = I2C2)
        muxes : [(address, channel), ...] up to 3 stages
        dev   : 7-bit target address
        wdata : bytes written first (register / command)
        rlen  : bytes read back (0 for a write-only transfer)
        """
        if len(muxes) > 3:
            raise ValueError("at most 3 mux stages")
        if len(wdata) > 16:
            raise ValueError("write data too long")
        if not 0 <= rlen <= 31:
            raise ValueError("read length must be 0..31")
        data = bytearray([bus, len(muxes)])
        for addr, ch in muxes:
            data += bytes([addr & 0xFF, ch & 0xFF])
        data += bytes([dev & 0xFF, len(wdata)]) + bytes(wdata) + bytes([rlen])
        return self._ipmi("0x32", "0x20", *["0x%02x" % b for b in data])

    def device_id(self):
        """IPMI Get Device ID (NetFn App 0x06, cmd 0x01) through the bridge."""
        return self._ipmi("0x06", "0x01")


# ---------------------------------------------------------------------------
# FRU parsing / building
# ---------------------------------------------------------------------------

def _checksum(area):
    return (-sum(area)) & 0xFF


def _parse_area(raw, area_off, names, is_board):
    if area_off == 0:
        return None
    area = raw[area_off:]
    if len(area) < 3:
        raise ValueError("area at 0x%02x is truncated" % area_off)
    length = area[1] * 8
    if length == 0 or len(area) < length:
        raise ValueError("bad area length at 0x%02x" % area_off)
    area = area[:length]
    info = {"offset": area_off, "length": length, "type": area[0],
            "lang": area[2], "fields": [], "checksum": area[-1]}
    pos = 3
    if is_board:
        if length < 6:
            raise ValueError("board area too short")
        info["mfg_time"] = area[3] | (area[4] << 8) | (area[5] << 16)
        pos = 6
    for name in names:
        if pos >= length - 1:
            break
        hdr = area[pos]
        n = hdr & 0x3F
        if n == 0 or pos + 1 + n > length - 1:
            break
        data = area[pos + 1: pos + 1 + n]
        info["fields"].append({"name": name, "len": n,
                               "value": data.rstrip(b"\x00").decode("latin-1")})
        pos += 1 + n
    return info


def parse_fru(raw, limit=FRU_MAX_SIZE):
    """Return the FRU fields, the multirecord blob and the area layout."""
    if len(raw) < 8 or raw[0] != 0x01:
        raise ValueError("not a FRU image (bad format byte)")
    board_off = raw[3] * 8
    product_off = raw[4] * 8
    multi_off = raw[5] * 8

    # FRU image extent: last non-0xFF byte inside the writable region.
    end = 0
    for i in range(min(limit, len(raw))):
        if raw[i] != 0xFF:
            end = i + 1

    parsed = {
        "raw": raw,
        "image_len": end,
        "multi_off": multi_off,
        "multi": raw[multi_off:end] if multi_off and multi_off < end else b"",
        "board": {},
        "board_area": _parse_area(raw, board_off, FIELD_NAMES_BOARD, True),
        "product_area": _parse_area(raw, product_off, FIELD_NAMES_PRODUCT, False),
    }
    if parsed["board_area"]:
        parsed["board"]["mfg_time"] = parsed["board_area"]["mfg_time"]
    return parsed


def _encode_area(area_type, lang, fields, prefix=b""):
    body = bytearray([area_type, 0, lang]) + bytearray(prefix)
    for text in fields:
        data = (text or "").encode("latin-1")
        if len(data) > FRU_MAX_STRING:
            raise ValueError("string too long (%d > %d): %r" % (len(data), FRU_MAX_STRING, text))
        body += bytes([0xC0 | (len(data) + 1)]) + data + b"\x00"
    body.append(0xC1)          # end-of-fields marker (openMMC convention)
    while len(body) % 8:
        body.append(0)
    body[1] = len(body) // 8
    body[-1] = 0
    body[-1] = _checksum(body)
    return bytes(body)


def build_image(parsed, values, limit=FRU_MAX_SIZE):
    """Rebuild the full FRU image from the edited fields (<= limit bytes)."""
    board_vals = values.get("board", {})
    product_vals = values.get("product", {})

    def val(area, name, default=""):
        v = area.get(name)
        return default if v is None else v

    mfg = board_vals.get("mfg_time", parsed["board"].get("mfg_time", 0))
    try:
        mfg = int(mfg)
    except (TypeError, ValueError):
        mfg = mfg_str_to_minutes(mfg)
    if not 0 <= mfg <= 0xFFFFFF:
        raise ValueError("manufacturing time out of range")

    board_info = parsed["board_area"]
    product_info = parsed["product_area"]
    board_type = board_info["type"] if board_info else 0x01
    product_type = product_info["type"] if product_info else 0x01
    board_lang = board_info["lang"] if board_info else 0x00
    product_lang = product_info["lang"] if product_info else 0x00

    board = _encode_area(
        board_type, board_lang,
        [val(board_vals, n) for n in FIELD_NAMES_BOARD],
        prefix=bytes([mfg & 0xFF, (mfg >> 8) & 0xFF, (mfg >> 16) & 0xFF]))
    product = _encode_area(
        product_type, product_lang,
        [val(product_vals, n) for n in FIELD_NAMES_PRODUCT])

    header = bytearray(8)
    header[0] = 0x01                     # format version
    header[1] = 0x00                     # internal use area offset
    header[2] = 0x00                     # chassis info area offset
    header[3] = (8 // 8)                 # board area offset (always 8)
    product_off = 8 + len(board)
    header[4] = product_off // 8
    multi = parsed["multi"]
    multi_off = product_off + len(product)
    header[5] = (multi_off // 8) if multi else 0
    header[6] = 0x00
    header[7] = 0
    header[7] = _checksum(header)

    image = bytes(header) + board + product + multi
    if len(image) > limit:
        raise ValueError("FRU image would be %d bytes; the writable area is %d bytes"
                         % (len(image), limit))
    return image


def image_for_eeprom(image, limit=FRU_MAX_SIZE):
    """Pad the image to the writable area with 0xFF."""
    if len(image) > limit:
        raise ValueError("image too large")
    return image + b"\xFF" * (limit - len(image))
