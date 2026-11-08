# uFC FRU editor

Tools to read and update the uFC **FRU EEPROM** through the MMC, using the
firmware's raw EEPROM commands (custom NetFn `0x32`):

| Cmd | Meaning |
|---|---|
| `0x13` | read: `[addr_lo addr_hi length]` → data |
| `0x12` | write: `[addr_lo addr_hi data…]` |

The firmware must be built with **`-DEEPROM_ACCESS=ON`**.

Two front-ends share the same backend (`fru_gui.py`): a **desktop application**
(MicroBoot-style, recommended) and a small **web page**.

---

## Desktop app (`fru_gui_tk.py`)

![layout](docs/screenshot.md)

- Connection fields: MCH IP, target IPMB address, bus, transit address
- Tabs: **Board info**, **Product info**, **Raw EEPROM**
- Buttons: **Read FRU**, **Write FRU**, **Save image…**, **Load image to EEPROM**
- Board/product fields are fixed length (max shown next to each field);
  writing rebuilds only those two areas with fresh checksums and verifies
  the result by reading it back.

### Run from source

```sh
# Linux: Tkinter is a separate package on most distributions
sudo apt install python3-tk ipmitool

python3 tools/fru_gui/fru_gui_tk.py --mch 192.168.1.41 --target 0x7e --bus 7
```

Windows: install Python from python.org (Tkinter is included) and put
`ipmitool.exe` in `PATH` or next to the executable.

### Build a standalone binary

The app is plain Python + Tkinter; [PyInstaller](https://pyinstaller.org/)
produces a single-file executable.

```sh
pip install pyinstaller

# Linux
tools/fru_gui/build-linux.sh        # → tools/fru_gui/dist/ufc-fru-editor

# Windows
tools\fru_gui\build-windows.bat     # → tools\fru_gui\dist\ufc-fru-editor.exe
```

`ipmitool` is still an external program: install it system-wide, or place
`ipmitool` / `ipmitool.exe` next to the built executable — the app prefers the
local copy when present.

---

## Web GUI (`fru_gui.py`)

```sh
python3 tools/fru_gui/fru_gui.py --mch 192.168.1.41 --target 0x7e --bus 7 --port 8099
```

Open <http://127.0.0.1:8099>. Same features as the desktop app, plus a
downloadable backup.

---

## Options (both front-ends)

| Option | Default | Meaning |
|---|---|---|
| `--mch` | `192.168.1.41` | NAT-MCH address (IPMI over LAN) |
| `--target` | `0x7e` | MMC IPMB-L address (AMC7) |
| `--bus` | `7` | IPMB-L bus (`-b`) |
| `--transit` | `0x82` | carrier transit address (`-T`) |
| `--port` | `8099` | web GUI HTTP port (web only) |

## Notes

- Writing changes the **live** FRU — save a backup image first.
- The MCH caches the FRU it read at discovery, so `show_fruinfo` may show old
  values until the module is re-discovered (`shutdown <fru>` + `fru_start <fru>`).
  Both front-ends read the EEPROM directly and always show the current data.
- Field lengths are fixed by the existing FRU layout; values that do not fit
  are rejected with the maximum length shown.
