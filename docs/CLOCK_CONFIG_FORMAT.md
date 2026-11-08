# Clock configuration store — on-flash format (FRU records)

Reserved region: **7 KB** at `0x0801E000–0x0801FBFF` (`__ClockConfig_start/end`).
Purpose: hold clock/device configuration so it can be changed over IPMI without
reflashing, with a built-in fallback in `.text`.

## Region layout (7 × 1 KB pages)

| Range | Use |
|---|---|
| `0x0801E000–0x0801EBFF` | slot A (3 KB) |
| `0x0801EC00–0x0801F7FF` | slot B (3 KB) |
| `0x0801F800–0x0801FBFF` | pointer page (2 mirrored records) |

### Selected: FRU record file inside a small wrapper

The payload is a **FRU multirecord file** (standard TLV + checksums), generated
by `tools/gen_clock_fru.py`; the wrapper adds atomic A/B updates.

Measured: Si5345 table (1 578 B) → **7 records** (record payload ≤ 249 B),
switch matrix → 1 record → **FRU file 1 746 B**, slot 3 072 B.

Record IDs: `0x30 + chunk` for the Si5345 table, `0x40` for the switch matrix.
Each record data field is `mfr(3) 5A 31 00`, `rec_id`, `fmt_ver 0`, then
`dev_type, chunk_idx, chunk_cnt, bus, dev_addr, n_mux, mux…, chunk`.

## Slot header (32 B)

| Off | Field | Type |
|---|---|---|
| 0 | magic `UFCC` | u32 |
| 4 | format version | u16 |
| 6 | header length (32) | u16 |
| 8 | payload length | u32 |
| 12 | payload CRC32 | u32 |
| 16 | sequence (write counter) | u32 |
| 20 | flags | u16 |
| 22 | device count | u8 |
| 23 | reserved | u8 |
| 24 | config id / ClockBuilder revision | u32 |
| 28 | reserved | u32 |

Little-endian throughout.

## Device block (variable length)

| Off | Field | Type |
|---|---|---|
| 0 | type | u8 |
| 1 | bus | u8 (0 = I2C1, 1 = I2C2) |
| 2 | device address | u8 |
| 3 | flags | u8 |
| 4 | n_mux | u8 |
| 5 | reserved | u8 |
| 6 | payload length | u16 |
| 8 | reserved | u32 |
| 12 | mux path: `n_mux × {addr u8, ch u8}` | 2·n_mux B |
| … | payload | len B |

`n_mux` is a count, so **any depth** is expressible; the firmware skips paths
longer than its `CLOCK_CFG_MAX_MUX` instead of rejecting the image.

### Payload types

| Type | Name | Payload |
|---|---|---|
| 1 | I²C register list | `u16 delay_ms`, `u16 delay_after_n`, then `n × {u16 addr, u8 data}` |
| 2 | clock-switch matrix | 16 bytes (one per port) |
| 3 | generic I²C writes | `u8 n_blocks`, then `{u8 len, bytes…}` |
| 4 | PMBus command list | `{u8 page, u8 cmd, u8 len, data…}` |
| 0xFF | end of list | — |

**Delay pseudo-entry** (type 1 and 3): `addr = 0xFFFF`, `data = delay in ms/4
(1…255)`. This lets a list contain several waits (reset settle, PLL lock) instead
of the single `delay_ms` pair.

Unknown types are skipped using `len` (forward compatibility).

## Compatibility: Renesas 8V54816A (clock switch)

- 8-bit register space, **full 16-byte block** transfers → type 2 (matrix), or
  type 3 (write a 16-byte block).
- 16 ports, `bit7 = output`, `bits3:0 = source port`.

## Compatibility: ADI ADN4604ASVZ (16 × 16 crosspoint)

- I²C slave `0b10010xx` = **0x48…0x4B** (ADDR1/ADDR0), **8-bit register
  addresses** `0x00…0xB7`, one data byte per write.
- Matrix changes are applied by writing **0x01 to register 0x80** (XPT update);
  the reset register is 0x00 (write 0x01).
- Fits **type 1** unchanged: encode the 8-bit register as `0x00XX`, list the
  reset (0x00=0x01), a delay pseudo-entry, the configuration registers, and the
  update (0x80=0x01). Even the whole map is only ≈ 0xB8 × 3 ≈ 550 B.
- The device block carries its own address and mux path, so the same format
  works on an AFC/u4FCP board where the ADN4604 sits behind a different chain.

## Size budget (uFC)

| Content | Bytes |
|---|---|
| slot header | 32 |
| Si5345 register list (16 + 4 + 526×3) | 1 598 |
| 8V54816A matrix | 12 + 16 |
| end marker + alignment | ~4 |
| **total** | **≈ 1 662** |

3 KB per slot leaves ~1.3 KB for an additional device (e.g. an ADN4604 map of
~550 B, an oscillator, or a PMBus rail controller).

## Update flow (atomic)

1. write the inactive slot (header + blocks), CRC over the payload;
2. read back and verify the CRC;
3. write the pointer record (active slot, sequence, CRC) twice;
4. reboot/apply; a power loss at any step leaves the previous slot active;
5. if both slots are invalid, the `.text` default table is used.
