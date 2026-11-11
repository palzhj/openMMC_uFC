# u4FCP-v2 prebuilt images (STM32F103CBT6, 128 KB flash, 20 KB SRAM)

The images are **build outputs**, not committed files (`.gitignore` excludes
`*.bin`). Build them with:

```bash
cmake -S . -B .hw2/u4fcp -DBOARD=u4fcp-v2 \
      -DTARGET_CONTROLLER=STM32F103CBT6 -DCMAKE_BUILD_TYPE=Release
make -C .hw2/u4fcp openMMC -j
# -> .hw2/u4fcp/out/newboot.bin  (~4.5 KB)
# -> .hw2/u4fcp/out/openMMC.bin  (~54 KB)
```

> The watchdog is **enabled by default** (`-DDISABLE_WATCHDOG=ON` disables it
> for debugging). The image enables the IWDG: the STM32F1
> IWDG runs from the LSI with prescaler 64 (625 Hz), the reload is 1250 ticks
> = **2 s timeout** (`WATCHDOG_TIMEOUT`), and the WatchdogTask feeds every
> 666 ms (one third of the timeout). Verified: without feeds the board resets
> every ~2 s (5 boots in 10 s); with feeds it stays up through the 1.2 s clock
> store apply and the PLL lock.

`uart-debug/` (if present) is the same image with `-DUART_DEBUG=ON`
(`printf` on USART1 PA9/PA10, 115200); `normal/` is the production build.
`jlink_program.sh` looks for these images in `uart-debug/`, `normal/` next to
the script and otherwise falls back to the local build tree
(`.hw2/u4fcp/out`); `--bin-dir DIR` overrides it.

Quick flash with J-Link from this directory:

```bash
./jlink_program.sh               # recommended (finds the images, clears fw_info)
./flash-jlink.sh uart-debug     # legacy: newboot + application only
JLinkExe -CommanderScript flash.jlink   # fixed images
```

## Programming a brand-new board (production flow)

Prerequisites:

- one **J-Link** on the board's SWD header, with **VTref** wired to the
  board's 3.3 V and GND (without it the probe reports
  `Target voltage too low` — see the note at the end);
- the board reachable through the MCH's IPMB bridge for the IPMI steps:
  target address `0x70 + 2*slot` (AMC12 -> `0x88`), transit `0x82`,
  bus `0` — `ipmitool -I lan -H <mch> -A none -T 0x82 -B 0 -t 0x88 -b 0 …`;
- the images built (see above).

The bring-up is four steps; `program.sh` runs them in one go.

| script | does |
|---|---|
| `jlink_program.sh` | newboot + application over SWD, `fw_info` cleared |
| `fru_program.sh <serial>` | FRU with the unique serial |
| `clk_program.sh` | write/capture the clock configuration preset |
| `vadj_program.sh` | set/read the FMC0/FMC1 VADJ (TPL0102 digital pot) |
| `program.sh <serial>` | flash + FRU + VADJ (+ clock preset) |

```bash
# 1. flash (bench or crate, single J-Link!)
./jlink_program.sh

# 2. insert the board in the crate, handle latched; write the FRU
./fru_program.sh U4FCP2-0001                     # AMC12 by default
./fru_program.sh U4FCP2-0001 --slot 2 --mch 192.168.1.41

# 3. clock configuration preset (clk-preset.bin next to this script)
./clk_program.sh --apply --slot 2                # write + apply now

# 4. FMC VADJ (factory default 1.8 V, stored in the TPL0102 EEPROM)
./vadj_program.sh --read --slot 2
./vadj_program.sh --fmc0 1.8 --fmc1 1.8 --persist --slot 2

# everything at once
./program.sh U4FCP2-0001 --slot 2 --clock-store clk-preset.bin
```

> The MMC is reachable only when the board is powered and inserted (MCH state
> `M4`). A blank‑EEPROM board boots fine and serves the compiled factory FRU
> (`user_amc_fru.h`), so Steps 1‑2 can be separated by the bench/crate move.

What each step does:

1. **jlink_program.sh** — `newboot.bin` at `0x08000000`, `openMMC.bin` at
   `0x08002000`, and `fw_info` (`0x0801FC00`, 8 bytes) cleared to `0xFFFFFFFF`
   so the bootloader boots the application instead of a stale staging image;
   the board resets and runs immediately.
2. **fru_program.sh** — waits for the MMC at `0x70 + 2*slot` through the MCH
   bridge, builds the FRU from `fru_template.bin` + `fru_config.json`
   (header, board/product areas, 100‑byte multi‑record area with the
   point‑to‑point/clock/module‑current records), sets the board **and**
   product serial to the argument, the manufacturing time to now and the
   module current to `module_current_ma/100` (8 A), writes the 512‑byte image
   into the CAT24C512 at `0x54`, then reads it back and verifies the serials
   and checksums.
   The MMC serves the FRU from the EEPROM (`FRU_USE_COMPILED` OFF); after the
   write **reboot the MMC** (power cycle the slot or a J-Link reset) so the
   boot reads the new image — the log then shows `FRU info is healthy!`.
3. **clk_program.sh** — writes the clock store preset (pointer page + slot,
   `clk-preset.bin`) and optionally applies it (`0x32 0x19`). The payload
   applies the store in `PAYLOAD_STATE_CLK_SETUP` on **every** power-up (the
   ADN4604/Si5345 are on the 12 V rail and lose their configuration when it is
   cycled); a blank store falls back to the compiled defaults in flash. The
   shipped preset has **all ADN4604 outputs disabled** (input 8 selected) and
   the two ClockBuilder Si5345 tables — rebuild it from the compiled clocking
   with
   `python3 build_clk_preset.py --upload --capture ../../clocking/u4fcp-v2-clk-preset.bin`
   after changing `clock_config_default[]` or the register headers.
4. **vadj_program.sh** — trims the FMC0/FMC1 VADJ regulators through the
   TPL0102‑100PWR at `0x56` (mux ch7). `--persist` writes the non‑volatile
   register, so the value survives power cycles; 1.8 V is DAC 0 (factory
   default).

### The FRU template (`fru_template.bin`)

`fru_template.bin` is the 512-byte FRU image that `fru_program.sh` merges with
the per-unit serial (board + product areas, manufacturing time, and the
100-byte multirecord area with the module-current/clock/connectivity records).
It is **generated** — `fru_template.py` edits or rebuilds it without a board:

```bash
./fru_template.py show fru_template.bin            # decode + checksum check
./fru_template.py verify fru_template.bin --strict # CI check (exit 0 = valid)
./fru_template.py gen -o fru_template.bin          # regenerate (see below)

# edit fields, module current (the PICMG record stores 0.1 A units) and date
./fru_template.py set fru_template.bin -o new.bin \
    --product version=2.3 --board part_number=u4FCP-v2.3 \
    --serial U4FCP2-0001 --module-current-ma 6000 --mfg-time "2026-11-11 12:00"

# rebuild from a board's current FRU (fields and records taken from the image)
./fru_template.py gen -o fru_template.bin --template board.fru --multi-from board.fru
```

- The field values come from `fru_config.json` (same keys as the programmer
  uses; `serial` is only the template placeholder — `fru_program.sh` always
  writes the real one, and the template's manufacturing time is 0).
- The multirecord bodies (clock configuration, point-to-point connectivity,
  module current, OEM record) come from `fru_multirecord.json`;
  `--module-current-ma` / `module_current_ma` override the current byte and its
  checksum, `--multi-from IMAGE` copies the whole area from an existing image.
- `set` accepts `--board field=value` / `--product field=value` (the names are
  listed by `show`), `--serial` for both serials, and `--mfg-time` as minutes
  since 1996-01-01 or `"YYYY-MM-DD HH:MM"`.
- Every image is rebuilt through `fru_core.build_image()`, so the common
  header, the area checksums and all multirecord checksums are recomputed; the
  unused tail is filled with `0xFF`.

Verify afterwards:

```bash
ipmitool … -t 0x88 -b 0 fru print          # serials, mfg date
ipmitool … -t 0x88 -b 0 raw 0x32 0x18      # clock store valid + CRC
# MCH: show_fru (state M4), show_sensorinfo 16 (FMC0/FMC1 VADJ ≈ 1.8 V)
```

On the UART (115200 8N1) a power-up now logs, in a dmesg-like format:

```
[   0.046] vadj : FMC0 1800 mV, FMC1 1800 mV
[   0.058] pwr  : main power on
[   1.058] pwr  : FMC0 power on
[   2.063] pwr  : FMC1 power on
[   3.066] jtag : AMC=1 RTM=0 FMC0=0 FMC1=0
[   4.645] clk  : apply from EEPROM preset: 15 records, 1052 registers (1106 ms)
[   4.756] clk  : PLL0 locked, PLL1 locked
[   4.760] boot : payload power-up done
```

A payload-only power cycle (MCH `shutdown`/`fru_start`) keeps the MMC running
and starts with `pwr : payload restart (MMC uptime N s, no MMC reset)`.

### Common pitfalls

- `Target voltage too low` — the J‑Link's **VTref** is not connected to the
  board (or the board is unpowered); wire VTref + GND, power‑cycle the J‑Link
  USB and retry. Two J‑Links attached at once is the classic trap: the probe
  may talk to the wrong one.
- `jlink_program.sh` / `flash-jlink.sh` now judge the run by the J‑Link output
  (`Cortex-M3 identified` + no `Cannot connect`/`failed to …` line), because
  JLinkExe itself returns 0 for several failure modes. A run against an
  unpowered/disconnected target prints `ERROR: J-Link programming FAILED
  (no usable target)`, the relevant output and the VTref/one-probe hints, and
  exits **1** (so `program.sh` stops before the IPMI steps).
- FRU written but the MCH still shows the factory values — the MMC reads the
  FRU **once at boot**; reboot it after `fru_program.sh`.
- MCH power warning `requested power … exceeds backplane limit` — the FRU's
  module‑current record is wrong (must be 100 mA units, e.g. `8000 mA -> 80`).
- Clock preset written but not applied — either `--apply` or reboot (the
  configuration is applied in `PAYLOAD_STATE_CLK_SETUP`).
- MCH current/voltage display off by a factor — the MCH caches the SDR
  records; re‑read them by re‑inserting the module (or restarting the MCH).

`fru_config.json` holds the FRU field values (manufacturer, names, part
numbers, version, asset tag, module current); edit it once for the product.
The manufacturing time can be forced with `--mfg-time "2026-11-10 01:30"`.

Note: the FRU and clock‑store steps need the board reachable through the
MCH's IPMB‑L — for a bare bench board without an MCH, flash over SWD and
program the FRU later in the crate (`--no-flash`).

## Layout (128 KB)

| region | range | size |
|---|---|---|
| newboot | `0x08000000 – 0x08001FFF` | 8 KB |
| application | `0x08002000 – 0x08010DFF` | 59.5 KB |
| staging | `0x08010E00 – 0x0801FBFF` | 59.5 KB |
| `fw_info` | `0x0801FC00 – 0x0801FFFF` | 1 KB |

The clock configuration is stored in the CAT24C512 EEPROM
(pointer `0x200`, slots A/B at `0x300`/`0x1B00`, 6 KB each, slave 0x54), not
in flash — that is why the app/staging regions are 59.5 KB instead of 50 KB.
The application image is ~49 KB at `-O3`.

`newboot` is linked at `0x08000000` with its vector table at the reset vector,
so no checksum fix-up is required; both `loadfile` commands write only their
own bytes — never erase the first 8 KB unless updating newboot.

If the probe reports `Target voltage too low`: the script sets `VTREF 3300`,
but that applies only after a **J-Link power cycle** (unplug/replug the USB),
or wire the J-Link VTref pin to the board's 3.3 V + GND.
