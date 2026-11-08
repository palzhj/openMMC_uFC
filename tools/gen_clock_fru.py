#!/usr/bin/env python3
"""
Build a FRU-format clock configuration slot for the uFC.

Layout of one 3 KB slot:
  0x000  wrapper (32 B)              magic UFCC, version, len, CRC32, seq, ...
  0x020  FRU file                    common header + multirecord OEM records
         record data = [dev_type][chunk_idx][chunk_cnt][bus][addr][n_mux]
                       [mux...][chunk]
The wrapper CRC32 covers the whole FRU file.

Records: mfr 0x315A, fmt 0, record ID 0x30 + chunk index (Si5345),
0x40 + index (clock switch matrix).
"""
import argparse, struct, zlib

REC_HDR = 4          # type, format/eol, len, hdr chksum
MFR = bytes([0x5A, 0x31, 0x00])

SI5345_TYPE, SWITCH_TYPE = 1, 2


def chk(data):
    return (-sum(data)) & 0xFF


def read_si5345(path):
    """Return the register list as [(addr, data)] with the delay marker."""
    regs = []
    section = None
    for line in open(path):
        line = line.strip()
        if line.startswith('#'):
            low = line.lower()
            if 'start configuration preamble' in low: section = 'pre'
            elif 'end configuration preamble' in low: section = None
            elif 'start configuration registers' in low: section = 'body'
            elif 'end configuration registers' in low: section = None
            elif 'start configuration postamble' in low: section = 'post'
            elif 'end configuration postamble' in low: section = None
            continue
        if section and line.startswith('0x'):
            a, d = line.split(',')
            regs.append((int(a, 16), int(d, 16)))
    return regs


def make_record(rec_id, payload, eol=False):
    data = bytearray()
    data += MFR
    data.append(rec_id)
    data.append(0x00)          # record format version
    data += payload
    data.append(0)             # record checksum placeholder
    data[-1] = chk(data)
    if len(data) > 255:
        raise ValueError("record data %d > 255" % len(data))
    rec = bytearray()
    rec.append(0xC0)
    rec.append(0x02 | (0x80 if eol else 0x00))
    rec.append(len(data))
    rec.append(0)
    rec[-1] = chk(rec)
    rec += data
    return bytes(rec)


def build_fru(records):
    fru = bytearray()
    fru += bytes([0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00])   # multirecord at 8
    fru[7] = chk(fru[:7])
    for i, rec in enumerate(records):
        fru += rec
    return bytes(fru)


def wrap(fru, seq, n_records, version=1, config_id=0):
    hdr = bytearray(32)
    hdr[0:4] = b"UFCC"
    struct.pack_into("<HHII", hdr, 4, version, 32, len(fru), zlib.crc32(fru) & 0xFFFFFFFF)
    struct.pack_into("<IHBBI", hdr, 16, seq, 0x0001, n_records, 0, config_id)
    return bytes(hdr) + fru


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--si5345", required=True)
    ap.add_argument("--matrix", default="00" * 16, help="16 bytes hex")
    ap.add_argument("--seq", type=int, default=1)
    ap.add_argument("--slot", type=int, default=3072, help="slot size (default 3 KB)")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    regs = read_si5345(args.si5345)
    if not regs:
        raise SystemExit("no registers parsed")
    table = b"".join(bytes([a >> 8, a & 0xFF, d]) for a, d in regs)
    matrix = bytes.fromhex(args.matrix.replace(" ", ""))
    if len(matrix) != 16:
        raise SystemExit("matrix must be 16 bytes")

    # record payload prefix: dev_type, chunk_idx, chunk_cnt, bus, addr, n_mux, mux(0x70,ch)
    def devhdr(dtype, idx, cnt, bus, addr, mux_ch):
        return bytes([dtype, idx, cnt, bus, addr, 1, 0x70, mux_ch])

    prefix = 8
    chunk_max = 255 - 6 - prefix          # record_len = 6 + payload
    chunks = [table[i:i + chunk_max] for i in range(0, len(table), chunk_max)]

    records = []
    for idx, c in enumerate(chunks):
        records.append(make_record(0x30 + idx,
                                   devhdr(SI5345_TYPE, idx, len(chunks), 1, 0x69, 2) + c))
    records.append(make_record(0x40, devhdr(SWITCH_TYPE, 0, 1, 1, 0x58, 2) + matrix, eol=True))

    fru = build_fru(records)
    slot = wrap(fru, args.seq, len(records))
    if len(slot) > args.slot:
        raise SystemExit("slot too small: %d > %d" % (len(slot), args.slot))
    slot += b"\xFF" * (args.slot - len(slot))
    open(args.out, "wb").write(slot)
    print("si5345 regs %d, table %d B in %d records, switch 1 record" % (len(regs), len(table), len(chunks)))
    print("fru file %d B, slot %d B, crc32 %08x, seq %d -> %s" %
          (len(fru), len(slot), zlib.crc32(fru) & 0xFFFFFFFF, args.seq, args.out))


if __name__ == "__main__":
    main()
