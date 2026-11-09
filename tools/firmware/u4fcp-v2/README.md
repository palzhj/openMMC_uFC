# u4FCP-v2 prebuilt images (STM32F103CBT6, 128 KB flash, 20 KB SRAM)

- `normal/` — production image (no UART output).
- `uart-debug/` — built with `-DUART_DEBUG=ON`; `printf` on USART1
  (PA9/PA10, 115200).

Build:

```bash
cmake -S . -B build-u4fcp-v2 -DBOARD=u4fcp-v2 \
      -DTARGET_CONTROLLER=STM32F103CBT6 -DCMAKE_BUILD_TYPE=Release
cmake --build build-u4fcp-v2 -j
```

Flash with J-Link from this directory:

```bash
./flash-jlink.sh uart-debug     # writes newboot and the application
# or, with a fixed image:
JLinkExe -CommanderScript flash.jlink
```

## Programming a brand-new board (production flow)

`program.sh` does the whole bring-up: flashes newboot + application over
SWD, clears the firmware-update record, writes the FRU with a **unique serial
number** and (optionally) restores the clock configuration. The serial is the
only mandatory argument.

```bash
# board in AMC12, MCH at 192.168.1.41
./program.sh U4FCP2-0001

# another slot / MCH
./program.sh U4FCP2-0001 --slot 7 --mch 192.168.1.41

# board already flashed: only the FRU (+ clock store)
./program.sh U4FCP2-0001 --no-flash --clock-store clock-slot.bin
```

What it does:

1. **SWD** — `newboot.bin` at `0x08000000`, `openMMC.bin` at `0x08002000`,
   and `fw_info` (`0x0801FC00`, 8 bytes) cleared to `0xFFFFFFFF` so the
   bootloader boots the application instead of a stale staging image.
2. **Waits** for the MMC to answer at `0x70 + 2*slot` through the MCH bridge.
3. **FRU** — `program_fru.py` builds the image from `fru_template.bin`
   (header + board/product areas + the 100‑byte multi‑record area with the
   point‑to‑point/clock/module‑current records), sets the board and product
   serials to the argument and the manufacturing time to now, writes the
   512‑byte image into the CAT24C512 at `0x54`, then reads it back and
   verifies the serials and checksums.
4. **Clock store** (optional) — `program_clock.py --upload` writes a clock
   store image captured from a programmed board.

Preparation for a batch:

```bash
# once, from a known-good board: capture the clock configuration
python3 program_clock.py --dump clock-slot.bin --slot 12
# per board:
./program.sh U4FCP2-0001 --clock-store clock-slot.bin
```

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
