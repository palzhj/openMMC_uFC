# FRU GUI

Small browser GUI to read and update the uFC **FRU EEPROM** through the MMC.

It uses the firmware's raw EEPROM access commands (custom NetFn `0x32`):

| Cmd | Meaning |
|---|---|
| `0x13` | read: `[addr_lo addr_hi length]` → data |
| `0x12` | write: `[addr_lo addr_hi data…]` |

The firmware must be built with **`-DEEPROM_ACCESS=ON`**.

## Usage

```sh
python3 tools/fru_gui/fru_gui.py --mch 192.168.1.41 --target 0x7e --bus 7 --port 8099
```

Then open <http://127.0.0.1:8099>.

Options (defaults match the AMC7 bench setup):

| Option | Default | Meaning |
|---|---|---|
| `--mch` | `192.168.1.41` | NAT-MCH address (IPMI over LAN) |
| `--target` | `0x7e` | MMC IPMB-L address (AMC7) |
| `--transit` | `0x82` | carrier transit address (`-T`) |
| `--bus` | `7` | IPMB-L bus (`-b`) |
| `--host` | `127.0.0.1` | HTTP bind address |
| `--port` | `8099` | HTTP port |

## What it does

- Reads the whole 256-byte FRU image (chunks of 31 bytes).
- Parses the **board** and **product** info areas (fixed-length fields, the
  board manufacturing time, area checksums).
- On *Write* it edits only those two areas **in place** (lengths preserved),
  recomputes both checksums, writes the areas back in 23-byte chunks and
  re-reads them to verify.
- *Download backup* saves the current 256-byte image.

The multirecord area (module current, AMC point-to-point) is left untouched.

## Notes

- Writing changes the **live** FRU; download a backup first.
- The MCH caches the FRU it read at module discovery — after an edit,
  `show_fruinfo` may still show the old values until the module is
  re-discovered (`shutdown <fru>` + `fru_start <fru>`). The GUI always reads
  the EEPROM directly, so it shows the current content.
- Field lengths are fixed by the existing FRU layout; the tool refuses values
  that do not fit (max shown next to each field).
