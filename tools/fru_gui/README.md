# uFC Toolbox

Desktop application for the uFC module: **FRU EEPROM editing** and
**generic I²C transfers** (EEPROM, PMBus, clock chips) through the MMC,
using the firmware's raw EEPROM commands (custom NetFn `0x32`):

| Cmd | Meaning |
|---|---|
| `0x13` | EEPROM read: `[addr_lo addr_hi length]` → data |
| `0x12` | EEPROM write: `[addr_lo addr_hi data…]` |
| `0x15` / `0x14` | same read/write with an explicit I²C address |
| `0x20` | generic I²C transfer: `[bus n_mux (mux_addr mux_ch)… dev wlen wdata… rlen]` |

The firmware must be built with **`-DEEPROM_ACCESS=ON`** (FRU editing) and
**`-DI2C_ACCESS=ON`** (I²C transfer tab).

## Look

![uFC Toolbox](docs/ui.png)

## Features

- Quiet desktop styling (warm paper ground, one teal accent, hairline borders,
  Noto Sans / DejaVu Sans Mono), a chip mark in the header, status dot
- Connection fields: MCH IP, target IPMB address, bus, transit address
- One **FRU fields** tab with both the board and product areas, plus a **Raw EEPROM** tab
- Buttons: **Read FRU**, **Write FRU**, **Save image…**, **Load image to EEPROM**
- **Manufacturing time is shown and edited as a UTC date** (`YYYY-MM-DD HH:MM`);
  the app converts it to/from the FRU 24-bit value (minutes since 1996-01-01).
  A **Now** button fills the current UTC time.
- Every string box shows the **FRU specification maximum** (62 characters,
  6-bit type/length field including the NUL) and the fields are rebuilt with
  whatever length is entered — the board/product areas are re-encoded, the
  common-header offsets recomputed and the multirecord area moved after them.
- The whole FRU must fit the configured **writable** region (128 bytes on the
  uFC's 24AA025E48); the tool refuses a write that would overflow and reports
  the required size.
- Board/product fields;
  writing rebuilds only those two areas with fresh checksums and verifies the
  result by reading it back
- Status line and log pane

## Run from source

```sh
# Linux: Tkinter is a separate package on most distributions
sudo apt install python3-tk ipmitool

python3 tools/fru_gui/fru_gui_tk.py --mch 192.168.1.41 --target 0x7e --bus 7
```

Windows: install Python from python.org (Tkinter is included) and put
`ipmitool.exe` in `PATH` or next to the executable.

## Build a standalone binary

The app is plain Python + Tkinter; [PyInstaller](https://pyinstaller.org/)
produces a single-file executable.

```sh
pip install pyinstaller

# Linux
tools/fru_gui/build-linux.sh        # → tools/fru_gui/dist/ufc-toolbox

# Windows
tools\fru_gui\build-windows.bat     # → tools\fru_gui\dist\ufc-toolbox.exe
```

`ipmitool` is still an external program: install it system-wide, or place
`ipmitool` / `ipmitool.exe` next to the built executable — the app prefers the
local copy when present.

## What the connection fields mean

The app is not tied to one board: it talks to any MMC/BMC that exposes the
raw EEPROM commands over IPMI. Pick how many hops the request needs:

| Bridge | Use |
|---|---|
| **Double** | MCH → transit → target (MicroTCA AMC behind a crate manager, e.g. AMC7: `-T 0x82 -B 0 -t 0x7E -b 7`) |
| **Single** | MCH → target on one of its buses (`-t`/`-b` only) |
| **Direct** | talk to the MCH/BMC itself, no bridging |

Fields that the selected mode does not use are disabled automatically, and the
path line under the strip shows the resulting route.

With the double bridge the parameters are:

| Field | Meaning |
|---|---|
| **MCH / BMC** | First controller over IPMI-LAN: the crate manager or a BMC. |
| **Transit** | First hop: the intermediate IPMI controller that owns the target's bus. |
| **Transit ch** | Channel from the MCH to the transit controller (usually 0). |
| **Target** | IPMB address of the device that answers (the module's MMC). |
| **Bus** | IPMB channel that reaches the target (per slot on a MicroTCA crate). |
| **EEPROM I²C** | 7-bit I²C address of the EEPROM on the FRU bus (0x50 = board FRU; other addresses use commands 0x14/0x15). |
| **EEPROM size** | Bytes read from the EEPROM (16-bit addressing, max 65536). |
| **FRU writable** | Bytes of the FRU image written (the 24AA025E48 allows the lower 128). |

Single bridging (`-t 0x7e -b 7` without a transit) does not reach the MMC on
this crate; the transit hop is required.

### Where the EEPROM address is

None of the routing fields is the EEPROM's own address. The 24AA025E48 sits at
**I²C 0x50 on the FRU bus, behind TCA9548A channel 0** — that is fixed in the
firmware's chip map (`CHIP_ID_EEPROM`) and not exposed to the host. The raw
commands carry a **16-bit byte offset into the EEPROM** (`0x0000`–`0x00FF` for
the 256-byte part); the tool computes it from the FRU image, which starts at
offset 0. The app shows the range under the connection strip.

Hover any field for a short explanation.

## I²C transfer tab

A generic I²C transaction over IPMI (firmware `-DI2C_ACCESS=ON`, command
`0x20`), for EEPROMs, PMBus devices and clock chips:

- bus (I2C2 by default — everything on the uFC hangs off it),
- up to three mux stages (address, channel),
- device address, **Write len** (numeric), **Write data** (hex, one byte per
  token — must match Write len), **Read len**; a live line shows the resulting
  operation, e.g. `write 1 B · read 8 B → write then read`, and turns red
  when the data count and Write len disagree. Write len 0 → **read only**,
  Read len 0 → **write only**.

Presets: **EEPROM FRU** (0x70 ch0, 0x50, read 8), **UCD PMBus** (0x70 ch0,
0x11, `8B` → READ_VOUT), **Clock 8V54816** (0x70 ch2, 0x58) and **Si5345**
(0x70 ch2, 0x69).

## Auxiliary information

Below the connection strip the app shows, for every read:

```
LAN → MCH 192.168.1.41 → transit 0x82 (ch 0) → target 0x7e (bus 7)
128 B used · 0 B free · multirecord 56 B · checksums OK · mfg 2026-11-07 15:02
· MMC 0x00  fw 1.01  IPMI 2.0
```

- the bridge path built from the fields,
- FRU image use (used/free bytes), multirecord size and checksum state,
- manufacturing time, and the MMC's Get Device ID (firmware/IPMI version).

## Files

| File | Purpose |
|---|---|
| `fru_gui_tk.py` | Tkinter application |
| `fru_core.py` | FRU parsing/building + IPMI raw-transport backend |
| `build-linux.sh`, `build-windows.bat` | PyInstaller builds |

## Options

| Option | Default | Meaning |
|---|---|---|
| `--mch` | `192.168.1.41` | NAT-MCH address (IPMI over LAN) |
| `--target` | `0x7e` | MMC IPMB-L address (AMC7) |
| `--bus` | `7` | IPMB-L bus (`-b`) |
| `--transit` | `0x82` | carrier transit address (`-T`) |
| `--eeprom-size` | `256` | EEPROM image size in bytes (max 65536) |
| `--fru-limit` | `128` | writable FRU bytes (≤ EEPROM size) |

The same two values can be edited in the application's **EEPROM size** /
**FRU writable** fields before pressing *Read FRU*.

The image size is limited to **64 KB** because the raw EEPROM command carries a
16-bit address. Larger devices (e.g. 128 KB) would need a firmware command
extension first; the tool is otherwise generic (any EEPROM whose firmware
implements `0x32/0x12/0x13` with 16-bit addresses).

## Notes

- Writing changes the **live** FRU — save a backup image first.
- The MCH caches the FRU it read at discovery, so `show_fruinfo` may show old
  values until the module is re-discovered (`shutdown <fru>` + `fru_start <fru>`).
  The editor reads the EEPROM directly and always shows the current data.
- Field lengths are fixed by the existing FRU layout; values that do not fit
  are rejected with the maximum length shown.
