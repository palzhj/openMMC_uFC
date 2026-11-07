#!/usr/bin/env python3
"""Convert a ClockBuilder Pro Si5345 register export into an openMMC header.

Usage:
    scripts/gen_si5345_registers.py Si5345-RevD-Registers.txt \\
        port/board/ufc-v2/default_0-Registers.h 0

The input is the "Registers.txt" file produced by ClockBuilder Pro; every
configuration line has the form  0xADDR16,0xDATA8  where the address high byte
is the page (register 0x01) and the low byte is the register offset.

The output matches the layout used by payload.c::pll_configuration(), which
assumes the 300 ms calibration delay sits after the 3 preamble writes.
"""
import re
import sys

def main(src, dst, index):
    lines = open(src, encoding="utf-8", errors="replace").read().splitlines()

    entries = []          # (addr, data)
    sections = []         # section name per entry
    section = "config"
    for ln in lines:
        s = ln.strip()
        if "configuration preamble" in s and s.startswith("# Start"):
            section = "preamble"; continue
        if "configuration registers" in s and s.startswith("# Start"):
            section = "config"; continue
        if "configuration postamble" in s and s.startswith("# Start"):
            section = "postamble"; continue
        if s.startswith("# End configuration"):
            section = "config"; continue
        m = re.match(r"^0x([0-9A-Fa-f]{4})\s*,\s*0x([0-9A-Fa-f]{2})$", s)
        if m:
            entries.append((int(m.group(1), 16), int(m.group(2), 16)))
            sections.append(section)

    assert entries, "no register lines found"
    n = len(entries)
    guard = f"SI5345_REVD_REG_CONFIG_HEADER_{index}"

    out = []
    out.append("/*")
    out.append(" * Si5345 configuration register export (generated file, do not edit)")
    out.append(" *")
    out.append(f" * Source  : {src.split('/')[-1]}")
    out.append(" * Part    : Si5345 (Rev D)")
    out.append(" * Tool    : Skyworks ClockBuilder Pro")
    out.append(" * Entries : %d (%d preamble, %d configuration, %d postamble)"
               % (n,
                  sections.count("preamble"),
                  sections.count("config"),
                  sections.count("postamble")))
    out.append(" *")
    out.append(" * Addresses are 16-bit: high byte = page (register 0x01), low byte =")
    out.append(" * register offset.  payload.c::pll_configuration() writes the page when")
    out.append(" * it changes and waits 300 ms after the 3 preamble writes.")
    out.append(" *")
    out.append(" * Regenerate with scripts/gen_si5345_registers.py.")
    out.append(" */")
    out.append("")
    out.append(f"#ifndef {guard}")
    out.append(f"#define {guard}")
    out.append("")
    out.append(f"#define SI5345_REVD_REG_CONFIG_NUM_REGS {n}")
    out.append("")
    out.append('#include "payload.h"')
    out.append("")
    out.append(f"const si5345_revd_register_t si5345_revd_registers_{index}[SI5345_REVD_REG_CONFIG_NUM_REGS] =")
    out.append("{")
    cur = None
    for (addr, data), sec in zip(entries, sections):
        if sec != cur:
            out.append(f"\n\t/* {sec} */")
            cur = sec
        out.append("\t{ 0x%04X, 0x%02X }," % (addr, data))
    out.append("};")
    out.append("")
    out.append(f"#endif /* {guard} */")
    out.append("")

    with open(dst, "w") as f:
        f.write("\n".join(out))
    print(f"{dst}: {n} registers")

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else "0")
