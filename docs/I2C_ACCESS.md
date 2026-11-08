# Universal I²C access over IPMI

The MMC exposes its I²C buses through custom IPMI commands (NetFn `0x32`), so a
host can talk to **the FRU EEPROM, PMBus devices (UCD90320), clock chips
(8V54816, Si5345)** and anything else on the module — including cascaded I²C
multiplexers — without opening a debugger.

Two layers are available:

| Layer | Commands | Use |
|---|---|---|
| **EEPROM helpers** | `0x12`/`0x13`, `0x14`/`0x15` | word-offset reads/writes inside an EEPROM (page-aware, retried) |
| **Generic transfer** | `0x20` | any I²C device: register/PMBus/EEPROM style, up to 3 mux stages |

The firmware must be built with:

```sh
cmake -DI2C_ACCESS=ON -DEEPROM_ACCESS=ON ...
```

`EEPROM_ACCESS` adds the EEPROM helpers, `I2C_ACCESS` adds command `0x20`.

---

## 1. Transport

The commands travel over the same path as any IPMI request. On a MicroTCA crate
the MMC is behind a double bridge:

```
host ── LAN ──► MCH ──► transit ──► target (MMC) ── IPMB-L ──► MMC
```

For the uFC in AMC7 of the NAT-MCH:

```sh
M="-I lan -H 192.168.1.41 -A none -T 0x82 -B 0 -t 0x7e -b 7"
```

`uFC Toolbox` performs the same requests and is described in §7.

---

## 2. EEPROM helpers

These address the EEPROM by **word offset** (0x0000–0x00FF for the 24AA025E48)
and handle the EEPROM's page writes and write-cycle delays internally.

### `0x13` — EEPROM read (board FRU EEPROM)

| | bytes |
|---|---|
| request | `addr_lo addr_hi len` |
| response | `len` data bytes |

- `len` = 1…31, `addr + len ≤ 256`

### `0x12` — EEPROM write (board FRU EEPROM)

| | bytes |
|---|---|
| request | `addr_lo addr_hi data…` (max 30 data bytes) |
| response | — |

- writes are split into 8-byte pages with the 5 ms write-cycle wait

### `0x15` / `0x14` — same, explicit I²C address

| Cmd | Request | Response |
|---|---|---|
| `0x15` read | `i2c_addr addr_lo addr_hi len` | `len` data bytes |
| `0x14` write | `i2c_addr addr_lo addr_hi data…` | — |

- `i2c_addr` = 0x08…0x77; same 256-byte offset limit
- `0x14` **probes the device first** (1-byte read), so writing to an absent
  address returns an error immediately instead of sitting in the write-retry
  loop for tens of seconds

### Example — read the FRU header

```sh
ipmitool $M raw 0x32 0x13 0x00 0x00 0x08
 01 00 00 01 05 09 00 f0
```

---

## 3. Generic transfer — `0x20`

One command covers read-only, write-only and write-then-read for any device on
any of the MMC's I²C interfaces, through a chain of up to three muxes.

### 3.1 Request

```
[ bus, n_mux, (mux_addr, mux_ch) × n_mux, dev, wlen, wdata…, rlen ]
```

| Field | Size | Meaning |
|---|---|---|
| `bus` | 1 | physical interface: `0` = I2C1, `1` = I2C2 |
| `n_mux` | 1 | number of mux stages, 0…3 |
| `mux_addr` | 1 | 7-bit address of the mux (e.g. `0x70`) |
| `mux_ch` | 1 | channel number 0…7 (converted to the TCA9548A bit mask `1<<ch`) |
| `dev` | 1 | 7-bit address of the target device |
| `wlen` | 1 | number of bytes written first (register/command/address) |
| `wdata` | `wlen` | the bytes themselves |
| `rlen` | 1 | number of bytes read back (0…31) |

### 3.2 Operation modes

| `wlen` | `rlen` | Operation | I²C sequence |
|---|---|---|---|
| 0 | >0 | **read only** | START, addr+R, read `rlen` |
| >0 | 0 | **write only** | START, addr+W, write `wlen` |
| >0 | >0 | **write then read** | START, addr+W, write `wdata`, repeated START, addr+R, read `rlen` |
| 0 | 0 | rejected | — |

### 3.3 Response

`rlen` data bytes. `rlen = 0` returns an empty response.

### 3.4 Size limits

The IPMB data field allows **25 bytes per request**, which the command's own
5 bytes consume:

```
wlen ≤ 20 − 2 × n_mux        (n_mux=1 → 18, n_mux=2 → 16, n_mux=3 → 14)
rlen ≤ 31
```

### 3.5 Mux chain

Each stage is selected before the transfer by writing its channel byte. Stages
are walked in the order given, i.e. the first is the one nearest the MMC:

```
I2C2 ─ 0x70 ch0 ─ 0x71 ch3 ─ device 0x50
       (stage 1)   (stage 2)
```

`mux_ch` is a **channel number** (0–7); the firmware writes the register value
`1<<ch`. A stage whose channel is not needed can be omitted.

### 3.6 Example — read 8 bytes from the FRU EEPROM through mux ch0

```sh
ipmitool $M raw 0x32 0x20 0x01 0x01 0x70 0x00 0x50 0x01 0x00 0x08
 01 00 00 01 05 09 00 f0
```

`bus=1 (I2C2)`, one mux (`0x70`, ch 0), device `0x50`, write `00`
(word offset), read 8.

### 3.7 Example — write 2 bytes to the FRU EEPROM

```sh
ipmitool $M raw 0x32 0x20 0x01 0x01 0x70 0x00 0x50 0x03 0x70 0x5A 0xA5 0x00
```

`wlen=3` (`70 5A A5` = offset 0x70 + two data bytes), `rlen=0`.

> **Page size:** `0x20` performs a single transaction. For EEPROM writes keep
> the payload inside one device page (≤ 8 bytes for the 24AA025E48 driver) or
> use `0x12`/`0x14`, which split and wait for the write cycle.

### 3.8 Example — UCD90320 PMBus, CLK_3V3 rail

The UCD is at `0x11` on mux ch0. Select page 17 (CLK_3V3), then READ_VOUT
(`0x8B`):

```sh
# PAGE = 17
ipmitool $M raw 0x32 0x20 0x01 0x01 0x70 0x00 0x11 0x02 0x00 0x11 0x00
# READ_VOUT (2 bytes, linear format: VOUT_MODE = 0x14 → 2^-12)
ipmitool $M raw 0x32 0x20 0x01 0x01 0x70 0x00 0x11 0x01 0x8B 0x02
 7c 34
```

`0x347C = 13436` → `13436 / 4096 = 3.280 V` ✓

### 3.9 Example — clock chip register

```sh
# 8V54816 (0x58) on mux ch2, read register 0x00
ipmitool $M raw 0x32 0x20 0x01 0x01 0x70 0x02 0x58 0x01 0x00 0x01
 00
```

---

## 4. uFC bus layout

All uFC devices hang off **I2C2** (`bus = 1`) behind one TCA9548A at `0x70`:

```
I2C2 ─ TCA9548A 0x70
        ├─ ch0: 24AA025E48 FRU EEPROM @0x50   (writable half 0x00–0x7F)
        ├─ ch0: UCD90320 PMBus @0x11
        └─ ch2: 8V54816 @0x58, Si5345 @0x69
```

| Device | bus | mux chain | address |
|---|---|---|---|
| FRU EEPROM | 1 | `(0x70, 0)` | `0x50` |
| UCD90320 | 1 | `(0x70, 0)` | `0x11` |
| 8V54816 | 1 | `(0x70, 2)` | `0x58` |
| Si5345 | 1 | `(0x70, 2)` | `0x69` |
| TCA9548A itself | 1 | — | `0x70` |

---

## 5. Errors and bus recovery

| Completion code | Meaning |
|---|---|
| `0x00` | OK |
| `0xC0` | interface busy — another task held the bus for > 200 ms |
| `0xC7` | request data length invalid |
| `0xCC` | invalid field (bus, mux count, address, offset+length) |
| `0xFF` | transfer failed (device NACK / absent) |

A failed transfer — and every boot — runs the I²C **bus recovery**: SCL is
clocked nine times, a STOP is generated, the peripheral is re-initialised and
the mux re-selected. An absent device therefore cannot wedge the FRU/clock bus.

---

## 6. Limits and caveats

- The EEPROM helpers are limited to a 16-bit word offset and 256 bytes
  (`EEPROM_ACCESS_SIZE`); the generic transfer is **not** size-limited by the
  firmware, only by the request/response sizes in §3.4.
- A transfer holds the interface lock; a slow device delays other I²C users
  (FRU task, PMBus polling) for the duration.
- `0x20` is a single attempt (no retries); use `0x14`/`0x15` for EEPROMs.
- `0x14`/`0x15` accept addresses `0x08…0x77`; `0x20` accepts `0x00…0x7F`
  (the transport does not add the R/W bit).
- Reading a PMBus device usually needs a command byte → use write-then-read.
  A pure read continues from the device's current internal pointer.

---

## 7. Using the uFC Toolbox

The **I²C transfer** tab wraps command `0x20`:

| Field | Meaning |
|---|---|
| Bus | I2C2 (uFC default) or I2C1 |
| Device | 7-bit target address |
| Write len / Read len | lengths of the two phases (0 selects read-only/write-only) |
| Write data | hex bytes, must match Write len |
| Mux stages | address/channel × 3, blank = unused |
| Presets | EEPROM FRU, UCD PMBus, Clock 8V54816, Si5345 |

The line under the fields shows the operation that will be sent
(`write 1 B · read 8 B → write then read`) and turns red if Write len and the
data disagree. Results are shown as hex and as decimal bytes.

The FRU editor also uses the EEPROM helpers (`0x13`/`0x12`, or `0x15`/`0x14`
when a different EEPROM address is configured).

---

## 8. Quick reference

```sh
M="-I lan -H 192.168.1.41 -A none -T 0x82 -B 0 -t 0x7e -b 7"

# FRU EEPROM (board default)
ipmitool $M raw 0x32 0x13 0x00 0x00 0x08          # read 8 bytes at 0
ipmitool $M raw 0x32 0x12 0x70 0x00 0x5A 0xA5     # write 2 bytes at 0x70

# EEPROM at another I2C address
ipmitool $M raw 0x32 0x15 0x51 0x00 0x00 0x08
ipmitool $M raw 0x32 0x14 0x51 0x70 0x00 0x5A 0xA5

# generic transfer (command 0x20)
ipmitool $M raw 0x32 0x20 <bus> <n_mux> [<mux> <ch>]… <dev> <wlen> <wdata…> <rlen>
```
