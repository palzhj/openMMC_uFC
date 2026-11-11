#!/usr/bin/env python3
"""Inspect, modify or generate the u4FCP-v2 FRU template (fru_template.bin).

fru_template.bin is the FRU image that program_fru.py/program.sh merge with a
per-unit serial number and write to the MMC's FRU EEPROM.  This tool edits the
template itself, without a board:

    fru_template.py show [FILE]                 # decode and check the image
    fru_template.py verify FILE                 # exit 0 if the image is valid
    fru_template.py set FILE -o OUT ...         # edit fields / mfg time / current
    fru_template.py gen -o OUT                  # build it from fru_template.txt
    fru_template.py dump IMAGE -o OUT.txt       # write the text blueprint

The editable blueprint is fru_template.txt (sections [board], [product], [fru]
and [multirecord], see that file); ``dump`` writes one from an existing image,
so the round trip dump -> gen reproduces the image byte for byte.

The image is always rebuilt with fru_core.build_image(), so the common header,
the board/product area checksums and every multirecord checksum are recomputed
and the multirecord module-current byte is kept in sync with its record.

Layout of the current template (512-byte writable area):

    0    common header (8 bytes, area offsets in 8-byte units)
    8    board info area   (mfg time + 5 fields)
    64   product info area (7 fields)
    128  multirecord area: type 0xc0 records
           0x16 module current requirements (0.1 A units)
           0x2d clock configuration
           0x19 point-to-point connectivity
           0x30 OEM record (end of list)

usage examples:

    # what is in the template (fields, multirecord records, checksums)
    ./fru_template.py show fru_template.bin

    # bump the product version and the part number, write a new template
    ./fru_template.py set fru_template.bin -o fru_template_new.bin \\
        --product version=2.3 --board part_number=u4FCP-v2.3

    # 6 A module current (the byte is 0.1 A units), fixed manufacturing date
    ./fru_template.py set fru_template.bin --in-place \\
        --module-current-ma 6000 --mfg-time "2026-11-11 12:00"

    # regenerate the checked-in template from fru_template.txt (deterministic)
    ./fru_template.py gen -o fru_template.bin

    # write the text blueprint from an image, edit it, rebuild
    ./fru_template.py dump fru_template.bin -o my_template.txt
    ./fru_template.py gen --text my_template.txt -o my_template.bin

    # rebuild it from a board's current FRU (fields from --template, records
    # from --multi-from)
    ./fru_template.py gen -o fru_template_new.bin \\
        --template board.fru --multi-from board.fru

    # CI: fail if the checked-in template is malformed
    ./fru_template.py verify fru_template.bin --strict
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "uFC_toolbox")))

from fru_core import (  # noqa: E402
    FIELD_NAMES_BOARD, FIELD_NAMES_PRODUCT,
    build_image, fru_extent, mfg_minutes_to_str, mfg_str_to_minutes,
    parse_fru,
)

DEFAULT_TEMPLATE = os.path.join(HERE, "fru_template.bin")
DEFAULT_TEXT = os.path.join(HERE, "fru_template.txt")

DEFAULT_SIZE = 512              # u4FCP-v2 FRU area in the CAT24C512

ARD_AREA_TYPE = 0xC0            # PICMG/MTCA.4 multirecord header type
MULTI_NAME = {
    0x16: "module current requirements",
    0x19: "point-to-point connectivity",
    0x2d: "clock configuration",
    0x30: "OEM record",
}
# the module current record body: manufacturer(3) record id, format, current
MODULE_CURRENT_ID = 0x16


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

def read_image(path):
    with open(path, "rb") as f:
        raw = f.read()
    if not raw:
        raise ValueError("%s is empty" % path)
    try:
        parsed = parse_fru(raw)
    except ValueError as exc:
        raise ValueError("%s: %s" % (path, exc))
    return raw, parsed


def area_fields(area):
    """Field name -> value of a parsed board/product area ({} when absent)."""
    return {f["name"]: f["value"] for f in (area or {}).get("fields", [])}


def header_problems(raw, parsed):
    """Checksum/layout problems of the image (list of strings)."""
    problems = []
    if len(raw) < 8:
        return ["image shorter than the common header"]
    if raw[0] != 0x01:
        problems.append("bad common header format byte 0x%02x" % raw[0])
    if sum(raw[:8]) & 0xFF:
        problems.append("common header checksum is wrong")
    for name in ("board_area", "product_area"):
        area = parsed[name]
        if not area:
            problems.append("%s: missing or unreadable" % name)
            continue
        start = area["offset"]
        chunk = raw[start:start + area["length"]]
        if not chunk or sum(chunk) & 0xFF:
            problems.append("%s: checksum is wrong" % name)
        if area["length"] % 8:
            problems.append("%s: length %d is not a multiple of 8" % (name, area["length"]))
    multi = parsed.get("multi") or b""
    if multi:
        pos = 0
        while pos + 5 <= len(multi):
            typ, fmt, ln = multi[pos], multi[pos + 1], multi[pos + 2]
            if ln > len(multi) - pos - 5:
                problems.append("multirecord %d: length %d overruns the area" % (pos, ln))
                break
            body = multi[pos + 5:pos + 5 + ln]
            # the record checksum covers the body, the header checksum the
            # four header bytes before it
            if (sum(body) + multi[pos + 3]) & 0xFF:
                problems.append("multirecord %d: record checksum is wrong" % pos)
            if (typ + fmt + ln + multi[pos + 3] + multi[pos + 4]) & 0xFF:
                problems.append("multirecord %d: header checksum is wrong" % pos)
            pos += 5 + ln
            if fmt & 0x80:
                break
    return problems


def tail_bytes(raw, parsed):
    """(offset, bytes) of the unused padding after the FRU image."""
    end = parsed.get("image_len") or fru_extent(raw)
    return end, raw[end:]


def parse_assignment(text, allowed, what):
    if "=" not in text:
        raise ValueError("%s: expected NAME=VALUE, got %r" % (what, text))
    name, value = text.split("=", 1)
    name = name.strip().lower()
    if name not in allowed:
        raise ValueError("%s: unknown field %r (allowed: %s)"
                         % (what, name, ", ".join(allowed)))
    return name, value


TEXT_SECTIONS = ("board", "product", "fru", "multirecord")
TEXT_COMMENT_RE = __import__("re").compile(r"(?:^|\s)[#;]")


def load_text(path):
    """Parse the editable blueprint file.

    [board] / [product]   field = value        (the FRU info area fields)
    [fru]                 mfg_time            minutes since 1996-01-01 or
                                              "YYYY-MM-DD HH:MM" (0 = unset)
                          module_current_ma   AMC module current requirement
    [multirecord]         <record id> = hex   record body, header generated

    Returns {"board": {...}, "product": {...}, "mfg_time": ...,
             "module_current_ma": ..., "records": [(id, bytes), ...]}.
    """
    out = {"board": {}, "product": {}, "mfg_time": None,
           "module_current_ma": None, "records": []}
    section = None
    seen = set()

    def fail(lineno, message):
        raise ValueError("%s:%d: %s" % (path, lineno, message))

    with open(path, encoding="utf-8") as handle:
        for lineno, raw in enumerate(handle, 1):
            line = TEXT_COMMENT_RE.split(raw.rstrip("\n"), maxsplit=1)[0].strip()
            if not line:
                continue
            if line.startswith("[") and line.endswith("]"):
                section = line[1:-1].strip().lower()
                if section not in TEXT_SECTIONS:
                    fail(lineno, "unknown section [%s] (use one of %s)"
                         % (section, ", ".join(TEXT_SECTIONS)))
                continue
            if section is None:
                fail(lineno, "value outside a section")
            if "=" not in line:
                fail(lineno, "expected 'key = value'")
            key, value = (part.strip() for part in line.split("=", 1))
            if not key:
                fail(lineno, "empty key")
            ident = (section, key.lower())
            if ident in seen:
                fail(lineno, "duplicate key %r in [%s]" % (key, section))
            seen.add(ident)

            if section in ("board", "product"):
                allowed = FIELD_NAMES_BOARD if section == "board" else FIELD_NAMES_PRODUCT
                if key.lower() not in allowed:
                    fail(lineno, "unknown %s field %r (allowed: %s)"
                         % (section, key, ", ".join(allowed)))
                out[section][key.lower()] = value
            elif section == "fru":
                if key.lower() == "mfg_time":
                    out["mfg_time"] = int(value) if value.isdigit() \
                        else mfg_str_to_minutes(value)
                elif key.lower() == "module_current_ma":
                    out["module_current_ma"] = int(value)
                elif key.lower() == "serial":
                    out["board"]["serial"] = value
                    out["product"]["serial"] = value
                else:
                    fail(lineno, "unknown [fru] key %r (allowed: mfg_time, "
                                 "module_current_ma, serial)" % key)
            else:
                try:
                    rid = int(key, 0)
                except ValueError:
                    fail(lineno, "record id %r is not a number (use 0x16)" % key)
                try:
                    body = bytes.fromhex(value.replace(" ", "").replace("\t", ""))
                except ValueError:
                    fail(lineno, "record body is not hex: %r" % value[:24])
                if rid in [r[0] for r in out["records"]]:
                    fail(lineno, "duplicate record id 0x%02x" % rid)
                out["records"].append((rid, body))
    return out


def dump_text(board, product, mfg_time, module_current_ma, multi):
    """Render the blueprint text for an image (the inverse of load_text)."""
    lines = [
        "# u4FCP-v2 FRU template -- edit this file and rebuild the image with",
        "#   ./fru_template.py gen -o fru_template.bin",
        "# (a starting point can be written from any image with",
        "#  ./fru_template.py dump fru_template.bin -o fru_template.txt)",
        "# Values may contain spaces; '#' or ';' starts a comment.",
        "",
        "[board]",
    ]
    for name in FIELD_NAMES_BOARD:
        lines.append("%-16s = %s" % (name, board.get(name, "")))
    lines += ["", "[product]"]
    for name in FIELD_NAMES_PRODUCT:
        lines.append("%-16s = %s" % (name, product.get(name, "")))
    lines += ["", "[fru]",
              "# manufacturing time: minutes since 1996-01-01 or "
              '"YYYY-MM-DD HH:MM"; 0 = unset (the programmer sets it)',
              "%-16s = %s" % ("mfg_time", mfg_time if mfg_time is not None else 0)]
    if module_current_ma is not None:
        lines.append("# AMC module current requirement (stored in 100 mA steps)")
        lines.append("%-16s = %d" % ("module_current_ma", module_current_ma))
    lines += ["", "[multirecord]",
              "# record id = record body (hex); the 5-byte header, the length and",
              "# both checksums are generated, the last record gets the EOL bit"]
    pos = 0
    while pos + 5 <= len(multi):
        rid = multi[pos + 5 + 3] if len(multi) > pos + 5 + 3 else 0
        ln = multi[pos + 2]
        body = multi[pos + 5:pos + 5 + ln]
        lines.append("%-16s = %s" % ("0x%02x" % rid, " ".join("%02x" % b for b in body)))
        pos += 5 + ln
        if multi[pos - 5 - ln + 1] & 0x80:
            break
    return "\n".join(lines) + "\n"


def build_multi_records(records, module_current=None):
    """Encode the multirecord area from [(record id, body), ...].

    Each record gets its 5-byte header (type 0xc0, format 0x02, length, record
    checksum, header checksum); the last one carries the end-of-list bit.
    """
    if not records:
        raise ValueError("no multirecord records (empty [multirecord] section)")
    blob = bytearray()
    for index, (rid, body) in enumerate(records):
        body = bytearray(body)
        if not body:
            raise ValueError("multirecord record 0x%02x: empty body" % rid)
        if rid is not None and len(body) >= 4 and (body[3] & 0xFF) != (rid & 0xFF):
            raise ValueError("multirecord record 0x%02x: body says id 0x%02x "
                             "(record id and body disagree)" % (rid, body[3]))
        if rid == MODULE_CURRENT_ID:
            if len(body) < 6:
                raise ValueError("module current record is too short")
            if module_current is None:
                module_current = body[5]
            body[5] = module_current & 0xFF
        fmt = 0x02 | (0x80 if index == len(records) - 1 else 0x00)
        rcks = (-sum(body)) & 0xFF
        hcks = (-(ARD_AREA_TYPE + fmt + len(body) + rcks)) & 0xFF
        blob += bytes([ARD_AREA_TYPE, fmt, len(body), rcks, hcks]) + bytes(body)
    return bytes(blob)


def parsed_from_scratch(multi=b"", module_current=None):
    """Minimal parse_fru()-like dict for an image built without a template."""
    parsed = {
        "raw": b"",
        "image_len": 0,
        "multi": multi,
        "multi_off": 0,
        "board": {},
        "board_area": {"type": 0x01, "lang": 0x00},
        "product_area": {"type": 0x01, "lang": 0x00},
        "module_current": None,
    }
    if multi:
        # locate the module current record so build_image() can keep it in sync
        pos = 0
        while pos + 5 <= len(multi):
            ln = multi[pos + 2]
            body = multi[pos + 5:pos + 5 + ln]
            if len(body) >= 6 and body[0:3] == b"\x5A\x31\x00" \
                    and body[3] == MODULE_CURRENT_ID:
                # offset of the record (build_image adds the 5-byte header
                # and the 5-byte record-id/format/version prefix itself)
                parsed["module_current"] = {"offset": pos, "current": body[5]}
                break
            pos += 5 + ln
            if multi[pos - (5 + ln) + 1] & 0x80:
                break
    return parsed


def build_and_pad(parsed, board, product, module_current, size):
    image = build_image(parsed,
                        {"board": board, "product": product,
                         "module_current": module_current},
                        limit=size)
    if len(image) > size:
        raise ValueError("image is %d bytes, the area is %d" % (len(image), size))
    return image + b"\xFF" * (size - len(image))


def describe(parsed, path=None):
    """Human readable structure of a parsed FRU (list of lines)."""
    raw = parsed["raw"]
    end = parsed.get("image_len") or fru_extent(raw)
    out = []
    head8 = raw[:8]
    out.append("%s: %d bytes (FRU image %d bytes, %d unused)"
               % (path or "image", len(raw), end, len(raw) - end))
    out.append("header: %s  (checksum %s)"
               % (head8.hex(" "), "ok" if not (sum(head8) & 0xFF) else "WRONG"))
    for name, label in (("board_area", "board"), ("product_area", "product")):
        area = parsed[name]
        if not area:
            out.append("%s area: missing" % label)
            continue
        chunk = raw[area["offset"]:area["offset"] + area["length"]]
        out.append("%s area @%d len %d type 0x%02x lang %d checksum 0x%02x (%s)"
                   % (label, area["offset"], area["length"], area["type"],
                      area["lang"], area["checksum"],
                      "ok" if not (sum(chunk) & 0xFF) else "WRONG"))
        for field in area["fields"]:
            out.append("    %-16s %r" % (field["name"], field["value"]))
        if name == "board_area" and area.get("mfg_time") is not None:
            out.append("    %-16s %d (%s)"
                       % ("mfg_time", area["mfg_time"],
                          mfg_minutes_to_str(area["mfg_time"])))
    multi = parsed.get("multi") or b""
    if not multi:
        out.append("multirecord area: none")
    else:
        out.append("multirecord area @%d, %d bytes" % (parsed["multi_off"], len(multi)))
        pos = 0
        index = 0
        while pos + 5 <= len(multi):
            typ, fmt, ln = multi[pos], multi[pos + 1], multi[pos + 2]
            if ln > len(multi) - pos - 5:
                out.append("    #%d @%d type 0x%02x: length %d overruns the area"
                           % (index, pos, typ, ln))
                break
            body = multi[pos + 5:pos + 5 + ln]
            rid = body[3] if len(body) >= 4 and body[0:3] == b"\x5A\x31\x00" else None
            name = MULTI_NAME.get(rid, "record") if rid is not None else "record"
            extra = ""
            if rid == MODULE_CURRENT_ID and len(body) >= 6:
                extra = "  module current %.1f A" % (body[5] / 10.0)
            out.append("    #%d @%d type 0x%02x id %s len %d  %s%s"
                       % (index, pos, typ,
                          ("0x%02x" % rid) if rid is not None else "??",
                          ln, name, extra))
            pos += 5 + ln
            index += 1
            if fmt & 0x80:
                break
    start, tail = tail_bytes(raw, parsed)
    stray = [(start + i, b) for i, b in enumerate(tail) if b != 0xFF]
    if stray:
        out.append("unused tail: %d byte(s) not 0xFF, first at offset %d (0x%02x)"
                   % (len(stray), stray[0][0], stray[0][1]))
    else:
        out.append("unused tail: all 0xFF")
    return out


# ---------------------------------------------------------------------------
# commands
# ---------------------------------------------------------------------------

def cmd_show(args):
    raw, parsed = read_image(args.file)
    problems = header_problems(raw, parsed)
    if args.json:
        start, tail = tail_bytes(raw, parsed)
        print(json.dumps({
            "file": args.file,
            "size": len(raw),
            "image_len": parsed.get("image_len"),
            "header": raw[:8].hex(),
            "board": area_fields(parsed["board_area"]),
            "product": area_fields(parsed["product_area"]),
            "mfg_time": parsed.get("mfg_time"),
            "module_current": parsed.get("module_current"),
            "multirecord_bytes": len(parsed.get("multi") or b""),
            "tail_stray_bytes": [start + i for i, b in enumerate(tail) if b != 0xFF],
            "problems": problems,
        }, indent=2))
        return 0
    print("\n".join(describe(parsed, args.file)))
    if problems:
        print("problems:")
        for problem in problems:
            print("  - %s" % problem)
        return 1
    print("checks: ok")
    return 0


def cmd_verify(args):
    raw, parsed = read_image(args.file)
    problems = header_problems(raw, parsed)
    start, tail = tail_bytes(raw, parsed)
    stray = [(start + i, b) for i, b in enumerate(tail) if b != 0xFF]
    for problem in problems:
        print("FAIL  %s" % problem)
    if stray and args.strict:
        problems.append("unused tail has %d non-0xFF byte(s)" % len(stray))
        print("FAIL  unused tail has %d non-0xFF byte(s) first at 0x%02x"
              % (len(stray), stray[0][0]))
    elif stray:
        print("WARN  unused tail has %d non-0xFF byte(s) first at 0x%02x"
              % (len(stray), stray[0][0]))
    if problems:
        return 1
    print("OK    %s: %d bytes, image %d, board + product + %d multirecord byte(s)"
          % (args.file, len(raw), parsed.get("image_len"),
             len(parsed.get("multi") or b"")))
    return 0


def apply_changes(args, parsed):
    """Return (board, product, module_current, changes) after the edits."""
    board = area_fields(parsed["board_area"])
    product = area_fields(parsed["product_area"])
    changes = []

    def remember(area_name, values, name, old, new):
        if old != new:
            changes.append((area_name, name, old, new))
        values[name] = new

    if args.serial is not None:
        remember("board", board, "serial", board.get("serial"), args.serial)
        remember("product", product, "serial", product.get("serial"), args.serial)
    for text in args.board or []:
        name, value = parse_assignment(text, FIELD_NAMES_BOARD, "--board")
        remember("board", board, name, board.get(name), value)
    for text in args.product or []:
        name, value = parse_assignment(text, FIELD_NAMES_PRODUCT, "--product")
        remember("product", product, name, product.get(name), value)

    mfg = args.mfg_time
    if mfg is None:
        mfg = parsed["board"].get("mfg_time", 0)
    elif isinstance(mfg, str):
        mfg = int(mfg) if mfg.isdigit() else mfg_str_to_minutes(mfg)
    mfg = int(mfg)
    old_mfg = parsed["board"].get("mfg_time", 0)
    if old_mfg != mfg:
        changes.append(("board", "mfg_time", old_mfg, mfg))
    board["mfg_time"] = mfg

    module = parsed.get("module_current")
    module_current = args.module_current_ma
    if module_current is not None:
        if module is None:
            raise ValueError("the %s image has no module current record "
                             "(record id 0x%02x)" % (args.file, MODULE_CURRENT_ID))
        module_current = int(module_current) // 100      # 0.1 A units
        old = module.get("current")
        if old != module_current:
            changes.append(("multirecord", "module current",
                            "%.1f A" % (old / 10.0), "%.1f A" % (module_current / 10.0)))
    return board, product, module_current, changes


def write_image(args, parsed, board, product, module_current):
    size = args.size or len(parsed["raw"])
    image = build_and_pad(parsed, board, product, module_current, size)

    out = args.file if args.in_place else args.output
    if not out:
        raise ValueError("give -o OUT (or --in-place)")
    if args.dry_run:
        print("dry run: %d-byte image not written" % len(image))
        return out, image
    with open(out, "wb") as f:
        f.write(image)
    return out, image


def report_written(out, image, expect_board, expect_product, module_current):
    parsed = parse_fru(image)
    problems = header_problems(image, parsed)
    board = area_fields(parsed["board_area"])
    product = area_fields(parsed["product_area"])
    mfg = (parsed["board_area"] or {}).get("mfg_time")
    checks = [
        ("board fields", all(board.get(k) == v for k, v in expect_board.items()
                             if k != "mfg_time")),
        ("product fields", all(product.get(k) == v for k, v in expect_product.items())),
        ("board mfg time", mfg == expect_board.get("mfg_time")),
        ("checksums", not problems),
    ]
    if module_current is not None:
        module = parsed.get("module_current")
        checks.append(("module current",
                       module is not None and module.get("current") == module_current))
    print("written %s: %d bytes (image %d, %d multirecord byte(s))"
          % (out, len(image), parsed.get("image_len"), len(parsed.get("multi") or b"")))
    for name, ok in checks:
        print("  %-16s %s" % (name, "ok" if ok else "FAILED"))
    for problem in problems:
        print("  FAILED %s" % problem)
    if not all(ok for _, ok in checks):
        sys.exit(1)


def cmd_set(args):
    raw, parsed = read_image(args.file)
    board, product, module_current, changes = apply_changes(args, parsed)
    if not changes:
        print("no changes requested")
        return 1
    out, image = write_image(args, parsed, board, product, module_current)
    for area, name, old, new in changes:
        print("  %-11s %-14s %r -> %r" % (area, name, old, new))
    if args.dry_run:
        return 0
    report_written(out, image, board, product, module_current)
    return 0


def cmd_gen(args):
    """Build a complete template from the text blueprint (or an image)."""
    blueprint = {"board": {}, "product": {}, "mfg_time": None,
                 "module_current_ma": None, "records": []}
    if args.text:
        blueprint = load_text(args.text)

    board = dict(blueprint["board"])
    product = dict(blueprint["product"])
    mfg = blueprint["mfg_time"]
    current = None
    if blueprint["module_current_ma"] is not None:
        current = max(0, min(255, int(blueprint["module_current_ma"]) // 100))

    skeleton_raw = b""
    if args.template:
        # an image is the more specific source: it wins over the blueprint
        skeleton_raw, skeleton = read_image(args.template)
        board = area_fields(skeleton["board_area"])
        product = area_fields(skeleton["product_area"])
        mfg = skeleton["board"].get("mfg_time", 0)

    for name in FIELD_NAMES_BOARD:
        board.setdefault(name, "")
    for name in FIELD_NAMES_PRODUCT:
        product.setdefault(name, "")

    if args.mfg_time is not None:
        text = args.mfg_time
        mfg = int(text) if str(text).isdigit() else mfg_str_to_minutes(text)
    board["mfg_time"] = 0 if mfg is None else int(mfg)

    if args.module_current_ma is not None:
        current = max(0, min(255, int(args.module_current_ma) // 100))

    if args.no_multi:
        if current is not None:
            raise ValueError("--module-current-ma needs a multirecord area "
                             "(drop --no-multi)")
        multi = b""
    elif args.multi_from:
        multi = read_image(args.multi_from)[1].get("multi") or b""
    else:
        multi = build_multi_records(blueprint["records"], current)

    parsed = parsed_from_scratch(multi)
    if multi and current is not None and parsed["module_current"] is None:
        raise ValueError("--module-current-ma: the multirecord area has no "
                         "module current record (id 0x%02x)" % MODULE_CURRENT_ID)
    if not multi:
        print("note: image has no multirecord area (no module current, clock "
              "or connectivity record)", file=sys.stderr)

    size = args.size or (len(skeleton_raw) if skeleton_raw else DEFAULT_SIZE)
    image = build_and_pad(parsed, board, product, current, size)
    if args.dry_run:
        print("dry run: %d-byte image not written" % len(image))
        return 0
    with open(args.output, "wb") as f:
        f.write(image)
    report_written(args.output, image, board, product, current)
    return 0


def cmd_dump(args):
    """Write the text blueprint of an image (load_text() can read it back)."""
    raw, parsed = read_image(args.file)
    board = area_fields(parsed["board_area"])
    product = area_fields(parsed["product_area"])
    module = parsed.get("module_current")
    text = dump_text(board, product, parsed["board"].get("mfg_time"),
                     (module or {}).get("current", 0) * 100,
                     parsed.get("multi") or b"")
    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(text)
        print("written %s (%d bytes of blueprint)" % (args.output, len(text)))
    else:
        sys.stdout.write(text)
    return 0


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    show = sub.add_parser("show", help="decode an image and check the checksums")
    show.add_argument("file", nargs="?", default=DEFAULT_TEMPLATE)
    show.add_argument("-j", "--json", action="store_true", help="machine readable")
    show.set_defaults(func=cmd_show)

    verify = sub.add_parser("verify", help="exit 0 when the image is valid")
    verify.add_argument("file", nargs="?", default=DEFAULT_TEMPLATE)
    verify.add_argument("--strict", action="store_true",
                        help="also fail when the unused tail is not all 0xFF")
    verify.set_defaults(func=cmd_verify)

    dump = sub.add_parser("dump", help="write the editable blueprint of an image")
    dump.add_argument("file", nargs="?", default=DEFAULT_TEMPLATE)
    dump.add_argument("-o", "--output", help="output text file (default: stdout)")
    dump.set_defaults(func=cmd_dump)

    for name, func, text in (("set", cmd_set, "edit fields of an image"),
                             ("gen", cmd_gen,
                              "generate an image from the text blueprint")):
        p = sub.add_parser(name, help=text)
        if name == "set":
            p.add_argument("file", nargs="?", default=DEFAULT_TEMPLATE)
            p.add_argument("--board", action="append", metavar="NAME=VALUE",
                           help="board field (manufacturer, product_name, serial, "
                                "part_number, fru_file_id)")
            p.add_argument("--product", action="append", metavar="NAME=VALUE",
                           help="product field (manufacturer, product_name, "
                                "part_number, version, serial, asset_tag, fru_file_id)")
            p.add_argument("--serial", help="board and product serial")
            p.add_argument("--module-current-ma", type=int,
                           help="PICMG module current requirement (mA, 100 mA steps)")
            p.add_argument("-o", "--output", help="output file")
            p.add_argument("--in-place", action="store_true", help="overwrite FILE")
        else:
            p.add_argument("-o", "--output", required=True, help="output file")
            p.add_argument("--text", metavar="FILE",
                           help="editable blueprint with the fields and the "
                                "record bodies (default: %s)" % DEFAULT_TEXT)
            p.add_argument("--template", metavar="IMAGE",
                           help="take the field values from an existing image "
                                "instead of the blueprint")
            p.add_argument("--multi-from", metavar="IMAGE",
                           help="take the multirecord area from an existing "
                                "image instead of the blueprint")
            p.add_argument("--no-multi", action="store_true",
                           help="drop the multirecord area")
            p.add_argument("--module-current-ma", type=int,
                           help="PICMG module current requirement (mA)")
            p.set_defaults(template=None, text=DEFAULT_TEXT)
        p.add_argument("--mfg-time", help='"YYYY-MM-DD HH:MM" or minutes since 1996-01-01')
        p.add_argument("--size", type=int, help="image size (default: keep the input size)")
        p.add_argument("--dry-run", action="store_true", help="do not write the file")
        p.set_defaults(func=func)

    args = ap.parse_args(argv)
    try:
        return args.func(args)
    except (ValueError, OSError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
